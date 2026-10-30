#include "pch.h"

#include <codecapi.h>
#include <d3d11_4.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>

#include "dbg.h"
#include "json/json.h"
#include "platform/window.h"
#include "player/decoder.h"
#include "player/exporter.h"
#include "text.h"
#include "video/chunks.h"
#include "video/mp4.h"

namespace exporter {
    const int RETRIES = 2;
    const double SIZE_SAFETY = 0.93;
    const DWORD POOL = 8;
    const auto PROGRESS_EVERY = std::chrono::milliseconds(400);

    struct sound_t {
        double t = 0;
        double dur = 0;
        std::vector<uint8_t> data;
    };

    struct run_t {
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        ID3D11VideoDevice* vdev = nullptr;
        ID3D11VideoContext* vctx = nullptr;
        ID3D11VideoProcessorEnumerator* vpe = nullptr;
        ID3D11VideoProcessor* vp = nullptr;
        ID3D11VideoProcessorInputView* inputs[decoder::SURFACES] = {};
        video_decoder_t* dec = nullptr;
        IMFDXGIDeviceManager* mgr = nullptr;
        IMFVideoSampleAllocatorEx* alloc = nullptr;
        IMFSinkWriter* writer = nullptr;
        DWORD video = 0;
        DWORD audio = 0;
        bool has_audio = false;
        int w = 0;
        int h = 0;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::deque<job_t> g_jobs;
    std::thread g_thread;
    bool g_quit = false;
    std::string g_cancel;

    bool read_file(const std::wstring& path, std::vector<uint8_t>& out) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;
        _fseeki64(f, 0, SEEK_END);
        int64_t n = _ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
        out.resize(n > 0 ? (size_t)n : 0);
        bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        return ok;
    }

    bool cancelled(const std::string& id) {
        std::lock_guard<std::mutex> lock(g_mu);
        return g_quit || g_cancel == id;
    }

    void post_progress(const std::string& id, double p) {
        char num[32];
        snprintf(num, sizeof(num), "%.3f", std::clamp(p, 0.0, 1.0));
        window::post_json("{\"type\":\"clip_progress\",\"id\":\"" + json::escape(id) + "\",\"p\":" + num + "}");
    }

    void release(run_t& r) {
        if (r.writer)
            r.writer->Release();
        for (ID3D11VideoProcessorInputView*& v : r.inputs)
            if (v) {
                v->Release();
                v = nullptr;
            }
        if (r.dec)
            decoder::destroy(r.dec);
        IUnknown* all[] = {r.alloc, r.mgr, r.vp, r.vpe, r.vctx, r.vdev, r.ctx, r.dev};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        r = run_t{};
    }

