#include "pch.h"

#include <audioclient.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <mmdeviceapi.h>

#include "dbg.h"
#include "player/audio.h"
#include "video/chunks.h"
#include "video/mp4.h"

namespace audio {
    using steady_t = std::chrono::steady_clock;

    const double GAP = 0.5;
    const REFERENCE_TIME DEVICE_BUFFER = 1000000;
    const double AHEAD = 0.3;

    struct src_t {
        std::wstring dir;
        mp4::track_t track;
        double origin = 0;
        std::vector<chunk_t> chunks;
        std::vector<uint8_t> data;
        std::vector<mp4::sample_t> samples;
        int chunk = -1;
        size_t next = 0;
        bool ended = false;
    };

    struct out_t {
        IMMDevice* device = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* render = nullptr;
        IAudioClock* clock = nullptr;
        ISimpleAudioVolume* volume = nullptr;
        UINT32 frames = 0;
        UINT64 freq = 0;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_thread;
    bool g_quit = false;
    bool g_open = false;
    bool g_start = false;
    bool g_stop = false;
    double g_target = 0;
    src_t g_next_src;
    bool g_new_src = false;
    float g_volume = 1.f;
    bool g_have_clock = false;
    double g_clock_t = 0;
    steady_t::time_point g_clock_at;

    src_t g_src;
    out_t g_out;
    IMFTransform* g_mft = nullptr;
    bool g_mft_provides = false;
    DWORD g_mft_size = 0;
    std::vector<float> g_pcm;
    size_t g_pcm_at = 0;
    double g_pcm_t = 0;
    double g_trim = -1;
    bool g_running = false;
    double g_base = 0;

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

    bool load(int pos) {
        g_src.samples.clear();
        g_src.next = 0;
        g_src.chunk = pos;
        return read_file(g_src.dir + chunks::name(1, g_src.chunks[pos].idx), g_src.data) &&
               mp4::fragments(g_src.data.data(), g_src.data.size(), g_src.samples);
    }

    bool gap_after(int pos) {
        if (pos + 1 >= (int)g_src.chunks.size() || g_src.samples.empty())
            return true;
        const mp4::sample_t& s = g_src.samples.back();
        return (double)g_src.chunks[pos + 1].start - (double)(s.dts + s.dur) > GAP * g_src.track.timescale;
    }

    double seconds(int64_t pts) {
        return (double)pts / g_src.track.timescale - g_src.origin;
    }

    void release_out() {
        if (g_out.client)
            g_out.client->Stop();
        IUnknown* all[] = {g_out.volume, g_out.clock, g_out.render, g_out.client, g_out.device};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        g_out = out_t{};
    }

