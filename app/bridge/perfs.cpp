#include "pch.h"

#include "bridge/jobs.h"
#include "bridge/perfs.h"
#include "bridge/tracks.h"
#include "dbg.h"
#include "demos/finder.h"
#include "demos/track.h"
#include "game/weapons.h"
#include "geo/map.h"
#include "geo/vpk.h"
#include "json/json.h"
#include "model/match.h"
#include "model/vis.h"
#include "parser/parser.h"
#include "perf/input.h"
#include "perf/perf.h"
#include "platform/window.h"
#include "text.h"

namespace perfs {
    const char* CACHE_FORMAT = "perfs 1";
    const int DATA_VERSION = 5;

    struct entry_t {
        int version = 0;
        uint64_t size = 0;
        int64_t mtime = 0;
        std::string map;
        std::string geo;
        std::string json;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_worker;
    std::atomic<bool> g_quit = false;
    demo_file_t g_want;
    std::string g_want_map;
    bool g_want_track = false;
    bool g_pending = false;
    std::atomic<bool> g_cancel = false;
    std::atomic<int> g_threads = 0;
    std::atomic<bool> g_idle = false;
    std::deque<demo_file_t> g_back;
    bool g_current_open = false;
    std::string g_current;
    std::map<std::string, entry_t> g_done;
    std::map<std::string, map_geo_t*> g_maps;
    std::vector<std::string> g_recent;
    std::wstring g_cache_path;
    std::string g_maps_dir;
    bool g_maps_looked = false;

    std::string geo_stamp(const std::string& map) {
        if (!g_maps_looked) {
            g_maps_dir = vpk::maps_dir();
            g_maps_looked = true;
        }
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (g_maps_dir.empty() || map.empty() || !GetFileAttributesExW(text::wide(g_maps_dir + "/" + map + ".vpk").c_str(), GetFileExInfoStandard, &a))
            return "-";
        uint64_t size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        uint64_t when = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        return std::to_string(size) + ":" + std::to_string(when);
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
        if (at == std::string::npos || data.compare(0, at, CACHE_FORMAT) != 0)
            return;
        at++;
        while (at < data.size()) {
            size_t end = data.find('\n', at);
            if (end == std::string::npos)
                end = data.size();
            std::string line = data.substr(at, end - at);
            at = end + 1;
            std::vector<std::string> parts;
            size_t from = 0;
            for (int k = 0; k < 6; k++) {
                size_t tab = line.find('\t', from);
                if (tab == std::string::npos)
                    break;
                parts.push_back(line.substr(from, tab - from));
                from = tab + 1;
            }
            if (parts.size() != 6 || line.compare(from, 14, "{\"type\":\"perf\"") != 0)
                continue;
            entry_t e;
            e.version = atoi(parts[0].c_str());
            e.size = strtoull(parts[1].c_str(), nullptr, 10);
            e.mtime = strtoll(parts[2].c_str(), nullptr, 10);
            e.map = parts[3];
            e.geo = parts[4];
            e.json = line.substr(from);
            g_done[parts[5]] = e;
        }
        dbg::print("[perfs] %zu cached demos", g_done.size());
    }

