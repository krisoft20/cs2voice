#include "pch.h"

#include <tlhelp32.h>

#include "bridge/history.h"
#include "bridge/jobs.h"
#include "bridge/perfs.h"
#include "bridge/profiles.h"
#include "dbg.h"
#include "demos/reader.h"
#include "json/json.h"
#include "parser/demo/zstdx.h"
#include "platform/crash.h"
#include "platform/window.h"
#include "text.h"

namespace jobs {
    struct entry_t {
        uint64_t size = 0;
        int64_t mtime = 0;
        std::string json;
        int version = 0;
    };

    const char* CACHE_FORMAT = "cache 5";
    const int DATA_VERSION = 17;

    const int MAX_WORKERS = 8;
    const uint64_t GAME_CHECK_MS = 5000;
    const int UNPACK_THREADS = 3;
    const int GAME_THREADS = 3;

    struct worker_t {
        std::thread thread;
        std::atomic<bool> cancel = false;
        std::string current;
        bool urgent = false;
        bool quick = false;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::deque<demo_file_t> g_quick;
    std::deque<demo_file_t> g_queue;
    std::map<std::string, entry_t> g_cache;
    worker_t g_workers[MAX_WORKERS];
    int g_count = 1;
    std::atomic<bool> g_quit = false;
    std::string g_urgent;
    std::wstring g_cache_path;
    bool g_dirty = false;
    uint64_t g_game_checked = 0;
    bool g_game_running = false;
    int g_hold = 0;

