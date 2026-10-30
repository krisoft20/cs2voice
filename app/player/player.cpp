#include "pch.h"

#include <dxgi1_2.h>

#include <future>

#include "dbg.h"
#include "json/json.h"
#include "platform/compose.h"
#include "platform/window.h"
#include "player/audio.h"
#include "player/decoder.h"
#include "player/player.h"
#include "player/scaler.h"
#include "text.h"
#include "video/chunks.h"
#include "video/mp4.h"

namespace player {
    using steady_t = std::chrono::steady_clock;

    const double GAP_JUMP = 0.5;
    const size_t AHEAD = 6;
    const auto MAX_SLEEP = std::chrono::milliseconds(20);
    const auto REPORT_EVERY = std::chrono::milliseconds(250);

    enum cmd_kind_t { ck_open, ck_play, ck_pause, ck_seek, ck_close, ck_rate, ck_step, ck_resize };

    struct cmd_t {
        cmd_kind_t kind = ck_close;
        std::wstring dir;
        double t = 0;
        int w = 0;
        int h = 0;
    };

    struct play_t {
        std::wstring dir;
        mp4::track_t track;
        std::vector<chunk_t> chunks;
        int64_t start = 0;
        int64_t end = 0;
        video_decoder_t* dec = nullptr;
        std::vector<uint8_t> data;
        std::vector<mp4::sample_t> samples;
        int chunk = -1;
        int pending = -1;
        std::future<std::vector<uint8_t>> next_data;
        size_t next = 0;
        bool ended = false;
        std::deque<decoder::frame_t> ready;
        bool playing = false;
        double rate = 1;
        double frame = 1.0 / 60;
        double base_media = 0;
        steady_t::time_point base_wall;
        double shown = -1;
        steady_t::time_point reported;
        steady_t::time_point presented;
        steady_t::time_point counted;
        int frames = 0;
        int dropped = 0;
        double gap = 0;
        IDXGISwapChain1* swap = nullptr;
        ID3D11VideoDevice* vdev = nullptr;
        ID3D11VideoContext* vctx = nullptr;
        ID3D11VideoProcessorEnumerator* vpe = nullptr;
        ID3D11VideoProcessor* vp = nullptr;
        ID3D11VideoProcessorInputView* inputs[decoder::SURFACES] = {};
        ID3D11Texture2D* rgb = nullptr;
        ID3D11ShaderResourceView* rgb_view = nullptr;
        ID3D11VideoProcessorOutputView* rgb_out = nullptr;
        ID3D11RenderTargetView* target = nullptr;
        int video_w = 0;
        int video_h = 0;
        int swap_w = 0;
        int swap_h = 0;
        bool has_frame = false;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::deque<cmd_t> g_cmds;
    std::thread g_thread;
    bool g_quit = false;

    int g_video_w = 0;
    int g_video_h = 0;
    double g_rect[4] = {};
    std::mutex g_rect_mu;

    void apply() {
        IDCompositionVisual* v = compose::video_visual();
        if (!v)
            return;
        D2D_MATRIX_3X2_F m = {};
        if (g_video_w > 0 && g_video_h > 0 && g_rect[2] > 0 && g_rect[3] > 0) {
            m._11 = (FLOAT)(g_rect[2] / g_video_w);
            m._22 = (FLOAT)(g_rect[3] / g_video_h);
            m._31 = (FLOAT)g_rect[0];
            m._32 = (FLOAT)g_rect[1];
        }
        v->SetTransform(m);
        v->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR);
        compose::commit();
    }

    double seconds(const play_t* p, int64_t pts) {
        return p->track.timescale ? (double)pts / p->track.timescale : 0;
    }

    void report(play_t* p, const char* state, const std::string& err = "") {
        char num[96];
        snprintf(num, sizeof(num), ",\"t\":%.3f,\"start\":%.3f,\"end\":%.3f,\"rate\":%.2f", std::max(0.0, p->shown), seconds(p, p->start), seconds(p, p->end), p->rate);
        int w = p->dec ? decoder::width(p->dec) : 0, h = p->dec ? decoder::height(p->dec) : 0;
        window::post_json(std::string("{\"type\":\"player\",\"state\":\"") + state + "\"" + num + ",\"width\":" + std::to_string(w) + ",\"height\":" +
                          std::to_string(h) + ",\"err\":\"" + json::escape(err) + "\"}");
        p->reported = steady_t::now();
    }

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

