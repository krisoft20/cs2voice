#include "pch.h"

#include "bridge/jobs.h"
#include "bridge/voices.h"
#include "dbg.h"
#include "demos/voice.h"
#include "json/json.h"
#include "platform/window.h"

namespace voices {
    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_worker;
    std::atomic<bool> g_quit = false;
    std::atomic<bool> g_cancel = false;
    demo_file_t g_want;
    bool g_pending = false;
    std::string g_current;
    std::string g_ready;
    std::string g_blob;
    std::string g_err;
    std::string g_send;

    void post(const std::string& path, const std::string& blob, const std::string& err) {
        std::string head = "{\"type\":\"voice\",\"path\":\"" + json::escape(path) + "\",\"err\":\"" + json::escape(err) + "\"}";
        if (err.empty())
            window::post_buffer(std::string(blob), head);
        else
            window::post_json(head);
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
            jobs::hold(true);
            bool ok = voice::extract(f.path, blob, err, &g_cancel);
            jobs::hold(false);
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
            dbg::print("[voices] %s in %llu ms, %zu bytes%s%s", f.name.c_str(), (unsigned long long)(GetTickCount64() - t0), g_blob.size(),
                       ok ? "" : ", ", err.c_str());
            if (send)
                post(f.path, g_blob, g_err);
        }
    }

    bool init() {
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