    std::string roster_json(const demo_file_t& f, const roster_t& r, bool ok) {
        std::string j = "{\"type\":\"roster\",\"path\":\"" + json::escape(f.path) + "\",\"ok\":" + (ok ? "true" : "false") +
                        ",\"partial\":" + (r.partial ? "true" : "false") + ",\"ended\":" + (r.ended ? "true" : "false") +
                        ",\"voice_known\":" + (r.voice_known ? "true" : "false") + ",\"pet_crash\":" + (r.pet_crash ? "true" : "false");
        j += ",\"err\":\"" + json::escape(r.err) + "\"";
        j += ",\"map\":\"" + json::escape(r.map) + "\",\"server\":\"" + json::escape(r.server) + "\"";
        j += ",\"voice\":" + std::to_string(r.voice) + ",\"duration\":" + std::to_string(r.duration) +
             ",\"rounds\":" + std::to_string(r.rounds);
        j += ",\"teams\":[";
        for (int i = 0; i < 2; i++) {
            const roster_team_t& t = r.teams[i];
            if (i)
                j += ",";
            j += "{\"name\":\"" + json::escape(t.name) + "\",\"score\":" + std::to_string(t.score) +
                 ",\"side\":" + std::to_string(t.side) + "}";
        }
        j += "],\"players\":[";
        for (size_t i = 0; i < r.players.size(); i++) {
            const roster_player_t& p = r.players[i];
            if (i)
                j += ",";
            j += "{\"slot\":" + std::to_string(p.slot) + ",\"name\":\"" + json::escape(p.name) + "\",\"crosshair\":\"" + json::escape(p.crosshair) + "\",\"steamid\":\"" +
                 std::to_string(p.steamid) + "\",\"team\":" + std::to_string(p.team) + ",\"voice\":" + std::to_string(p.voice) + ",\"mute\":" + std::to_string(p.mute);
            const player_stats_t& st = p.stats;
            j += ",\"stats\":{\"kills\":" + std::to_string(st.kills) + ",\"deaths\":" + std::to_string(st.deaths) +
                 ",\"assists\":" + std::to_string(st.assists) + ",\"damage\":" + std::to_string(st.damage) +
                 ",\"hs_kills\":" + std::to_string(st.hs_kills) + ",\"wallbang_kills\":" + std::to_string(st.wallbang_kills) +
                 ",\"smoke_kills\":" + std::to_string(st.smoke_kills) + ",\"rounds\":" + std::to_string(st.rounds) +
                 ",\"kast\":" + std::to_string(st.kast) + ",\"shots\":" + std::to_string(st.shots) +
                 ",\"hits\":" + std::to_string(st.hits) + ",\"head_hits\":" + std::to_string(st.head_hits) +
                 ",\"entry_kills\":" + std::to_string(st.entry_kills) + ",\"entry_deaths\":" + std::to_string(st.entry_deaths) +
                 ",\"trade_kills\":" + std::to_string(st.trade_kills) + ",\"clutch_rounds\":" + std::to_string(st.clutch_rounds) +
                 ",\"clutches\":" + std::to_string(st.clutches) + ",\"k2\":" + std::to_string(st.multi[2]) +
                 ",\"k3\":" + std::to_string(st.multi[3]) + ",\"k4\":" + std::to_string(st.multi[4]) + ",\"k5\":" + std::to_string(st.multi[5]) +
                 ",\"utility_damage\":" + std::to_string(st.utility_damage) + ",\"enemies_flashed\":" + std::to_string(st.enemies_flashed) +
                 ",\"flash_assists\":" + std::to_string(st.flash_assists) +
                 ",\"ping\":" + std::to_string(st.ping_n ? (st.ping_sum + st.ping_n / 2) / st.ping_n : 0) +
                 ",\"flashes\":" + std::to_string(st.flashes) + ",\"enemy_blinds\":" + std::to_string(st.enemy_blinds) +
                 ",\"enemy_blind_ms\":" + std::to_string((long long)(st.enemy_blind * 1000 + 0.5)) + ",\"team_blind_ms\":" + std::to_string((long long)(st.team_blind * 1000 + 0.5)) + ",\"lines\":[";
            for (size_t k = 0; k < st.lines.size(); k++) {
                const round_line_t& l = st.lines[k];
                j += std::string(k ? "," : "") + "[" + (l.in ? "1" : "0") + "," + std::to_string(l.kills) + "," + (l.died ? "1" : "0") + "," +
                     std::to_string(l.damage) + "," + std::to_string(l.side) + "]";
            }
            j += "],\"eco\":[";
            for (size_t k = 0; k < st.eco.size(); k++) {
                const round_eco_t& e = st.eco[k];
                j += std::string(k ? "," : "") + "[" + std::to_string(e.equipment) + "," + std::to_string(e.saved) + "," + std::to_string(e.earned) + "," +
                     std::to_string(e.live) + "," + std::to_string(e.kills) + "]";
            }
            j += "]}";
            j += ",\"music\":" + std::to_string(p.music) + ",\"skins\":[";
            for (size_t k = 0; k < p.skins.size(); k++) {
                const skin_t& sk = p.skins[k];
                char wear[32];
                snprintf(wear, sizeof(wear), "%.6f", sk.wear);
                j += std::string(k ? "," : "") + "{\"def\":" + std::to_string(sk.def) + ",\"paint\":" + std::to_string(sk.paint) +
                     ",\"seed\":" + std::to_string(sk.seed) + ",\"wear\":" + wear + ",\"st\":" + std::to_string(sk.stattrak) +
                     ",\"gloves\":" + (sk.gloves ? "true" : "false") + ",\"sides\":" + std::to_string(sk.sides) + ",\"tag\":\"" + json::escape(sk.tag) + "\",\"stickers\":[";
                bool first = true;
                for (uint32_t id : sk.stickers) {
                    if (!id)
                        continue;
                    j += std::string(first ? "" : ",") + std::to_string(id);
                    first = false;
                }
                j += "]}";
            }
            j += "]}";
        }
        j += "],\"winners\":[";
        for (size_t k = 0; k < r.winners.size(); k++)
            j += std::string(k ? "," : "") + std::to_string(r.winners[k]);
        j += "],\"round_info\":[";
        for (size_t k = 0; k < r.rounds_info.size(); k++) {
            const round_info_t& ri = r.rounds_info[k];
            j += std::string(k ? "," : "") + "{\"start\":" + std::to_string(ri.start) + ",\"decided\":" + std::to_string(ri.decided) +
                 ",\"reason\":" + std::to_string(ri.reason) + ",\"kills\":[";
            for (size_t n = 0; n < ri.kills.size(); n++) {
                const kill_t& kl = ri.kills[n];
                j += std::string(n ? "," : "") + "[" + std::to_string(kl.tick) + "," + std::to_string(kl.killer) + "," + std::to_string(kl.victim) + "," +
                     std::to_string(kl.assister) + "," + std::to_string(kl.flags) + ",\"" + json::escape(kl.weapon) + "\"]";
            }
            j += "],\"bomb\":[";
            for (size_t n = 0; n < ri.bomb.size(); n++)
                j += std::string(n ? "," : "") + "[" + std::to_string(ri.bomb[n].tick) + "," + std::to_string(ri.bomb[n].slot) + "," + std::to_string(ri.bomb[n].kind) + "]";
            j += "]}";
        }
        j += "],\"tick_interval\":" + std::to_string(r.tick_interval) + "}";
        return j;
    }