    void save() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"wb");
        if (!f)
            return;
        fprintf(f, "%s\n", CACHE_FORMAT);
        for (const auto& [path, e] : g_done)
            fprintf(f, "%d\t%llu\t%lld\t%s\t%s\t%s\t%s\n", e.version, (unsigned long long)e.size, (long long)e.mtime, e.map.c_str(), e.geo.c_str(),
                    path.c_str(), e.json.c_str());
        fclose(f);
    }

    const map_geo_t* geometry(const std::string& name, std::string& err) {
        auto it = g_maps.find(name);
        if (it != g_maps.end())
            return it->second;
        while (g_recent.size() >= 2) {
            delete g_maps[g_recent.front()];
            g_maps.erase(g_recent.front());
            g_recent.erase(g_recent.begin());
        }
        g_recent.push_back(name);
        map_geo_t* g = new map_geo_t();
        uint64_t t0 = GetTickCount64();
        if (!geo::load_map(*g, name, err)) {
            delete g;
            g = nullptr;
        } else {
            dbg::print("[perfs] %s geometry in %llu ms, %zu triangles", name.c_str(), (unsigned long long)(GetTickCount64() - t0), g->faces.size());
        }
        g_maps[name] = g;
        return g;
    }

    bool fresh(const demo_file_t& f) {
        auto it = g_done.find(f.path);
        if (it == g_done.end())
            return false;
        const entry_t& e = it->second;
        return e.version == DATA_VERSION && e.size == f.size && e.mtime == f.mtime && e.geo == geo_stamp(e.map);
    }

    bool now_open() {
        std::lock_guard<std::mutex> lock(g_mu);
        return g_current_open;
    }

    std::string measure(const demo_file_t& f, const std::string& hint, bool with_track, std::string& map, bool& ok) {
        std::string err, geo_err;
        match_t* m = new match_t();
        std::string out;
        ok = false;
        uint64_t t0 = GetTickCount64();
        const map_geo_t* g = nullptr;
        std::thread early;
        if (!hint.empty())
            early = std::thread([&] { g = geometry(hint, geo_err); });
        parser_t* p = new parser_t();
        bool built = parser::open(*p, f.path.c_str());
        if (built) {
            match_builder_t* mb = model::begin(*m, *p);
            track::state_t* ts = with_track ? track::begin(nullptr) : nullptr;
            fanout_t fo;
            fo.cancel = &g_cancel;
            callbacks_t mc, tc;
            model::hook(mb, mc);
            parser::add(fo, mc);
            if (ts) {
                track::hook(ts, tc);
                parser::add(fo, tc);
            }
            built = parser::run(*p, parser::fan(fo));
            if (!built)
                err = p->err;
            model::finish(mb, *p);
            if (ts) {
                std::string blob, track_err;
                bool track_ok = track::finish(ts, *p, built, blob, track_err) && !g_cancel;
                tracks::deliver(f, blob, track_ok, now_open());
            }
        } else {
            err = p->err;
        }
        demo::close(p->demo);
        delete p;
        uint64_t t1 = GetTickCount64();
        if (early.joinable())
            early.join();
        if (g_cancel) {
            delete m;
            return out;
        }
        if (!built) {
            out = "{\"type\":\"perf\",\"path\":\"" + json::escape(f.path) + "\",\"err\":\"" + json::escape(err) + "\",\"players\":[]}";
            delete m;
            return out;
        }
        map = m->map;
        if (map != hint)
            g = geometry(m->map, geo_err);
        err = geo_err;
        uint64_t t2 = GetTickCount64();
        vis_t* v = new vis_t();
        if (g)
            vis::build(*v, *m, *g, 4);
        uint64_t t3 = GetTickCount64();
        ok = g != nullptr;
        std::string speed_err;
        if (!weapons::load(finder::game_dir(), speed_err))
            dbg::print("[perfs] no weapon speeds: %s", speed_err.c_str());
        out = "{\"type\":\"perf\",\"path\":\"" + json::escape(f.path) + "\",\"err\":\"" + json::escape(g ? "" : err) + "\",\"players\":[";
        std::vector<std::string> rows(m->players.size());
        auto row_of = [&](size_t idx) {
            const player_t& pl = m->players[idx];
            if (pl.slot < 0 || !pl.steamid)
                return;
            perf_t pf;
            perf::measure(*m, g, g ? v : nullptr, pl.slot, pf);
            if (!pf.rounds)
                return;
            std::string& out = rows[idx];
            out += "{\"steamid\":\"" + std::to_string(pl.steamid) + "\"";
            const std::vector<double>* lists[3] = {&pf.ttd_ms, &pf.reaction_ms, &pf.placement_deg};
            for (int i = 0; i < 3; i++) {
                int k = ps_ttd + i;
                char num[64];
                snprintf(num, sizeof(num), "%.6f", pf.value[k]);
                out += std::string(",\"") + perf::names[k] + "\":" + (pf.has[k] ? num : "null") + ",\"" + perf::names[k] +
                       "_n\":" + std::to_string(lists[i]->size());
            }
            char setup[64];
            snprintf(setup, sizeof(setup), ",\"fps\":%.0f,\"sens\":%.3f", input::fps(*m, pl.slot), input::sens(*m, pl.slot));
            out += setup;
            shots_t sh = input::shots(*m, pl.slot);
            out += ",\"first\":" + std::to_string(sh.first) + ",\"first_hits\":" + std::to_string(sh.first_hits) + ",\"stops\":" + std::to_string(sh.stops) +
                   ",\"stopped\":" + std::to_string(sh.stopped);
            swings_t sw = input::swings(*m, pl.slot);
            out += ",\"swings\":" + std::to_string(sw.close) + ",\"swings_good\":" + std::to_string(sw.good);
            out += "}";
        };
        std::atomic<size_t> next = 0;
        unsigned n = g_threads ? (unsigned)g_threads : std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (unsigned k = 0; k + 1 < n && k + 1 < rows.size(); k++)
            pool.emplace_back([&] {
                if (g_idle)
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
                for (size_t i = next++; i < rows.size(); i = next++)
                    row_of(i);
            });
        for (size_t i = next++; i < rows.size(); i = next++)
            row_of(i);
        for (std::thread& t : pool)
            t.join();
        bool first = true;
        for (const std::string& r : rows) {
            if (r.empty())
                continue;
            if (!first)
                out += ",";
            first = false;
            out += r;
        }
        out += "]}";
        dbg::print("[perfs] %s in %llu ms (model%s %llu, geometry %llu, sight %llu)", f.name.c_str(), (unsigned long long)(GetTickCount64() - t0),
                   with_track ? " and tracks" : "", (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2));
        delete v;
        delete m;
        return out;
    }

    void work() {
        while (true) {
            demo_file_t f;
            std::string hint;
            bool open = false, with_track = false, need_perf = true;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                while (!g_quit && !g_pending && (g_back.empty() || !jobs::idle()))
                    g_cv.wait_for(lock, std::chrono::milliseconds(500));
                if (g_quit)
                    return;
                if (g_pending) {
                    f = g_want;
                    hint = g_want_map;
                    with_track = g_want_track;
                    g_pending = false;
                    open = true;
                } else {
                    f = g_back.front();
                    g_back.pop_front();
                    need_perf = !fresh(f);
                }
                g_current = f.path;
                g_current_open = open;
                g_cancel = false;
            }
            if (!open)
                with_track = !tracks::stored(f);
            std::string map, j;
            bool ok = false;
            if (need_perf || with_track) {
                if (open)
                    jobs::wait_urgent(g_quit);
                std::string info, info_err;
                json_t r;
                bool stale = false;
                if (hint.empty() && jobs::cached(f, info, stale) && json::parse(info, r, info_err))
                    hint = json::str(json::at(r, "map"));
                jobs::hold(true);
                if (need_perf) {
                    j = measure(f, hint, with_track, map, ok);
                } else {
                    std::string blob, err;
                    bool track_ok = track::extract(f.path, blob, err, &g_cancel) && !g_cancel;
                    tracks::deliver(f, blob, track_ok, false);
                }
                jobs::hold(false);
            }
            if (g_quit)
                return;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_current.clear();
                if (g_cancel) {
                    if (!g_current_open)
                        g_back.push_front(f);
                    continue;
                }
                if (ok && GetFileAttributesW(text::wide(f.path).c_str()) != INVALID_FILE_ATTRIBUTES) {
                    g_done[f.path] = entry_t{DATA_VERSION, f.size, f.mtime, map, geo_stamp(map), j};
                    save();
                }
            }
            if (!j.empty())
                window::post_json(j);
        }
    }

    void set_threads(int n, bool idle) {
        vis::set_threads(n, idle);
        g_threads = n < 0 ? 0 : n;
        g_idle = idle;
    }

    bool init(const std::wstring& cache_path) {
        g_cache_path = cache_path;
        load();
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
        for (auto& [name, g] : g_maps)
            delete g;
        g_maps.clear();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(g_mu);
        dbg::print("[perfs] cache cleared, %zu demos dropped", g_done.size());
        g_done.clear();
        save();
    }

    void forget(const std::vector<std::string>& paths) {
        std::lock_guard<std::mutex> lock(g_mu);
        size_t n = 0;
        for (const std::string& p : paths)
            n += g_done.erase(p);
        if (n)
            save();
    }

    void queue(const demo_file_t& f) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_current == f.path)
                return;
            for (const demo_file_t& q : g_back)
                if (q.path == f.path)
                    return;
            g_back.push_back(f);
        }
        g_cv.notify_all();
    }

    void want(const demo_file_t& f, const std::string& map) {
        {
            std::unique_lock<std::mutex> lock(g_mu);
            if (!g_current.empty() && !g_current_open && g_current != f.path)
                g_cancel = true;
            if (fresh(f)) {
                std::string j = g_done[f.path].json;
                lock.unlock();
                window::post_json(j);
                return;
            }
            if (g_current == f.path && !g_cancel) {
                if (!g_current_open && !tracks::stored(f))
                    tracks::expect(f);
                g_current_open = true;
                return;
            }
            if (!g_current.empty())
                g_cancel = true;
            g_want = f;
            g_want_map = map;
            g_want_track = !tracks::stored(f);
            if (g_want_track)
                tracks::expect(f);
            g_pending = true;
        }
        g_cv.notify_all();
    }
}