    std::wstring chunk_path(const play_t* p, int pos) {
        return p->dir + chunks::name(0, p->chunks[pos].idx);
    }

    void prefetch(play_t* p) {
        int pos = p->chunk + 1;
        if (p->pending == pos || pos >= (int)p->chunks.size() || p->next < p->samples.size() / 2)
            return;
        if (p->next_data.valid())
            p->next_data.wait();
        p->pending = pos;
        p->next_data = std::async(std::launch::async, [path = chunk_path(p, pos)] {
            std::vector<uint8_t> out;
            read_file(path, out);
            return out;
        });
    }

    bool load(play_t* p, int pos) {
        p->samples.clear();
        p->next = 0;
        p->chunk = pos;
        bool ok;
        if (p->pending == pos && p->next_data.valid()) {
            p->data = p->next_data.get();
            ok = !p->data.empty();
        } else {
            ok = read_file(chunk_path(p, pos), p->data);
        }
        p->pending = -1;
        return ok && mp4::fragments(p->data.data(), p->data.size(), p->samples);
    }

    void drop_frames(play_t* p) {
        for (const decoder::frame_t& f : p->ready)
            decoder::release(p->dec, f.surface);
        p->ready.clear();
    }

    void close_presenter(play_t* p) {
        for (ID3D11VideoProcessorInputView*& v : p->inputs)
            if (v) {
                v->Release();
                v = nullptr;
            }
        IUnknown* all[] = {p->target, p->rgb_out, p->rgb_view, p->rgb, p->vp, p->vpe, p->vctx, p->vdev};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        p->target = nullptr;
        p->rgb_out = nullptr;
        p->rgb_view = nullptr;
        p->rgb = nullptr;
        p->has_frame = false;
        p->vp = nullptr;
        p->vpe = nullptr;
        p->vctx = nullptr;
        p->vdev = nullptr;
        if (p->swap) {
            window::on_ui([] {
                if (IDCompositionVisual* v = compose::video_visual())
                    v->SetContent(nullptr);
                g_video_w = g_video_h = 0;
                compose::commit();
            });
            p->swap->Release();
            p->swap = nullptr;
        }
    }

    void close_source(play_t* p) {
        if (p->dec) {
            drop_frames(p);
            close_presenter(p);
            decoder::destroy(p->dec);
        }
        p->dec = nullptr;
        if (p->next_data.valid())
            p->next_data.wait();
        p->next_data = {};
        p->pending = -1;
        p->samples.clear();
        p->data.clear();
        p->chunk = -1;
        p->playing = false;
        p->shown = -1;
    }