    void load() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"rb");
        if (!f)
            return;
        std::string data;
        char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            data.append(buf, n);
        fclose(f);
        size_t at = data.find('\n');
        if (at == std::string::npos)
            return;
        bool versioned = data.compare(0, at, CACHE_FORMAT) == 0;
        int old_version = data[0] == 'v' ? atoi(data.c_str() + 1) : 0;
        at++;
        int stale = 0;
        while (at < data.size()) {
            size_t end = data.find('\n', at);
            if (end == std::string::npos)
                end = data.size();
            std::string line = data.substr(at, end - at);
            at = end + 1;
            entry_t e;
            e.version = old_version;
            if (versioned) {
                size_t v = line.find('\t');
                if (v == std::string::npos)
                    continue;
                e.version = atoi(line.substr(0, v).c_str());
                line = line.substr(v + 1);
            }
            size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1),
                   c = b == std::string::npos ? b : line.find('\t', b + 1);
            if (c == std::string::npos || line.compare(c + 1, 16, "{\"type\":\"roster\"") != 0)
                continue;
            e.size = strtoull(line.substr(0, a).c_str(), nullptr, 10);
            e.mtime = strtoll(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
            e.json = line.substr(c + 1);
            stale += e.version != DATA_VERSION;
            g_cache[line.substr(b + 1, c - b - 1)] = e;
        }
        dbg::print("[jobs] %zu cached demos, %d stale", g_cache.size(), stale);
    }

    void save() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"wb");
        if (!f)
            return;
        fprintf(f, "%s\n", CACHE_FORMAT);
        for (const auto& [path, e] : g_cache)
            fprintf(f, "%d\t%llu\t%lld\t%s\t%s\n", e.version, (unsigned long long)e.size, (long long)e.mtime, path.c_str(), e.json.c_str());
        fclose(f);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(g_mu);
        dbg::print("[jobs] cache cleared, %zu demos dropped", g_cache.size());
        g_cache.clear();
        save();
        g_dirty = false;
    }

    bool present(const std::string& path) {
        return GetFileAttributesW(text::wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    void forget(const std::vector<std::string>& paths) {
        std::lock_guard<std::mutex> lock(g_mu);
        size_t n = 0;
        for (const std::string& p : paths)
            n += g_cache.erase(p);
        if (n)
            save();
    }

    void on_early(const roster_t& r, void* user) {
        const demo_file_t& f = *(const demo_file_t*)user;
        dbg::print("[jobs] %s teams known", f.name.c_str());
        window::post_json(roster_json(f, r, true));
    }

    bool game_running() {
        uint64_t t = GetTickCount64();
        if (g_game_checked && t - g_game_checked < GAME_CHECK_MS)
            return g_game_running;
        bool first = !g_game_checked;
        g_game_checked = t;
        bool found = false;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
                found = _wcsicmp(pe.szExeFile, L"cs2.exe") == 0;
            CloseHandle(snap);
        }
        if (found != g_game_running || first) {
            dbg::print("[jobs] cs2 %s", found ? "running, reading demos on idle threads" : "closed");
            zstdx::set_threads(found ? 1 : UNPACK_THREADS);
            perfs::set_threads(found ? GAME_THREADS : 0, found);
        }
        g_game_running = found;
        return found;
    }

    bool is_full(const entry_t& e) {
        return e.json.find("\"partial\":false") != std::string::npos;
    }

    bool fresh(const demo_file_t& f, bool need_full) {
        auto it = g_cache.find(f.path);
        return it != g_cache.end() && it->second.size == f.size && it->second.mtime == f.mtime &&
               it->second.version == DATA_VERSION &&
               (is_full(it->second) || (!need_full && it->second.json.find("\"voice_known\":true") != std::string::npos));
    }

    bool pick(int w, demo_file_t& f, bool& quick, bool& urgent) {
        urgent = !g_queue.empty() && g_queue.front().path == g_urgent;
        bool busy = g_hold > 0;
        for (int i = 0; i < g_count; i++)
            busy = busy || g_workers[i].urgent;
        if (!urgent && busy)
            return false;
        quick = !urgent && !g_quick.empty();
        if (quick) {
            f = g_quick.front();
            g_quick.pop_front();
            return true;
        }
        if (g_queue.empty() || (!urgent && w > 0))
            return false;
        game_running();
        f = g_queue.front();
        g_queue.pop_front();
        return true;
    }

    void finish(worker_t& me) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            me.current.clear();
            me.urgent = false;
            me.quick = false;
        }
        g_cv.notify_all();
    }

    void work(int w) {
        worker_t& me = g_workers[w];
        while (true) {
            demo_file_t f;
            bool quick = false, urgent = false;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                while (!g_quit && !pick(w, f, quick, urgent))
                    g_cv.wait_for(lock, std::chrono::milliseconds(GAME_CHECK_MS));
                if (g_quit)
                    return;
                if (fresh(f, !quick))
                    continue;
                me.current = f.path;
                me.urgent = urgent;
                me.quick = quick;
                me.cancel = false;
            }
            SetThreadPriority(GetCurrentThread(), urgent ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_BELOW_NORMAL);
            crash::set_task("reading " + f.path);
            roster_t r;
            if (quick) {
                uint64_t t0 = GetTickCount64();
                bool ok = reader::peek(f.path, r, &g_quit);
                finish(me);
                if (g_quit)
                    return;
                if (!ok || r.players.empty())
                    continue;
                r.partial = true;
                std::string j = roster_json(f, r, true);
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    if (fresh(f, true) || !present(f.path))
                        continue;
                    g_cache[f.path] = entry_t{f.size, f.mtime, j, DATA_VERSION};
                    g_dirty = true;
                    if (g_quick.empty()) {
                        save();
                        g_dirty = false;
                    }
                }
                dbg::print("[jobs] %s %s in %llu ms", f.name.c_str(), r.ended ? "score" : "teams", (unsigned long long)(GetTickCount64() - t0));
                window::post_json(j);
                continue;
            }
            uint64_t t0 = GetTickCount64();
            bool ok = reader::read(f.path, r, urgent ? on_early : nullptr, &f, urgent ? &g_quit : &me.cancel);
            if (g_quit)
                return;
            if (!urgent && me.cancel) {
                bool opened;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    opened = me.urgent;
                    me.current.clear();
                    me.urgent = false;
                    if (opened)
                        g_queue.push_front(f);
                    else
                        g_queue.push_back(f);
                }
                g_cv.notify_all();
                dbg::print("[jobs] %s %s", f.name.c_str(), opened ? "restarted as the opened demo" : "paused for the opened demo");
                continue;
            }
            dbg::print("[jobs] %s read in %llu ms", f.name.c_str(), (unsigned long long)(GetTickCount64() - t0));
            std::string j = roster_json(f, r, ok);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (present(f.path)) {
                    g_cache[f.path] = entry_t{f.size, f.mtime, j, DATA_VERSION};
                    save();
                    g_dirty = false;
                }
            }
            finish(me);
            window::post_json(j);
            history::note(j, f.mtime);
            if (!urgent) {
                std::vector<std::string> ids;
                for (const roster_player_t& p : r.players)
                    ids.push_back(std::to_string(p.steamid));
                profiles::want(ids, false);
            }
        }
    }

    bool init(const std::wstring& cache_path) {
        g_cache_path = cache_path;
        load();
        g_count = std::clamp((int)std::thread::hardware_concurrency() / 4, 1, MAX_WORKERS);
        dbg::print("[jobs] %d workers", g_count);
        game_running();
        for (int i = 0; i < g_count; i++) {
            g_workers[i].thread = std::thread(work, i);
            SetThreadPriority(g_workers[i].thread.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
        }
        return true;
    }

    void seed_history() {
        std::vector<std::pair<std::string, int64_t>> full;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (const auto& [path, e] : g_cache)
                if (is_full(e))
                    full.push_back({e.json, e.mtime});
        }
        for (const auto& [j, mtime] : full)
            history::note(j, mtime, true);
        dbg::print("[jobs] history seeded from %zu cached demos", full.size());
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
            for (worker_t& w : g_workers)
                w.cancel = true;
            if (g_dirty)
                save();
        }
        g_cv.notify_all();
        for (worker_t& w : g_workers)
            if (w.thread.joinable())
                w.thread.join();
    }

    void want(const demo_file_t& f, bool urgent, bool quick) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto queued = [&](std::deque<demo_file_t>& q) {
                for (auto it = q.begin(); it != q.end(); ++it)
                    if (it->path == f.path)
                        return it;
                return q.end();
            };
            if (!urgent) {
                if (quick) {
                    if (queued(g_quick) == g_quick.end())
                        g_quick.push_back(f);
                } else if (queued(g_queue) == g_queue.end()) {
                    g_queue.push_back(f);
                }
            } else {
                auto it = queued(g_queue);
                if (it != g_queue.end())
                    g_queue.erase(it);
                it = queued(g_quick);
                if (it != g_quick.end())
                    g_quick.erase(it);
                g_urgent = f.path;
                bool reading = false, idle = false;
                for (int i = 0; i < g_count; i++) {
                    if (g_workers[i].current == f.path && !g_workers[i].quick) {
                        reading = true;
                        g_workers[i].urgent = true;
                        SetThreadPriority(g_workers[i].thread.native_handle(), THREAD_PRIORITY_NORMAL);
                    }
                    idle = idle || g_workers[i].current.empty();
                }
                if (!reading)
                    g_queue.push_front(f);
                for (int i = 0; i < g_count && !reading && !idle; i++) {
                    worker_t& w = g_workers[i];
                    if (!w.urgent && !w.quick && !w.current.empty()) {
                        w.cancel = true;
                        break;
                    }
                }
            }
        }
        g_cv.notify_all();
    }

    bool idle() {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_quick.empty() || !g_queue.empty() || g_hold > 0)
            return false;
        for (int i = 0; i < g_count; i++)
            if (!g_workers[i].current.empty())
                return false;
        return !game_running();
    }

    void hold(bool on) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_hold += on ? 1 : -1;
            if (on)
                for (int i = 0; i < g_count; i++)
                    if (!g_workers[i].urgent && !g_workers[i].quick && !g_workers[i].current.empty())
                        g_workers[i].cancel = true;
        }
        g_cv.notify_all();
    }

    bool cached(const demo_file_t& f, std::string& json, bool& stale, bool* full) {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_cache.find(f.path);
        if (it == g_cache.end() || it->second.size != f.size || it->second.mtime != f.mtime)
            return false;
        stale = it->second.version != DATA_VERSION;
        if (full)
            *full = is_full(it->second);
        json = stale ? "{\"stale\":true," + it->second.json.substr(1) : it->second.json;
        return true;
    }

    bool talkers(const demo_file_t& f, std::vector<talk::talker_t>& out) {
        std::string info, err;
        bool stale = false;
        json_t j;
        if (!cached(f, info, stale) || stale || !json::parse(info, j, err))
            return false;
        roster_t r;
        if (const json_t* rounds = json::at(j, "round_info"); rounds && rounds->kind == jk_arr)
            for (const json_t& x : rounds->arr) {
                round_info_t ri;
                ri.start = (int)json::num(json::at(x, "start"));
                r.rounds_info.push_back(ri);
            }
        const json_t* players = json::at(j, "players");
        if (!players || players->kind != jk_arr)
            return false;
        for (const json_t& x : players->arr) {
            roster_player_t p;
            p.slot = (int)json::num(json::at(x, "slot"), -1);
            p.name = json::str(json::at(x, "name"));
            p.steamid = _strtoui64(json::str(json::at(x, "steamid")).c_str(), nullptr, 10);
            p.team = (int)json::num(json::at(x, "team"), -1);
            const json_t* st = json::at(x, "stats");
            const json_t* lines = st ? json::at(*st, "lines") : nullptr;
            if (lines && lines->kind == jk_arr)
                for (const json_t& l : lines->arr) {
                    round_line_t line;
                    if (l.kind == jk_arr && l.arr.size() >= 5) {
                        line.in = l.arr[0].num != 0;
                        line.side = (int)l.arr[4].num;
                    }
                    p.stats.lines.push_back(line);
                }
            r.players.push_back(p);
        }
        out = talk::talkers(r);
        return !out.empty();
    }
}
