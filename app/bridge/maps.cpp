#include "pch.h"

#include "bridge/maps.h"
#include "dbg.h"
#include "demos/finder.h"
#include "game/art.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"

namespace maps {
    std::mutex g_mu;
    std::string g_dir;
    std::string g_game;
    std::map<std::string, bool> g_known;
    std::set<std::string> g_pending;
    std::deque<std::function<void()>> g_jobs;
    std::condition_variable g_cv;
    std::thread g_worker;
    bool g_quit = false;

    void work() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || !g_jobs.empty(); });
                if (g_quit)
                    return;
                job = std::move(g_jobs.front());
                g_jobs.pop_front();
            }
            job();
        }
    }

    void later(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_worker.joinable())
                g_worker = std::thread(work);
            g_jobs.push_back(std::move(job));
        }
        g_cv.notify_all();
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

    bool init(const std::wstring& dir) {
        CreateDirectoryW(dir.c_str(), nullptr);
        g_dir = text::utf8(dir);
        g_game = finder::game_dir();
        dbg::print("[maps] game %s", g_game.empty() ? "not found" : g_game.c_str());
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(g_mu);
        g_known.clear();
        files::wipe(text::wide(g_dir), L"*.png");
    }

    void ensure(const std::string& map) {
        if (map.empty())
            return;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto it = g_known.find(map);
            if (it != g_known.end()) {
                if (it->second)
                    window::post_json("{\"type\":\"art\",\"map\":\"" + json::escape(map) + "\"}");
                return;
            }
            if (!g_pending.insert(map).second)
                return;
        }
        later([map] {
            std::string png = g_dir + "\\" + map + ".png";
            bool have = GetFileAttributesW(text::wide(png).c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!have && !g_game.empty()) {
                std::string err;
                uint64_t t0 = GetTickCount64();
                have = art::map_image(g_game, map, png, err);
                if (!have)
                    dbg::print("[maps] %s: %s", map.c_str(), err.c_str());
                else
                    dbg::print("[maps] %s extracted in %llu ms", map.c_str(), (unsigned long long)(GetTickCount64() - t0));
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_known[map] = have;
                g_pending.erase(map);
            }
            if (have)
                window::post_json("{\"type\":\"art\",\"map\":\"" + json::escape(map) + "\"}");
        });
    }

    bool radar_png(const std::string& name) {
        std::string png = g_dir + "\\" + name + "_radar.png";
        if (GetFileAttributesW(text::wide(png).c_str()) != INVALID_FILE_ATTRIBUTES)
            return true;
        if (g_game.empty())
            return false;
        std::string err;
        uint64_t t0 = GetTickCount64();
        if (!art::radar_image(g_game, name, png, err)) {
            dbg::print("[maps] radar %s: %s", name.c_str(), err.c_str());
            return false;
        }
        dbg::print("[maps] radar %s extracted in %llu ms", name.c_str(), (unsigned long long)(GetTickCount64() - t0));
        return true;
    }

    void radar_now(const std::string& map);

    void radar(const std::string& map) {
        if (map.empty() || g_game.empty())
            return;
        later([map] { radar_now(map); });
    }

    void radar_now(const std::string& map) {
        overview_t o;
        std::string err;
        bool ok = art::overview(g_game, map, o, err) && radar_png(map);
        bool lower = ok && o.lower && radar_png(map + "_lower");
        if (!ok) {
            dbg::print("[maps] no radar for %s: %s", map.c_str(), err.c_str());
            window::post_json("{\"type\":\"radar\",\"map\":\"" + json::escape(map) + "\",\"ok\":false}");
            return;
        }
        char buf[256];
        snprintf(buf, sizeof(buf), ",\"pos_x\":%.2f,\"pos_y\":%.2f,\"scale\":%.4f,\"lower\":%s,\"split_z\":%.1f}", o.pos_x, o.pos_y, o.scale,
                 lower ? "true" : "false", o.split_z);
        window::post_json("{\"type\":\"radar\",\"map\":\"" + json::escape(map) + "\",\"ok\":true" + buf);
    }
}