    bool open_presenter(play_t* p, std::string& err) {
        ID3D11Device* d3d = compose::d3d();
        int w = decoder::width(p->dec), h = decoder::height(p->dec);
        ID3D11DeviceContext* ctx = nullptr;
        d3d->GetImmediateContext(&ctx);
        bool ok = SUCCEEDED(d3d->QueryInterface(IID_PPV_ARGS(&p->vdev))) && SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&p->vctx)));
        ctx->Release();
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd = {D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE, {60, 1}, (UINT)w, (UINT)h, {60, 1}, (UINT)w, (UINT)h,
                                                 D3D11_VIDEO_USAGE_PLAYBACK_NORMAL};
        ok = ok && SUCCEEDED(p->vdev->CreateVideoProcessorEnumerator(&cd, &p->vpe)) && SUCCEEDED(p->vdev->CreateVideoProcessor(p->vpe, 0, &p->vp));
        for (int i = 0; ok && i < decoder::SURFACES; i++) {
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv = {};
            iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            iv.Texture2D.ArraySlice = (UINT)i;
            ok = SUCCEEDED(p->vdev->CreateVideoProcessorInputView(decoder::texture(p->dec), p->vpe, &iv, &p->inputs[i]));
        }
        IDXGIDevice* dx = nullptr;
        IDXGIAdapter* ad = nullptr;
        IDXGIFactory2* f = nullptr;
        if (ok) {
            DXGI_SWAP_CHAIN_DESC1 sd = {};
            sd.Width = (UINT)w;
            sd.Height = (UINT)h;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.Scaling = DXGI_SCALING_STRETCH;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            ok = SUCCEEDED(d3d->QueryInterface(IID_PPV_ARGS(&dx))) && SUCCEEDED(dx->GetAdapter(&ad)) && SUCCEEDED(ad->GetParent(IID_PPV_ARGS(&f))) &&
                 SUCCEEDED(f->CreateSwapChainForComposition(d3d, &sd, nullptr, &p->swap));
        }
        IUnknown* tmp[] = {f, ad, dx};
        for (IUnknown* u : tmp)
            if (u)
                u->Release();
        if (!ok) {
            err = "cant create the video output";
            close_presenter(p);
            return false;
        }
        p->video_w = p->swap_w = w;
        p->video_h = p->swap_h = h;
        std::string scale_err;
        D3D11_TEXTURE2D_DESC td = {(UINT)w, (UINT)h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {D3D11_VPOV_DIMENSION_TEXTURE2D};
        ID3D11Texture2D* bb = nullptr;
        bool scaled = scaler::init(d3d, scale_err) && SUCCEEDED(d3d->CreateTexture2D(&td, nullptr, &p->rgb)) &&
                      SUCCEEDED(d3d->CreateShaderResourceView(p->rgb, nullptr, &p->rgb_view)) &&
                      SUCCEEDED(p->vdev->CreateVideoProcessorOutputView(p->rgb, p->vpe, &od, &p->rgb_out)) && SUCCEEDED(p->swap->GetBuffer(0, IID_PPV_ARGS(&bb))) &&
                      SUCCEEDED(d3d->CreateRenderTargetView(bb, nullptr, &p->target));
        if (bb)
            bb->Release();
        if (!scaled) {
            dbg::print("[player] no scaler, the compositor scales the video");
            IUnknown* part[] = {p->target, p->rgb_out, p->rgb_view, p->rgb};
            for (IUnknown* u : part)
                if (u)
                    u->Release();
            p->target = nullptr;
            p->rgb_out = nullptr;
            p->rgb_view = nullptr;
            p->rgb = nullptr;
        }
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE in = {};
        in.YCbCr_Matrix = decoder::bt601(p->dec) ? 0 : 1;
        in.Nominal_Range = decoder::full_range(p->dec) ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255 : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        p->vctx->VideoProcessorSetStreamColorSpace(p->vp, 0, &in);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE out = {};
        out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        p->vctx->VideoProcessorSetOutputColorSpace(p->vp, &out);
        RECT r = {0, 0, w, h};
        p->vctx->VideoProcessorSetStreamFrameFormat(p->vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        p->vctx->VideoProcessorSetStreamSourceRect(p->vp, 0, TRUE, &r);
        p->vctx->VideoProcessorSetStreamDestRect(p->vp, 0, TRUE, &r);
        p->vctx->VideoProcessorSetOutputTargetRect(p->vp, TRUE, &r);
        IDXGISwapChain1* swap = p->swap;
        swap->AddRef();
        window::on_ui([swap, w, h] {
            if (IDCompositionVisual* v = compose::video_visual())
                v->SetContent(swap);
            swap->Release();
            g_video_w = w;
            g_video_h = h;
            apply();
        });
        return true;
    }

    bool open_source(play_t* p, const std::wstring& dir, std::string& err) {
        close_source(p);
        p->dir = dir;
        p->track = mp4::track_t{};
        std::vector<uint8_t> init;
        if (!read_file(dir + L"\\init-stream0.m4s", init) || !mp4::init(init.data(), init.size(), p->track)) {
            err = "not a steam recording";
            return false;
        }
        if (p->track.codec != mp4::fourcc("hev1") && p->track.codec != mp4::fourcc("hvc1")) {
            err = "only hevc recordings play here";
            return false;
        }
        p->chunks = chunks::scan(dir, 0);
        if (p->chunks.empty() || !load(p, (int)p->chunks.size() - 1) || p->samples.empty()) {
            err = "the recording has no video";
            return false;
        }
        const mp4::sample_t& tail = p->samples.back();
        p->end = (int64_t)tail.dts + tail.cto + tail.dur;
        if (!load(p, 0) || p->samples.empty()) {
            err = "the recording has no video";
            return false;
        }
        p->start = (int64_t)p->samples[0].dts + p->samples[0].cto;
        if (p->samples[0].dur)
            p->frame = seconds(p, p->samples[0].dur);
        p->dec = decoder::create(compose::d3d(), p->track, err);
        if (!p->dec)
            return false;
        if (!open_presenter(p, err)) {
            decoder::destroy(p->dec);
            p->dec = nullptr;
            return false;
        }
        p->ended = false;
        std::string audio_err;
        bool sound = audio::open(dir, 0, audio_err);
        dbg::print("[player] %s: chunks %d-%d, %.1f-%.1f s%s", text::utf8(dir).c_str(), p->chunks.front().idx, p->chunks.back().idx, seconds(p, p->start), seconds(p, p->end),
                   sound ? "" : ", no audio");
        return true;
    }

    bool feed(play_t* p, std::string& err) {
        while (p->next >= p->samples.size()) {
            if (p->chunk + 1 >= (int)p->chunks.size() || !load(p, p->chunk + 1)) {
                decoder::drain(p->dec);
                p->ended = true;
                return false;
            }
        }
        const mp4::sample_t& s = p->samples[p->next++];
        prefetch(p);
        return decoder::decode(p->dec, p->data.data() + s.offset, s.size, (int64_t)s.dts + s.cto, err);
    }

    void fill(play_t* p) {
        std::string err;
        while (p->ready.size() < AHEAD) {
            decoder::frame_t f;
            if (decoder::pop(p->dec, f)) {
                p->ready.push_back(f);
                continue;
            }
            if (p->ended)
                return;
            if (!feed(p, err) && !err.empty()) {
                dbg::print("[player] %s", err.c_str());
                report(p, "error", err);
                p->ended = true;
                return;
            }
        }
    }

    void present_rgb(play_t* p) {
        ID3D11DeviceContext* ctx = nullptr;
        compose::d3d()->GetImmediateContext(&ctx);
        scaler::draw(ctx, p->rgb_view, p->video_w, p->video_h, p->target, p->swap_w, p->swap_h);
        ctx->Release();
        p->swap->Present(0, 0);
    }

    void resize(play_t* p, int w, int h) {
        if (!p->swap || !p->target || w < 16 || h < 16 || w > 8192 || h > 8192 || (w == p->swap_w && h == p->swap_h))
            return;
        p->target->Release();
        p->target = nullptr;
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(p->swap->ResizeBuffers(2, (UINT)w, (UINT)h, DXGI_FORMAT_B8G8R8A8_UNORM, 0)) || FAILED(p->swap->GetBuffer(0, IID_PPV_ARGS(&bb))) ||
            FAILED(compose::d3d()->CreateRenderTargetView(bb, nullptr, &p->target))) {
            if (bb)
                bb->Release();
            dbg::print("[player] cant resize the video output to %dx%d", w, h);
            return;
        }
        bb->Release();
        p->swap_w = w;
        p->swap_h = h;
        if (p->has_frame)
            present_rgb(p);
        window::on_ui([w, h] {
            g_video_w = w;
            g_video_h = h;
            apply();
        });
    }

    void show(play_t* p, const decoder::frame_t& f) {
        ID3D11Texture2D* bb = nullptr;
        ID3D11VideoProcessorOutputView* ov = nullptr;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {D3D11_VPOV_DIMENSION_TEXTURE2D};
        if (p->rgb) {
            D3D11_VIDEO_PROCESSOR_STREAM st = {};
            st.Enable = TRUE;
            st.pInputSurface = p->inputs[f.surface];
            p->vctx->VideoProcessorBlt(p->vp, p->rgb_out, 0, 1, &st);
            p->has_frame = true;
            present_rgb(p);
        } else if (SUCCEEDED(p->swap->GetBuffer(0, IID_PPV_ARGS(&bb))) && SUCCEEDED(p->vdev->CreateVideoProcessorOutputView(bb, p->vpe, &od, &ov))) {
            D3D11_VIDEO_PROCESSOR_STREAM st = {};
            st.Enable = TRUE;
            st.pInputSurface = p->inputs[f.surface];
            p->vctx->VideoProcessorBlt(p->vp, ov, 0, 1, &st);
            p->swap->Present(0, 0);
        }
        steady_t::time_point t0 = steady_t::now();
        if (p->playing)
            p->gap = std::max(p->gap, std::chrono::duration<double, std::milli>(t0 - p->presented).count());
        p->presented = t0;
        p->frames++;
        if (ov)
            ov->Release();
        if (bb)
            bb->Release();
        decoder::release(p->dec, f.surface);
        p->shown = seconds(p, f.pts);
    }

    double media_now(const play_t* p) {
        double t = 0;
        if (p->playing && p->rate == 1 && audio::clock(t))
            return t;
        return p->base_media + std::chrono::duration<double>(steady_t::now() - p->base_wall).count() * (p->playing ? p->rate : 0);
    }

    void seek_to(play_t* p, double t) {
        drop_frames(p);
        decoder::reset(p->dec);
        double from = seconds(p, p->start), to = seconds(p, p->end);
        t = std::clamp(t, from, std::max(from, to - 0.05));
        int pos = chunks::find(p->chunks, (uint64_t)(t * p->track.timescale));
        while (load(p, pos) && !p->samples.empty() && seconds(p, (int64_t)p->samples[0].dts + p->samples[0].cto) > t && pos > 0)
            pos--;
        p->ended = false;
        std::string err;
        while (true) {
            fill(p);
            if (p->ready.empty())
                break;
            decoder::frame_t f = p->ready.front();
            p->ready.pop_front();
            if (seconds(p, f.pts) + 0.5 * p->frame >= t || (p->ready.empty() && p->ended)) {
                show(p, f);
                break;
            }
            decoder::release(p->dec, f.surface);
        }
        p->base_media = std::max(0.0, p->shown);
        p->base_wall = steady_t::now();
    }

    void tick(play_t* p) {
        fill(p);
        if (p->ready.empty()) {
            if (p->ended) {
                p->playing = false;
                audio::stop();
                report(p, "ended");
            }
            return;
        }
        double now = media_now(p);
        double next = seconds(p, p->ready.front().pts);
        if (next > now + GAP_JUMP) {
            p->base_media = now = next;
            p->base_wall = steady_t::now();
            if (p->rate == 1)
                audio::start(next);
        }
        while (!p->ready.empty()) {
            decoder::frame_t f = p->ready.front();
            double at = seconds(p, f.pts);
            if (at > now + 0.002)
                break;
            p->ready.pop_front();
            if (at < now - 0.1 && !p->ready.empty()) {
                decoder::release(p->dec, f.surface);
                p->dropped++;
                continue;
            }
            show(p, f);
            break;
        }
        if (steady_t::now() - p->reported > REPORT_EVERY)
            report(p, "playing");
        if (steady_t::now() - p->counted > std::chrono::seconds(2)) {
            double a = 0;
            bool sound = audio::clock(a);
            double steady = p->base_media + std::chrono::duration<double>(steady_t::now() - p->base_wall).count();
            dbg::print("[player] %d frames, %d dropped, max gap %.0f ms, ahead %zu, %s clock, drift %.3f s", p->frames, p->dropped, p->gap, p->ready.size(),
                       sound ? "audio" : "steady", sound ? a - steady : 0.0);
            p->frames = p->dropped = 0;
            p->gap = 0;
            p->counted = steady_t::now();
        }
    }

    steady_t::time_point due(play_t* p) {
        if (!p->playing || !p->dec)
            return steady_t::time_point::max();
        if (p->ready.empty())
            return steady_t::now();
        double wait = seconds(p, p->ready.front().pts) - media_now(p);
        steady_t::time_point at = steady_t::now() + std::chrono::microseconds((int64_t)(std::max(0.0, wait) * 1e6));
        return std::min(at, steady_t::now() + MAX_SLEEP);
    }

    void handle(play_t* p, const cmd_t& c) {
        std::string err;
        switch (c.kind) {
        case ck_open:
            if (!open_source(p, c.dir, err)) {
                close_source(p);
                report(p, "error", err);
                break;
            }
            {
                std::unique_lock<std::mutex> lock(g_rect_mu);
                int w = (int)std::lround(g_rect[2]), h = (int)std::lround(g_rect[3]);
                lock.unlock();
                resize(p, w, h);
            }
            seek_to(p, c.t);
            report(p, "paused");
            break;
        case ck_play:
            if (!p->dec)
                break;
            if (p->ended && p->ready.empty())
                seek_to(p, seconds(p, p->start));
            p->playing = true;
            p->base_media = std::max(0.0, p->shown);
            p->base_wall = steady_t::now();
            if (p->rate == 1)
                audio::start(p->base_media);
            report(p, "playing");
            break;
        case ck_pause:
            if (!p->dec)
                break;
            p->playing = false;
            audio::stop();
            report(p, "paused");
            break;
        case ck_seek:
            if (!p->dec)
                break;
            seek_to(p, c.t);
            if (p->playing && p->rate == 1)
                audio::start(p->base_media);
            report(p, p->playing ? "playing" : "paused");
            break;
        case ck_rate:
            p->base_media = p->playing ? std::max(0.0, p->shown) : p->base_media;
            p->base_wall = steady_t::now();
            p->rate = std::clamp(c.t, 0.1, 4.0);
            if (p->playing && p->rate == 1)
                audio::start(p->base_media);
            else
                audio::stop();
            report(p, p->playing ? "playing" : "paused");
            break;
        case ck_step:
            if (!p->dec)
                break;
            p->playing = false;
            audio::stop();
            if (c.t < 0) {
                seek_to(p, p->shown - p->frame);
            } else {
                fill(p);
                if (!p->ready.empty()) {
                    decoder::frame_t f = p->ready.front();
                    p->ready.pop_front();
                    show(p, f);
                }
                p->base_media = std::max(0.0, p->shown);
            }
            report(p, "paused");
            break;
        case ck_resize:
            resize(p, c.w, c.h);
            break;
        case ck_close:
            audio::stop();
            close_source(p);
            report(p, "closed");
            break;
        }
    }

    void run() {
        play_t* p = new play_t();
        while (true) {
            cmd_t c;
            bool has = false;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                steady_t::time_point wake = due(p);
                if (wake == steady_t::time_point::max())
                    g_cv.wait(lock, [] { return g_quit || !g_cmds.empty(); });
                else
                    g_cv.wait_until(lock, wake, [] { return g_quit || !g_cmds.empty(); });
                if (g_quit)
                    break;
                if (!g_cmds.empty()) {
                    c = g_cmds.front();
                    g_cmds.pop_front();
                    has = true;
                }
            }
            if (has)
                handle(p, c);
            else if (p->playing && p->dec)
                tick(p);
        }
        close_source(p);
        delete p;
    }

    void push(const cmd_t& c) {
        if (!compose::d3d())
            return;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_thread.joinable())
                g_thread = std::thread(run);
            if ((c.kind == ck_seek || c.kind == ck_resize) && !g_cmds.empty() && g_cmds.back().kind == c.kind)
                g_cmds.back() = c;
            else
                g_cmds.push_back(c);
        }
        g_cv.notify_all();
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
        }
        g_cv.notify_all();
        if (g_thread.joinable())
            g_thread.join();
        audio::close();
        scaler::cleanup();
    }

    void open(const std::wstring& dir, double t) {
        push({ck_open, dir, t});
    }

    void play() {
        push({ck_play});
    }

    void pause() {
        push({ck_pause});
    }

    void seek(double t) {
        push({ck_seek, L"", t});
    }

    void close() {
        push({ck_close});
    }

    void rate(double v) {
        push({ck_rate, L"", v});
    }

    void step(int dir) {
        push({ck_step, L"", (double)dir});
    }

    void place(double x, double y, double w, double h) {
        {
            std::lock_guard<std::mutex> lock(g_rect_mu);
            g_rect[0] = x;
            g_rect[1] = y;
            g_rect[2] = w;
            g_rect[3] = h;
        }
        apply();
        push({ck_resize, L"", 0, (int)std::lround(w), (int)std::lround(h)});
    }
}