    bool create_out() {
        release_out();
        IMMDeviceEnumerator* en = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
        if (SUCCEEDED(hr))
            hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &g_out.device);
        if (en)
            en->Release();
        if (SUCCEEDED(hr))
            hr = g_out.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_out.client);
        if (SUCCEEDED(hr)) {
            WAVEFORMATEXTENSIBLE wf = {};
            wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wf.Format.nChannels = (WORD)g_src.track.channels;
            wf.Format.nSamplesPerSec = (DWORD)g_src.track.rate;
            wf.Format.wBitsPerSample = 32;
            wf.Format.nBlockAlign = (WORD)(4 * wf.Format.nChannels);
            wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
            wf.Format.cbSize = sizeof(wf) - sizeof(WAVEFORMATEX);
            wf.Samples.wValidBitsPerSample = 32;
            wf.dwChannelMask = wf.Format.nChannels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            hr = g_out.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, DEVICE_BUFFER, 0,
                                          &wf.Format, nullptr);
        }
        if (SUCCEEDED(hr))
            hr = g_out.client->GetBufferSize(&g_out.frames);
        if (SUCCEEDED(hr))
            hr = g_out.client->GetService(IID_PPV_ARGS(&g_out.render));
        if (SUCCEEDED(hr))
            hr = g_out.client->GetService(IID_PPV_ARGS(&g_out.clock));
        if (SUCCEEDED(hr))
            hr = g_out.clock->GetFrequency(&g_out.freq);
        if (SUCCEEDED(hr) && SUCCEEDED(g_out.client->GetService(IID_PPV_ARGS(&g_out.volume))))
            g_out.volume->SetMasterVolume(g_volume, nullptr);
        if (FAILED(hr)) {
            dbg::print("[audio] output failed 0x%08lx", hr);
            release_out();
            return false;
        }
        return true;
    }

    void release_mft() {
        if (g_mft)
            g_mft->Release();
        g_mft = nullptr;
    }

    bool create_mft() {
        release_mft();
        MFT_REGISTER_TYPE_INFO in = {MFMediaType_Audio, MFAudioFormat_AAC};
        IMFActivate** acts = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in, nullptr, &acts, &count)))
            count = 0;
        for (UINT32 i = 0; i < count; i++) {
            if (!g_mft)
                acts[i]->ActivateObject(IID_PPV_ARGS(&g_mft));
            acts[i]->Release();
        }
        CoTaskMemFree(acts);
        if (!g_mft) {
            dbg::print("[audio] no aac decoder");
            return false;
        }
        IMFMediaType* t = nullptr;
        MFCreateMediaType(&t);
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)g_src.track.rate);
        t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)g_src.track.channels);
        t->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        std::vector<uint8_t> info(12, 0);
        info[2] = 0xfe;
        info.insert(info.end(), g_src.track.asc.begin(), g_src.track.asc.end());
        t->SetBlob(MF_MT_USER_DATA, info.data(), (UINT32)info.size());
        HRESULT hr = g_mft->SetInputType(0, t, 0);
        t->Release();
        bool found = false;
        for (DWORD i = 0; SUCCEEDED(hr) && !found; i++) {
            IMFMediaType* o = nullptr;
            if (FAILED(g_mft->GetOutputAvailableType(0, i, &o)))
                break;
            GUID sub = {};
            o->GetGUID(MF_MT_SUBTYPE, &sub);
            found = sub == MFAudioFormat_Float && SUCCEEDED(g_mft->SetOutputType(0, o, 0));
            o->Release();
        }
        if (FAILED(hr) || !found) {
            dbg::print("[audio] aac decoder setup failed 0x%08lx", hr);
            release_mft();
            return false;
        }
        MFT_OUTPUT_STREAM_INFO si = {};
        g_mft->GetOutputStreamInfo(0, &si);
        g_mft_provides = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        g_mft_size = std::max<DWORD>(si.cbSize, 1024 * 8 * 4);
        g_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        g_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    void take(IMFSample* s) {
        LONGLONG at = 0;
        s->GetSampleTime(&at);
        IMFMediaBuffer* b = nullptr;
        if (FAILED(s->ConvertToContiguousBuffer(&b)))
            return;
        BYTE* p = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
            const float* f = (const float*)p;
            size_t n = len / sizeof(float);
            size_t ch = (size_t)g_src.track.channels;
            double t = at / 1e7;
            if (g_trim >= 0) {
                size_t skip = 0;
                if (t < g_trim)
                    skip = std::min(n / ch, (size_t)((g_trim - t) * g_src.track.rate + 0.5)) * ch;
                if (skip < n) {
                    if (g_pcm_at >= g_pcm.size()) {
                        g_pcm.clear();
                        g_pcm_at = 0;
                    }
                    g_pcm_t = t + (double)(skip / ch) / g_src.track.rate;
                    g_trim = -1;
                    g_pcm.insert(g_pcm.end(), f + skip, f + n);
                }
            } else {
                g_pcm.insert(g_pcm.end(), f, f + n);
            }
            b->Unlock();
        }
        b->Release();
    }

    void drain_mft() {
        while (true) {
            MFT_OUTPUT_DATA_BUFFER od = {};
            if (!g_mft_provides) {
                IMFMediaBuffer* b = nullptr;
                MFCreateSample(&od.pSample);
                MFCreateMemoryBuffer(g_mft_size, &b);
                od.pSample->AddBuffer(b);
                b->Release();
            }
            DWORD status = 0;
            HRESULT hr = g_mft->ProcessOutput(0, 1, &od, &status);
            if (SUCCEEDED(hr) && od.pSample)
                take(od.pSample);
            if (od.pSample)
                od.pSample->Release();
            if (od.pEvents)
                od.pEvents->Release();
            if (FAILED(hr))
                break;
        }
    }

    bool decode_one() {
        while (g_src.next >= g_src.samples.size()) {
            if (gap_after(g_src.chunk) || !load(g_src.chunk + 1)) {
                g_src.ended = true;
                return false;
            }
        }
        const mp4::sample_t& s = g_src.samples[g_src.next++];
        IMFSample* in = nullptr;
        IMFMediaBuffer* b = nullptr;
        MFCreateSample(&in);
        MFCreateMemoryBuffer(s.size, &b);
        BYTE* p = nullptr;
        b->Lock(&p, nullptr, nullptr);
        memcpy(p, g_src.data.data() + s.offset, s.size);
        b->Unlock();
        b->SetCurrentLength(s.size);
        in->AddBuffer(b);
        b->Release();
        in->SetSampleTime((LONGLONG)(seconds((int64_t)s.dts + s.cto) * 1e7));
        in->SetSampleDuration((LONGLONG)((double)s.dur / g_src.track.timescale * 1e7));
        HRESULT hr = g_mft->ProcessInput(0, in, 0);
        if (hr == MF_E_NOTACCEPTING) {
            drain_mft();
            hr = g_mft->ProcessInput(0, in, 0);
        }
        in->Release();
        drain_mft();
        return SUCCEEDED(hr);
    }

    double buffered() {
        return (double)((g_pcm.size() - g_pcm_at) / g_src.track.channels) / g_src.track.rate;
    }

    void publish(bool have, double t) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_have_clock = have;
        g_clock_t = t;
        g_clock_at = steady_t::now();
    }

    bool write() {
        UINT32 padding = 0;
        if (FAILED(g_out.client->GetCurrentPadding(&padding)))
            return false;
        UINT32 room = g_out.frames - padding;
        size_t ch = (size_t)g_src.track.channels;
        UINT32 n = (UINT32)std::min<size_t>(room, (g_pcm.size() - g_pcm_at) / ch);
        if (n) {
            BYTE* p = nullptr;
            if (FAILED(g_out.render->GetBuffer(n, &p)))
                return false;
            memcpy(p, g_pcm.data() + g_pcm_at, (size_t)n * ch * sizeof(float));
            g_out.render->ReleaseBuffer(n, 0);
            g_pcm_at += (size_t)n * ch;
            if (g_pcm_at > g_pcm.size() / 2 && g_pcm_at > 48000) {
                g_pcm.erase(g_pcm.begin(), g_pcm.begin() + (ptrdiff_t)g_pcm_at);
                g_pcm_at = 0;
            }
        }
        UINT64 pos = 0;
        if (SUCCEEDED(g_out.clock->GetPosition(&pos, nullptr)) && g_out.freq)
            publish(true, g_base + (double)pos / g_out.freq);
        if (g_src.ended && g_pcm_at >= g_pcm.size() && padding == 0) {
            g_out.client->Stop();
            g_running = false;
            publish(false, 0);
        }
        return true;
    }

    void begin(double t) {
        if (g_out.client) {
            g_out.client->Stop();
            g_out.client->Reset();
        }
        g_running = false;
        publish(false, 0);
        if (!g_mft || (!g_out.client && !create_out()))
            return;
        g_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        g_pcm.clear();
        g_pcm_at = 0;
        g_trim = t;
        g_src.ended = false;
        int pos = g_src.chunks.empty() ? -1 : chunks::find(g_src.chunks, (uint64_t)std::max(0.0, (t + g_src.origin) * g_src.track.timescale));
        while (pos >= 0 && load(pos) && !g_src.samples.empty() && seconds((int64_t)g_src.samples[0].dts + g_src.samples[0].cto) > t && pos > 0)
            pos--;
        while (buffered() < AHEAD && decode_one())
            ;
        g_base = g_pcm_t;
        if (!write()) {
            create_out();
            return;
        }
        g_out.client->Start();
        g_running = true;
    }

    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
        while (true) {
            bool start = false, stop = false, fresh = false;
            double target = 0;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait_for(lock, std::chrono::milliseconds(g_running ? 10 : 1000), [] { return g_quit || g_start || g_stop || g_new_src; });
                if (g_quit)
                    break;
                fresh = g_new_src;
                if (fresh) {
                    g_src = std::move(g_next_src);
                    g_next_src = src_t{};
                    g_new_src = false;
                }
                start = g_start;
                stop = g_stop;
                target = g_target;
                g_start = g_stop = false;
                if (g_out.volume)
                    g_out.volume->SetMasterVolume(g_volume, nullptr);
            }
            if (fresh) {
                release_out();
                g_running = false;
                publish(false, 0);
                if (g_src.track.rate > 0)
                    create_mft();
                else
                    release_mft();
            }
            if (stop && g_out.client) {
                g_out.client->Stop();
                g_running = false;
                publish(false, 0);
            }
            if (start && g_mft)
                begin(target);
            if (!g_running)
                continue;
            while (buffered() < AHEAD && !g_src.ended && decode_one())
                ;
            if (!write()) {
                dbg::print("[audio] output lost, reopening");
                UINT64 pos = 0;
                double at = g_base;
                if (g_out.clock && SUCCEEDED(g_out.clock->GetPosition(&pos, nullptr)) && g_out.freq)
                    at += (double)pos / g_out.freq;
                release_out();
                begin(at);
            }
        }
        release_out();
        release_mft();
        MFShutdown();
        CoUninitialize();
    }

    void ensure() {
        if (!g_thread.joinable())
            g_thread = std::thread(run);
    }

    bool open(const std::wstring& dir, double origin, std::string& err) {
        src_t s;
        s.dir = dir;
        s.origin = origin;
        std::vector<uint8_t> init;
        bool ok = read_file(dir + L"\\init-stream1.m4s", init) && mp4::init(init.data(), init.size(), s.track) && s.track.codec == mp4::fourcc("mp4a") &&
                  s.track.rate > 0 && s.track.channels > 0 && s.track.timescale > 0;
        if (ok) {
            s.chunks = chunks::scan(dir, 1);
            ok = !s.chunks.empty();
        }
        if (!ok) {
            err = "no audio track";
            s = src_t{};
        }
        {
            std::lock_guard<std::mutex> lock(g_mu);
            ensure();
            g_next_src = std::move(s);
            g_new_src = true;
            g_start = g_stop = false;
        }
        g_cv.notify_all();
        return ok;
    }

    void close() {
        std::thread t;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
            t = std::move(g_thread);
        }
        g_cv.notify_all();
        if (t.joinable())
            t.join();
        std::lock_guard<std::mutex> lock(g_mu);
        g_quit = false;
        g_have_clock = false;
    }

    void start(double t) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_thread.joinable())
                return;
            g_start = true;
            g_stop = false;
            g_target = t;
            g_have_clock = false;
        }
        g_cv.notify_all();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_stop = true;
            g_start = false;
            g_have_clock = false;
        }
        g_cv.notify_all();
    }

    bool clock(double& t) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_have_clock)
            return false;
        t = g_clock_t + std::chrono::duration<double>(steady_t::now() - g_clock_at).count();
        return true;
    }

    void volume(float v) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_volume = std::clamp(v, 0.f, 1.f);
    }
}