    bool open_gpu(run_t& r, const mp4::track_t& track, const job_t& job, std::string& err) {
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &r.dev, nullptr, &r.ctx);
        if (FAILED(hr)) {
            err = "no gpu for the clip";
            return false;
        }
        ID3D11Multithread* mt = nullptr;
        if (SUCCEEDED(r.dev->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
        }
        r.dec = decoder::create(r.dev, track, err);
        if (!r.dec)
            return false;
        int sw = decoder::width(r.dec), sh = decoder::height(r.dec);
        r.h = job.height > 0 && job.height < sh ? job.height & ~1 : sh;
        r.w = job.width > 0 ? std::min(job.width, 4096) & ~1 : (int)std::lround((double)sw * r.h / sh) & ~1;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd = {D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE, {60, 1}, (UINT)sw, (UINT)sh, {(UINT)job.fps, 1}, (UINT)r.w, (UINT)r.h,
                                                 D3D11_VIDEO_USAGE_PLAYBACK_NORMAL};
        bool ok = SUCCEEDED(r.dev->QueryInterface(IID_PPV_ARGS(&r.vdev))) && SUCCEEDED(r.ctx->QueryInterface(IID_PPV_ARGS(&r.vctx))) &&
                  SUCCEEDED(r.vdev->CreateVideoProcessorEnumerator(&cd, &r.vpe)) && SUCCEEDED(r.vdev->CreateVideoProcessor(r.vpe, 0, &r.vp));
        for (int i = 0; ok && i < decoder::SURFACES; i++) {
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv = {};
            iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            iv.Texture2D.ArraySlice = (UINT)i;
            ok = SUCCEEDED(r.vdev->CreateVideoProcessorInputView(decoder::texture(r.dec), r.vpe, &iv, &r.inputs[i]));
        }
        if (!ok) {
            err = "cant scale the video";
            return false;
        }
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE in = {};
        in.YCbCr_Matrix = decoder::bt601(r.dec) ? 0 : 1;
        in.Nominal_Range = decoder::full_range(r.dec) ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255 : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        r.vctx->VideoProcessorSetStreamColorSpace(r.vp, 0, &in);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE out = {};
        out.YCbCr_Matrix = 1;
        out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        r.vctx->VideoProcessorSetOutputColorSpace(r.vp, &out);
        RECT src = {0, 0, sw, sh}, dst = {0, 0, r.w, r.h};
        r.vctx->VideoProcessorSetStreamFrameFormat(r.vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        r.vctx->VideoProcessorSetStreamSourceRect(r.vp, 0, TRUE, &src);
        r.vctx->VideoProcessorSetStreamDestRect(r.vp, 0, TRUE, &dst);
        r.vctx->VideoProcessorSetOutputTargetRect(r.vp, TRUE, &dst);
        return true;
    }

    IMFMediaType* video_type(GUID sub, const run_t& r, const job_t& job, int bitrate) {
        IMFMediaType* t = nullptr;
        MFCreateMediaType(&t);
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, sub);
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(t, MF_MT_FRAME_SIZE, (UINT32)r.w, (UINT32)r.h);
        MFSetAttributeRatio(t, MF_MT_FRAME_RATE, (UINT32)job.fps, 1);
        MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (sub == MFVideoFormat_H264) {
            t->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)bitrate);
            t->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
            t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        }
        return t;
    }

    IMFMediaType* audio_type(const mp4::track_t& track) {
        IMFMediaType* t = nullptr;
        MFCreateMediaType(&t);
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)track.rate);
        t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)track.channels);
        t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
        t->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        t->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
        std::vector<uint8_t> info(12, 0);
        info[2] = 0xfe;
        info.insert(info.end(), track.asc.begin(), track.asc.end());
        t->SetBlob(MF_MT_USER_DATA, info.data(), (UINT32)info.size());
        return t;
    }

    void set_rate(IMFSinkWriter* w, DWORD stream, const job_t& job, int bitrate) {
        ICodecAPI* api = nullptr;
        if (FAILED(w->GetServiceForStream(stream, GUID_NULL, IID_PPV_ARGS(&api))))
            return;
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = job.max_bytes ? eAVEncCommonRateControlMode_CBR : eAVEncCommonRateControlMode_PeakConstrainedVBR;
        api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);
        v.ulVal = (ULONG)bitrate;
        api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v);
        v.ulVal = (ULONG)(bitrate * 3 / 2);
        api->SetValue(&CODECAPI_AVEncCommonMaxBitRate, &v);
        v.ulVal = (ULONG)(job.fps * 2);
        api->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);
        api->Release();
    }

    bool open_writer(run_t& r, const job_t& job, const std::wstring& path, const mp4::track_t* sound, int bitrate, std::string& err) {
        UINT token = 0;
        IMFAttributes* attrs = nullptr;
        bool ok = SUCCEEDED(MFCreateDXGIDeviceManager(&token, &r.mgr)) && SUCCEEDED(r.mgr->ResetDevice(r.dev, token)) &&
                  SUCCEEDED(MFCreateAttributes(&attrs, 3));
        if (ok) {
            attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, r.mgr);
            ok = SUCCEEDED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs, &r.writer));
            attrs->Release();
        }
        if (!ok) {
            err = "cant create the clip file";
            return false;
        }
        IMFMediaType* out = video_type(MFVideoFormat_H264, r, job, bitrate);
        IMFMediaType* in = video_type(MFVideoFormat_NV12, r, job, bitrate);
        IMFAttributes* enc = nullptr;
        MFCreateAttributes(&enc, 4);
        enc->SetUINT32(CODECAPI_AVEncCommonRateControlMode, job.max_bytes ? eAVEncCommonRateControlMode_CBR : eAVEncCommonRateControlMode_PeakConstrainedVBR);
        enc->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, (UINT32)bitrate);
        enc->SetUINT32(CODECAPI_AVEncCommonMaxBitRate, (UINT32)(bitrate * 3 / 2));
        enc->SetUINT32(CODECAPI_AVEncMPVGOPSize, (UINT32)(job.fps * 2));
        ok = SUCCEEDED(r.writer->AddStream(out, &r.video)) && SUCCEEDED(r.writer->SetInputMediaType(r.video, in, enc));
        enc->Release();
        out->Release();
        if (!ok) {
            in->Release();
            err = "no h264 encoder";
            return false;
        }
        set_rate(r.writer, r.video, job, bitrate);
        if (sound) {
            IMFMediaType* a = audio_type(*sound);
            r.has_audio = SUCCEEDED(r.writer->AddStream(a, &r.audio)) && SUCCEEDED(r.writer->SetInputMediaType(r.audio, a, nullptr));
            a->Release();
            if (!r.has_audio)
                dbg::print("[clip] audio passthrough refused, the clip has no sound");
        }
        IMFAttributes* pool = nullptr;
        ok = SUCCEEDED(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&r.alloc))) && SUCCEEDED(r.alloc->SetDirectXManager(r.mgr)) &&
             SUCCEEDED(MFCreateAttributes(&pool, 2));
        if (ok) {
            pool->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER);
            ok = SUCCEEDED(r.alloc->InitializeSampleAllocatorEx(POOL, POOL * 2, pool, in));
            pool->Release();
        }
        in->Release();
        if (!ok || FAILED(r.writer->BeginWriting())) {
            err = "cant start the encoder";
            return false;
        }
        return true;
    }

    bool write_frame(run_t& r, const decoder::frame_t& f, double t, const job_t& job, std::string& err) {
        IMFSample* sample = nullptr;
        IMFMediaBuffer* buf = nullptr;
        IMFDXGIBuffer* dx = nullptr;
        ID3D11Texture2D* tex = nullptr;
        ID3D11VideoProcessorOutputView* ov = nullptr;
        UINT slice = 0;
        HRESULT hr = r.alloc->AllocateSample(&sample);
        bool ok = SUCCEEDED(hr) && SUCCEEDED(hr = sample->GetBufferByIndex(0, &buf)) && SUCCEEDED(hr = buf->QueryInterface(IID_PPV_ARGS(&dx))) &&
                  SUCCEEDED(hr = dx->GetResource(IID_PPV_ARGS(&tex))) && SUCCEEDED(hr = dx->GetSubresourceIndex(&slice));
        if (!ok)
            err = "no encoder surface";
        if (ok) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {};
            if (td.ArraySize > 1) {
                od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
                od.Texture2DArray.FirstArraySlice = slice;
                od.Texture2DArray.ArraySize = 1;
            } else {
                od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            }
            ok = SUCCEEDED(hr = r.vdev->CreateVideoProcessorOutputView(tex, r.vpe, &od, &ov));
            if (!ok)
                err = "cant draw into the encoder surface";
        }
        if (ok) {
            D3D11_VIDEO_PROCESSOR_STREAM st = {};
            st.Enable = TRUE;
            st.pInputSurface = r.inputs[f.surface];
            ok = SUCCEEDED(hr = r.vctx->VideoProcessorBlt(r.vp, ov, 0, 1, &st));
            if (!ok)
                err = "cant convert the frame";
        }
        if (ok) {
            IMF2DBuffer* flat = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&flat)))) {
                flat->GetContiguousLength(&length);
                flat->Release();
            }
            buf->SetCurrentLength(length);
            sample->SetSampleTime((LONGLONG)std::llround((t - job.from) * 1e7));
            sample->SetSampleDuration((LONGLONG)std::llround(1e7 / job.fps));
            ok = SUCCEEDED(hr = r.writer->WriteSample(r.video, sample));
            if (!ok)
                err = "the encoder refused a frame";
        }
        if (!ok) {
            char code[16];
            snprintf(code, sizeof(code), " 0x%08lx", hr);
            err += code;
        }
        IUnknown* all[] = {ov, tex, dx, buf, sample};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        return ok;
    }

    bool write_sound(run_t& r, const sound_t& s, const job_t& job) {
        IMFSample* sample = nullptr;
        IMFMediaBuffer* buf = nullptr;
        BYTE* p = nullptr;
        bool ok = SUCCEEDED(MFCreateSample(&sample)) && SUCCEEDED(MFCreateMemoryBuffer((DWORD)s.data.size(), &buf)) && SUCCEEDED(buf->Lock(&p, nullptr, nullptr));
        if (ok) {
            memcpy(p, s.data.data(), s.data.size());
            buf->Unlock();
            buf->SetCurrentLength((DWORD)s.data.size());
            sample->AddBuffer(buf);
            sample->SetSampleTime((LONGLONG)std::llround((s.t - job.from) * 1e7));
            sample->SetSampleDuration((LONGLONG)std::llround(s.dur * 1e7));
            ok = SUCCEEDED(r.writer->WriteSample(r.audio, sample));
        }
        if (buf)
            buf->Release();
        if (sample)
            sample->Release();
        return ok;
    }

    std::vector<sound_t> load_sound(const job_t& job, const mp4::track_t& track) {
        std::vector<sound_t> out;
        std::vector<uint8_t> data;
        std::vector<mp4::sample_t> samples;
        for (const chunk_t& c : chunks::scan(job.dir, 1)) {
            double start = (double)c.start / track.timescale;
            if (start > job.to || start + 10 < job.from)
                continue;
            samples.clear();
            if (!read_file(job.dir + chunks::name(1, c.idx), data) || !mp4::fragments(data.data(), data.size(), samples))
                continue;
            for (const mp4::sample_t& s : samples) {
                double t = (double)(s.dts + s.cto) / track.timescale;
                if (t < job.from || t >= job.to)
                    continue;
                sound_t x;
                x.t = t;
                x.dur = (double)s.dur / track.timescale;
                x.data.assign(data.begin() + s.offset, data.begin() + s.offset + s.size);
                out.push_back(std::move(x));
            }
        }
        return out;
    }

    bool encode(const job_t& job, const std::wstring& path, int bitrate, std::string& err) {
        mp4::track_t vtrack, atrack;
        std::vector<uint8_t> init;
        if (!read_file(job.dir + L"\\init-stream0.m4s", init) || !mp4::init(init.data(), init.size(), vtrack) || !vtrack.timescale) {
            err = "not a recording";
            return false;
        }
        bool sound = read_file(job.dir + L"\\init-stream1.m4s", init) && mp4::init(init.data(), init.size(), atrack) && atrack.timescale && atrack.rate > 0;
        run_t r;
        if (!open_gpu(r, vtrack, job, err) || !open_writer(r, job, path, sound ? &atrack : nullptr, bitrate, err)) {
            release(r);
            return false;
        }
        std::vector<sound_t> audio = r.has_audio ? load_sound(job, atrack) : std::vector<sound_t>{};
        size_t next_sound = 0;
        std::vector<chunk_t> list = chunks::scan(job.dir, 0);
        std::vector<uint8_t> data;
        std::vector<mp4::sample_t> samples;
        double step = 1.0 / job.fps, due = job.from;
        auto last_post = std::chrono::steady_clock::now();
        bool done = false, ok = !list.empty();
        auto take = [&](const decoder::frame_t& f) {
            double t = (double)f.pts / vtrack.timescale;
            if (t < job.from || t >= job.to || t + step / 2 < due) {
                decoder::release(r.dec, f.surface);
                done = done || t >= job.to;
                return;
            }
            due = std::max(due + step, t + step / 2);
            ok = ok && write_frame(r, f, t, job, err);
            decoder::release(r.dec, f.surface);
            while (next_sound < audio.size() && audio[next_sound].t <= t)
                ok = write_sound(r, audio[next_sound++], job) && ok;
            if (!ok && err.empty())
                err = "the encoder refused the sound";
            if (std::chrono::steady_clock::now() - last_post > PROGRESS_EVERY) {
                post_progress(job.id, (t - job.from) / std::max(0.001, job.to - job.from));
                last_post = std::chrono::steady_clock::now();
            }
        };
        for (int pos = chunks::find(list, (uint64_t)(job.from * vtrack.timescale)); ok && !done && pos < (int)list.size(); pos++) {
            if ((double)list[pos].start / vtrack.timescale >= job.to)
                break;
            if (cancelled(job.id)) {
                err = "cancelled";
                ok = false;
                break;
            }
            samples.clear();
            if (!read_file(job.dir + chunks::name(0, list[pos].idx), data) || !mp4::fragments(data.data(), data.size(), samples))
                continue;
            for (const mp4::sample_t& s : samples) {
                if (!ok || done)
                    break;
                if (!decoder::decode(r.dec, data.data() + s.offset, s.size, (int64_t)s.dts + s.cto, err)) {
                    ok = false;
                    break;
                }
                decoder::frame_t f;
                while (ok && decoder::pop(r.dec, f))
                    take(f);
            }
        }
        decoder::drain(r.dec);
        decoder::frame_t f;
        while (decoder::pop(r.dec, f)) {
            if (ok)
                take(f);
            else
                decoder::release(r.dec, f.surface);
        }
        while (ok && next_sound < audio.size())
            ok = write_sound(r, audio[next_sound++], job);
        if (ok && FAILED(r.writer->Finalize())) {
            err = "cant finish the clip file";
            ok = false;
        }
        if (!ok && err.empty())
            err = "the encoder failed";
        release(r);
        return ok;
    }

    uint64_t size_of(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA a;
        return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) ? ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow : 0;
    }

    void run_job(const job_t& job) {
        std::wstring part = job.out.substr(0, job.out.size() - 4) + L".part.mp4";
        int bitrate = job.bitrate;
        std::string err;
        uint64_t bytes = 0;
        bool ok = false;
        auto t0 = std::chrono::steady_clock::now();
        for (int attempt = 0; attempt <= RETRIES; attempt++) {
            err.clear();
            DeleteFileW(part.c_str());
            ok = encode(job, part, bitrate, err);
            bytes = ok ? size_of(part) : 0;
            if (!ok || !job.max_bytes || bytes <= job.max_bytes)
                break;
            dbg::print("[clip] %s came out %.1f mb over the %.1f mb limit, again with less bitrate", job.id.c_str(), bytes / 1048576.0, job.max_bytes / 1048576.0);
            bitrate = (int)(bitrate * (double)job.max_bytes / bytes * SIZE_SAFETY);
            ok = false;
            err = "cant fit the size limit";
        }
        if (ok)
            ok = MoveFileExW(part.c_str(), job.out.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        else
            DeleteFileW(part.c_str());
        double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        dbg::print("[clip] %s %s, %.1f mb, %.1f s for %.1f s of video%s%s", job.id.c_str(), ok ? "done" : "failed", bytes / 1048576.0, took, job.to - job.from,
                   err.empty() ? "" : ", ", err.c_str());
        window::post_json("{\"type\":\"clip_done\",\"id\":\"" + json::escape(job.id) + "\",\"ok\":" + (ok ? "true" : "false") + ",\"path\":\"" +
                          json::escape(text::utf8(job.out)) + "\",\"bytes\":" + std::to_string(ok ? bytes : 0) + ",\"err\":\"" + json::escape(err) + "\"}");
    }

    void run() {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION);
        while (true) {
            job_t job;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || !g_jobs.empty(); });
                if (g_quit)
                    break;
                job = g_jobs.front();
                g_jobs.pop_front();
                if (g_cancel == job.id)
                    continue;
                g_cancel.clear();
            }
            run_job(job);
        }
        MFShutdown();
        CoUninitialize();
    }

    void add(const job_t& job) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_thread.joinable())
                g_thread = std::thread(run);
            g_jobs.push_back(job);
        }
        g_cv.notify_all();
    }

    void cancel(const std::string& id) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cancel = id;
        g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(), [&](const job_t& j) { return j.id == id; }), g_jobs.end());
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
        }
        g_cv.notify_all();
        if (g_thread.joinable())
            g_thread.join();
    }
}
