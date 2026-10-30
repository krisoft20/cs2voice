#include "pch.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlobj.h>
#include <wincodec.h>

#include "bridge/clips.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/window.h"
#include "text.h"

namespace clips {
    const UINT THUMB_W = 480;
    const double THUMB_PARTS[] = {0.3, 0.5, 0.7};
    const int THUMB_READS = 90;

    std::wstring g_dir;
    std::wstring g_meta;
    std::mutex g_mu;
    std::mutex g_meta_mu;
    std::condition_variable g_cv;
    std::deque<std::wstring> g_queue;
    std::set<std::wstring> g_queued;
    std::thread g_worker;
    bool g_quit = false;

    std::wstring dir() {
        if (g_dir.empty()) {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &p)))
                g_dir = std::wstring(p) + L"\\cs2voice";
            CoTaskMemFree(p);
        }
        if (!g_dir.empty())
            CreateDirectoryW(g_dir.c_str(), nullptr);
        return g_dir;
    }

    std::wstring meta_dir() {
        return g_meta;
    }

    bool inside(const std::wstring& path) {
        std::wstring d = dir();
        return !d.empty() && path.size() > d.size() + 1 && _wcsnicmp(path.c_str(), d.c_str(), d.size()) == 0 && path[d.size()] == L'\\' &&
               path.find(L'\\', d.size() + 1) == std::wstring::npos && path.find(L"..") == std::wstring::npos &&
               GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::wstring file_name(const std::string& name) {
        std::wstring out;
        for (wchar_t c : text::wide(name)) {
            if (c < 32 || wcschr(L"<>:\"/\\|?*", c))
                continue;
            out += c;
        }
        while (!out.empty() && (out.back() == L' ' || out.back() == L'.'))
            out.pop_back();
        if (out.empty() || out.size() > 120)
            out = L"clip";
        return out;
    }

    std::wstring stem(const std::wstring& path) {
        size_t slash = path.find_last_of(L'\\');
        std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        return name.size() > 4 ? name.substr(0, name.size() - 4) : name;
    }

    std::wstring thumb_of(const std::wstring& mp4) {
        return g_meta + L"\\" + stem(mp4) + L".jpg";
    }

    std::wstring meta_of(const std::wstring& mp4) {
        return g_meta + L"\\" + stem(mp4) + L".json";
    }

    bool read_text(const std::wstring& path, std::string& out) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;
        char buf[4096];
        size_t n;
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
        return true;
    }

    bool write_text(const std::wstring& path, const std::string& text) {
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (!f)
            return false;
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
        fclose(f);
        return ok;
    }

    FILETIME stamp(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a);
        return a.ftLastWriteTime;
    }

    std::string num(double v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", v);
        return buf;
    }

    struct meta_t {
        std::string match;
        std::string map;
        std::string tag;
        double from = 0;
        double to = 0;
        double len = 0;
        bool fav = false;
    };

    bool read_meta(const std::wstring& mp4, meta_t& m) {
        std::string text;
        json_t j;
        std::string err;
        if (!read_text(meta_of(mp4), text) || !json::parse(text, j, err) || j.kind != jk_obj)
            return false;
        m.match = json::str(json::at(j, "match"));
        m.map = json::str(json::at(j, "map"));
        m.tag = json::str(json::at(j, "tag"));
        m.from = json::num(json::at(j, "from"));
        m.to = json::num(json::at(j, "to"));
        m.len = json::num(json::at(j, "len"));
        const json_t* fav = json::at(j, "fav");
        m.fav = fav && fav->kind == jk_bool && fav->b;
        return true;
    }

    std::string meta_json(const meta_t& m) {
        return "{\"match\":\"" + json::escape(m.match) + "\",\"map\":\"" + json::escape(m.map) + "\",\"tag\":\"" + json::escape(m.tag) + "\",\"from\":" + num(m.from) +
               ",\"to\":" + num(m.to) + ",\"len\":" + num(m.len) + ",\"fav\":" + (m.fav ? "true" : "false") + "}";
    }

    void write_meta(const std::wstring& mp4, const std::string& match, const std::string& map, const std::string& tag, double from, double to) {
        meta_t m;
        m.match = match;
        m.map = map;
        m.tag = tag;
        m.from = from;
        m.to = to;
        m.len = to - from;
        std::lock_guard<std::mutex> lock(g_meta_mu);
        if (!g_meta.empty())
            write_text(meta_of(mp4), meta_json(m));
    }

    bool make_thumb(const std::wstring& mp4, const std::wstring& jpg, double& secs) {
        IMFAttributes* attrs = nullptr;
        IMFSourceReader* reader = nullptr;
        IMFMediaType* type = nullptr;
        IMFMediaType* cur = nullptr;
        IMFSample* sample = nullptr;
        IMFMediaBuffer* buf = nullptr;
        IWICImagingFactory* wic = nullptr;
        IWICBitmap* bmp = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* props = nullptr;
        UINT32 w = 0, h = 0;
        LONG stride = 0;
        bool ok = SUCCEEDED(MFCreateAttributes(&attrs, 1)) && SUCCEEDED(MFCreateSourceReaderFromURL(mp4.c_str(), attrs, &reader)) && SUCCEEDED(MFCreateMediaType(&type));
        if (ok) {
            type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            ok = SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type)) &&
                 SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) && SUCCEEDED(MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h)) &&
                 w > 0 && h > 0;
            if (ok)
                stride = (LONG)MFGetAttributeUINT32(cur, MF_MT_DEFAULT_STRIDE, w);
        }
        if (ok) {
            PROPVARIANT dur;
            PropVariantInit(&dur);
            if (SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &dur)) && dur.vt == VT_UI8)
                secs = dur.uhVal.QuadPart / 1e7;
            PropVariantClear(&dur);
        }
        UINT32 x0 = 0, y0 = 0, cw = w, ch = h;
        MFVideoArea area = {};
        if (cur && SUCCEEDED(cur->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), nullptr)) && area.Area.cx > 0 && area.Area.cy > 0 &&
            area.OffsetX.value + area.Area.cx <= (LONG)w && area.OffsetY.value + area.Area.cy <= (LONG)h) {
            x0 = area.OffsetX.value;
            y0 = area.OffsetY.value;
            cw = area.Area.cx;
            ch = area.Area.cy;
        }
        std::vector<BYTE> pixels, best;
        double best_score = -1;
        bool full = cur && MFGetAttributeUINT32(cur, MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235) == MFNominalRange_0_255;
        bool bt601 = cur && MFGetAttributeUINT32(cur, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709) == MFVideoTransferMatrix_BT601;
        float ys = full ? 1.0f : 255.0f / 219.0f, cs = full ? 1.0f : 255.0f / 224.0f, yo = full ? 0.0f : 16.0f;
        float rv = bt601 ? 1.402f : 1.5748f, gu = bt601 ? 0.344f : 0.1873f, gv = bt601 ? 0.714f : 0.4681f, bu = bt601 ? 1.772f : 1.8556f;
        auto grab = [&](const BYTE* top, LONG step, DWORD len) {
            UINT32 rows = step > 0 ? std::max(h, (UINT32)(len / 3 * 2 / step)) : h;
            pixels.resize((size_t)cw * 4 * ch);
            const BYTE* uv = top + (ptrdiff_t)step * rows;
            double sum = 0, sq = 0;
            size_t n = 0;
            for (UINT32 y = 0; y < ch; y++) {
                const BYTE* row = top + (ptrdiff_t)(y + y0) * step + x0;
                const BYTE* crow = uv + (ptrdiff_t)((y + y0) / 2) * step;
                BYTE* out = &pixels[(size_t)y * cw * 4];
                for (UINT32 x = 0; x < cw; x++) {
                    float l = (row[x] - yo) * ys;
                    float u = (crow[(x + x0) & ~1u] - 128.0f) * cs;
                    float v = (crow[((x + x0) & ~1u) + 1] - 128.0f) * cs;
                    out[x * 4 + 0] = (BYTE)std::clamp(l + bu * u + 0.5f, 0.0f, 255.0f);
                    out[x * 4 + 1] = (BYTE)std::clamp(l - gu * u - gv * v + 0.5f, 0.0f, 255.0f);
                    out[x * 4 + 2] = (BYTE)std::clamp(l + rv * v + 0.5f, 0.0f, 255.0f);
                    out[x * 4 + 3] = 255;
                    if ((x & 3) == 0 && (y & 3) == 0) {
                        sum += l;
                        sq += l * l;
                        n++;
                    }
                }
            }
            double mean = n ? sum / n : 0;
            return n ? sq / n - mean * mean : 0;
        };
        for (double part : THUMB_PARTS) {
            if (!ok)
                break;
            PROPVARIANT at;
            PropVariantInit(&at);
            at.vt = VT_I8;
            at.hVal.QuadPart = (LONGLONG)(secs * part * 1e7);
            reader->SetCurrentPosition(GUID_NULL, at);
            for (int i = 0; !sample && i < THUMB_READS; i++) {
                DWORD flags = 0;
                LONGLONG ts = 0;
                if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
                    break;
            }
            if (!sample)
                continue;
            double score = -1;
            IMF2DBuffer2* b2 = nullptr;
            BYTE* scan0 = nullptr;
            BYTE* start = nullptr;
            DWORD size = 0;
            LONG pitch = 0;
            if (SUCCEEDED(sample->GetBufferByIndex(0, &buf))) {
                if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan0, &pitch, &start, &size))) {
                    score = pitch > 0 ? grab(scan0, pitch, size - (DWORD)(scan0 - start)) : -1;
                    b2->Unlock2D();
                } else {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        if (stride > 0 && len >= (DWORD)stride * h * 3 / 2)
                            score = grab(data, stride, len);
                        buf->Unlock();
                    }
                }
                if (b2)
                    b2->Release();
                buf->Release();
                buf = nullptr;
            }
            sample->Release();
            sample = nullptr;
            if (score > best_score) {
                best_score = score;
                best.swap(pixels);
            }
            if (secs <= 0)
                break;
        }
        ok = ok && !best.empty();
        if (ok) {
            UINT th = std::max(1u, ch * THUMB_W / cw);
            ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                 SUCCEEDED(wic->CreateBitmapFromMemory(cw, ch, GUID_WICPixelFormat32bppBGR, cw * 4, (UINT)best.size(), best.data(), &bmp)) &&
                 SUCCEEDED(wic->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(bmp, THUMB_W, th, WICBitmapInterpolationModeFant)) &&
                 SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(jpg.c_str(), GENERIC_WRITE)) &&
                 SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
                 SUCCEEDED(enc->CreateNewFrame(&frame, &props)) && SUCCEEDED(frame->Initialize(props)) && SUCCEEDED(frame->SetSize(THUMB_W, th));
            WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
            ok = ok && SUCCEEDED(frame->SetPixelFormat(&fmt)) && SUCCEEDED(frame->WriteSource(scaler, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
        }
        IUnknown* all[] = {props, frame, enc, stream, scaler, bmp, wic, buf, sample, cur, type, reader, attrs};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        if (!ok)
            DeleteFileW(jpg.c_str());
        return ok;
    }

    void work() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION);
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        while (true) {
            std::wstring mp4;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || !g_queue.empty(); });
                if (g_quit)
                    break;
                mp4 = g_queue.front();
                g_queue.pop_front();
            }
            auto t0 = std::chrono::steady_clock::now();
            double secs = 0;
            bool ok = make_thumb(mp4, thumb_of(mp4), secs);
            if (ok && secs > 0) {
                std::lock_guard<std::mutex> lock(g_meta_mu);
                meta_t m;
                read_meta(mp4, m);
                if (m.len <= 0) {
                    m.len = secs;
                    write_text(meta_of(mp4), meta_json(m));
                }
            }
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            dbg::print("[clip] thumb %s %s in %.0f ms", text::utf8(stem(mp4)).c_str(), ok ? "made" : "failed", ms);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_queued.erase(mp4);
            }
            if (ok)
                window::post_json("{\"type\":\"clip_thumb\",\"file\":\"" + json::escape(text::utf8(stem(mp4) + L".mp4")) + "\",\"len\":" + num(secs) + "}");
        }
        MFShutdown();
        CoUninitialize();
    }

    void want_thumb(const std::wstring& mp4) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_queued.insert(mp4).second)
                return;
            if (!g_worker.joinable())
                g_worker = std::thread(work);
            g_queue.push_back(mp4);
        }
        g_cv.notify_all();
    }

    void init(const std::wstring& data_dir) {
        g_meta = data_dir + L"\\clips";
        CreateDirectoryW(g_meta.c_str(), nullptr);
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
        }
        g_cv.notify_all();
        if (g_worker.joinable())
            g_worker.join();
    }

    void list() {
        std::wstring d = dir();
        std::vector<std::pair<int64_t, std::string>> rows;
        uint64_t total = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = d.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((d + L"\\*.mp4").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring name = fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || (name.size() > 9 && name.compare(name.size() - 9, 9, L".part.mp4") == 0))
                    continue;
                std::wstring path = d + L"\\" + name;
                ULARGE_INTEGER ft;
                ft.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                ft.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                int64_t unix = (int64_t)(ft.QuadPart / 10000000ull) - 11644473600ll;
                uint64_t bytes = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                total += bytes;
                std::wstring jpg = thumb_of(path);
                FILETIME made = stamp(jpg);
                bool thumb = GetFileAttributesW(jpg.c_str()) != INVALID_FILE_ATTRIBUTES && CompareFileTime(&made, &fd.ftLastWriteTime) >= 0;
                if (!thumb && !g_meta.empty())
                    want_thumb(path);
                meta_t m;
                std::string meta;
                {
                    std::lock_guard<std::mutex> lock(g_meta_mu);
                    meta = read_meta(path, m) ? meta_json(m) : "null";
                }
                rows.push_back({unix, "{\"file\":\"" + json::escape(text::utf8(name)) + "\",\"path\":\"" + json::escape(text::utf8(path)) +
                                          "\",\"bytes\":" + std::to_string(bytes) + ",\"mtime\":" + std::to_string(unix) + ",\"thumb\":" + (thumb ? "true" : "false") +
                                          ",\"meta\":" + meta + "}"});
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string j = "{\"type\":\"clips\",\"dir\":\"" + json::escape(text::utf8(d)) + "\",\"bytes\":" + std::to_string(total) + ",\"items\":[";
        for (size_t i = 0; i < rows.size(); i++)
            j += (i ? "," : "") + rows[i].second;
        window::post_json(j + "]}");
    }

    void remove(const std::vector<std::wstring>& paths) {
        for (const std::wstring& path : paths) {
            if (!inside(path))
                continue;
            if (!DeleteFileW(path.c_str())) {
                dbg::print("[clip] cant delete %s, error %lu", text::utf8(path).c_str(), GetLastError());
                continue;
            }
            DeleteFileW(thumb_of(path).c_str());
            DeleteFileW(meta_of(path).c_str());
        }
        list();
    }

    void rename(const std::wstring& path, const std::string& name) {
        if (!inside(path))
            return;
        std::wstring base = dir() + L"\\" + file_name(name);
        std::wstring out = base + L".mp4";
        if (_wcsicmp(out.c_str(), path.c_str()) != 0)
            for (int n = 2; GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES; n++)
                out = base + L" (" + std::to_wstring(n) + L").mp4";
        if (_wcsicmp(out.c_str(), path.c_str()) != 0 && MoveFileExW(path.c_str(), out.c_str(), 0)) {
            MoveFileExW(thumb_of(path).c_str(), thumb_of(out).c_str(), MOVEFILE_REPLACE_EXISTING);
            MoveFileExW(meta_of(path).c_str(), meta_of(out).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        list();
    }

    void favourite(const std::wstring& path, bool on) {
        if (!inside(path))
            return;
        {
            std::lock_guard<std::mutex> lock(g_meta_mu);
            meta_t m;
            read_meta(path, m);
            m.fav = on;
            write_text(meta_of(path), meta_json(m));
        }
        list();
    }

    void copy(const std::vector<std::wstring>& paths) {
        std::vector<std::wstring> ok;
        for (const std::wstring& p : paths)
            if (inside(p))
                ok.push_back(p);
        if (ok.empty() || !OpenClipboard(window::handle()))
            return;
        EmptyClipboard();
        size_t chars = 1;
        for (const std::wstring& p : ok)
            chars += p.size() + 1;
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t))) {
            DROPFILES* df = (DROPFILES*)GlobalLock(h);
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            wchar_t* at = (wchar_t*)((uint8_t*)df + sizeof(DROPFILES));
            for (const std::wstring& p : ok) {
                memcpy(at, p.c_str(), p.size() * sizeof(wchar_t));
                at += p.size() + 1;
            }
            GlobalUnlock(h);
            if (!SetClipboardData(CF_HDROP, h))
                GlobalFree(h);
        }
        UINT effect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
            *(DWORD*)GlobalLock(h) = DROPEFFECT_COPY;
            GlobalUnlock(h);
            if (!SetClipboardData(effect, h))
                GlobalFree(h);
        }
        CloseClipboard();
    }
}
