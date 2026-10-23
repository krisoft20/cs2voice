#include "pch.h"

#include "bridge/jobs.h"
#include "bridge/tracks.h"
#include "dbg.h"
#include "demos/track.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"

namespace tracks {
    const uint32_t FILE_MAGIC = 0x336b7274;

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_worker;
    std::atomic<bool> g_quit = false;
    std::atomic<bool> g_cancel = false;
    demo_file_t g_want;
    bool g_pending = false;
    std::string g_current;
    std::string g_coming;
    std::string g_ready;
    std::string g_blob;
    std::string g_err;
    std::string g_send;
    std::wstring g_dir;

    void post(const std::string& path, const std::string& blob, const std::string& err) {
        std::string head = "{\"type\":\"track\",\"path\":\"" + json::escape(path) + "\",\"err\":\"" + json::escape(err) + "\"}";
        if (err.empty())
            window::post_buffer(std::string(blob), head);
        else
            window::post_json(head);
    }

    std::wstring file_of(const std::string& path) {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : path) {
            h ^= c;
            h *= 1099511628211ull;
        }
        wchar_t name[32];
        swprintf(name, 32, L"\\%016llx.bin", (unsigned long long)h);
        return g_dir + name;
    }

    bool load(const demo_file_t& f, std::string* blob) {
        FILE* fp = _wfopen(file_of(f.path).c_str(), L"rb");
        if (!fp)
            return false;
        uint32_t magic = 0, path_len = 0, len = 0;
        uint64_t size = 0;
        int64_t mtime = 0;
        bool ok = fread(&magic, 4, 1, fp) == 1 && fread(&size, 8, 1, fp) == 1 && fread(&mtime, 8, 1, fp) == 1 && fread(&path_len, 4, 1, fp) == 1 &&
                  magic == FILE_MAGIC && size == f.size && mtime == f.mtime && path_len == f.path.size();
        std::string path(ok ? path_len : 0, '\0');
        ok = ok && fread(path.data(), 1, path_len, fp) == path_len && path == f.path && fread(&len, 4, 1, fp) == 1;
        if (ok) {
            long at = ftell(fp);
            _fseeki64(fp, 0, SEEK_END);
            ok = _ftelli64(fp) == (long long)at + len;
            if (ok && blob) {
                blob->resize(len);
                _fseeki64(fp, at, SEEK_SET);
                ok = fread(blob->data(), 1, len, fp) == len;
            }
        }
        fclose(fp);
        return ok;
    }

    void store(const demo_file_t& f, const std::string& blob) {
        std::wstring dst = file_of(f.path), tmp = dst + L".tmp";
        FILE* fp = _wfopen(tmp.c_str(), L"wb");
        if (!fp)
            return;
        uint32_t path_len = (uint32_t)f.path.size(), len = (uint32_t)blob.size();
        bool ok = fwrite(&FILE_MAGIC, 4, 1, fp) == 1 && fwrite(&f.size, 8, 1, fp) == 1 && fwrite(&f.mtime, 8, 1, fp) == 1 &&
                  fwrite(&path_len, 4, 1, fp) == 1 && fwrite(f.path.data(), 1, path_len, fp) == path_len && fwrite(&len, 4, 1, fp) == 1 &&
                  fwrite(blob.data(), 1, len, fp) == len;
        fclose(fp);
        if (ok)
            MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
        else
            DeleteFileW(tmp.c_str());
    }

    void work() {
        while (true) {
            demo_file_t f;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || g_pending; });
                if (g_quit)
                    return;
                f = g_want;
                g_pending = false;
                g_current = f.path;
                g_cancel = false;
            }
            std::string blob, err;
            uint64_t t0 = GetTickCount64();
            bool ok = load(f, &blob);
            bool disk = ok;
            if (!ok) {
                jobs::wait_urgent(g_quit);
                jobs::hold(true);
                ok = track::extract(f.path, blob, err, &g_cancel);
                jobs::hold(false);
            }
            if (g_quit)
                return;
            bool send;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_current.clear();
                if (g_cancel)
                    continue;
                g_ready = f.path;
                g_blob = ok ? std::move(blob) : std::string();
                g_err = ok ? std::string() : (err.empty() ? "unreadable" : err);
                send = g_send == f.path;
            }
            if (ok && !disk)
                store(f, g_blob);
            dbg::print("[tracks] %s %s in %llu ms, %zu bytes%s%s", f.name.c_str(), disk ? "loaded" : "read", (unsigned long long)(GetTickCount64() - t0),
                       g_blob.size(), ok ? "" : ", ", err.c_str());
            if (send)
                post(f.path, g_blob, g_err);
        }
    }

    bool init(const std::wstring& dir) {
        g_dir = dir;
        CreateDirectoryW(dir.c_str(), nullptr);
        g_worker = std::thread(work);
        SetThreadPriority(g_worker.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
        return true;
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
            g_cancel = true;
        }
        g_cv.notify_all();
        if (g_worker.joinable())
            g_worker.join();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(g_mu);
        g_ready.clear();
        g_blob.clear();
        files::wipe(g_dir, L"*.bin");
    }

    void forget(const std::vector<std::string>& paths) {
        std::lock_guard<std::mutex> lock(g_mu);
        for (const std::string& p : paths) {
            DeleteFileW(file_of(p).c_str());
            if (g_ready == p) {
                g_ready.clear();
                g_blob.clear();
            }
        }
    }

    bool stored(const demo_file_t& f) {
        std::lock_guard<std::mutex> lock(g_mu);
        return g_ready == f.path || load(f, nullptr);
    }

    void expect(const demo_file_t& f) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_coming = f.path;
        if (g_pending && g_want.path == f.path)
            g_pending = false;
    }

    void deliver(const demo_file_t& f, const std::string& blob, bool ok, bool open) {
        if (ok && GetFileAttributesW(text::wide(f.path).c_str()) != INVALID_FILE_ATTRIBUTES)
            store(f, blob);
        bool send = false, again = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_coming == f.path)
                g_coming.clear();
            if (ok && open) {
                g_ready = f.path;
                g_blob = blob;
                g_err.clear();
                send = g_send == f.path;
            } else if (!ok && g_send == f.path && g_current != f.path) {
                g_want = f;
                g_pending = again = true;
            }
        }
        dbg::print("[tracks] %s from the perf pass, %zu bytes%s", f.name.c_str(), blob.size(), ok ? "" : ", cancelled");
        if (send)
            post(f.path, blob, std::string());
        if (again)
            g_cv.notify_all();
    }

    void want(const demo_file_t& f, bool send) {
        {
            std::unique_lock<std::mutex> lock(g_mu);
            if (send)
                g_send = f.path;
            if (g_ready == f.path) {
                if (!send)
                    return;
                std::string blob = g_blob, err = g_err;
                lock.unlock();
                post(f.path, blob, err);
                return;
            }
            if (g_coming == f.path)
                return;
            if (g_current == f.path && !g_cancel)
                return;
            if (!g_current.empty())
                g_cancel = true;
            g_want = f;
            g_pending = true;
        }
        g_cv.notify_all();
    }
}
