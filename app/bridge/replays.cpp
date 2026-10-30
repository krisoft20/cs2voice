#include "pch.h"

#include "bridge/replays.h"
#include "dbg.h"
#include "demos/finder.h"
#include "json/json.h"
#include "platform/window.h"
#include "player/player.h"
#include "text.h"
#include "video/chunks.h"
#include "video/mp4.h"
#include "video/recordings.h"

namespace replays {
    const uint64_t STEAM_BASE = 76561197960265728ull;
    const int CS2 = 730;
    const double CHUNK_SECONDS = 3.0;
    const int SAVE_MARGIN = 2;
    const int REMOVE_TRIES = 30;
    const int AUTO_MAX = 50;
    const uint64_t AUTO_FREE_MIN = 10ull << 30;
    const int AUTO_FIRST = 30;
    const int AUTO_EVERY = 120;
    const double LIVE_DONE_GAP = 60;

    std::wstring g_root;

    bool read_file(const std::wstring& path, std::string& out) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;
        char buf[65536];
        size_t n;
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
        return true;
    }

    bool exists(const std::wstring& path) {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::wstring find_root(std::wstring& user) {
        std::wstring steam = text::wide(finder::steam_root());
        if (steam.empty())
            return {};
        uint64_t me = finder::current_user();
        if (me > STEAM_BASE) {
            user = steam + L"\\userdata\\" + std::to_wstring(me - STEAM_BASE);
            if (exists(user + L"\\gamerecordings\\gamerecording.pb"))
                return user + L"\\gamerecordings";
        }
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((steam + L"\\userdata\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return {};
        std::wstring best;
        FILETIME newest = {};
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
                continue;
            std::wstring dir = steam + L"\\userdata\\" + fd.cFileName;
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (!GetFileAttributesExW((dir + L"\\gamerecordings\\gamerecording.pb").c_str(), GetFileExInfoStandard, &a))
                continue;
            if (best.empty() || CompareFileTime(&a.ftLastWriteTime, &newest) > 0) {
                best = dir;
                newest = a.ftLastWriteTime;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        user = best;
        return best.empty() ? best : best + L"\\gamerecordings";
    }

    int background_mode(const std::wstring& user) {
        std::string vdf;
        if (user.empty() || !read_file(user + L"\\config\\localconfig.vdf", vdf))
            return -1;
        size_t at = vdf.find("\"BackgroundRecordMode\"");
        if (at == std::string::npos)
            return -1;
        size_t v = vdf.find('"', at + 22);
        return v == std::string::npos ? -1 : atoi(vdf.c_str() + v + 1);
    }

    std::string codec_of(const std::wstring& dir) {
        std::string init;
        mp4::track_t t;
        if (!read_file(dir + L"\\init-stream0.m4s", init) || !mp4::init((const uint8_t*)init.data(), init.size(), t))
            return "";
        if (t.codec == mp4::fourcc("hev1") || t.codec == mp4::fourcc("hvc1"))
            return "hevc";
        if (t.codec == mp4::fourcc("avc1") || t.codec == mp4::fourcc("avc3"))
            return "h264";
        return "other";
    }

    std::string num(double v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", v);
        return buf;
    }

    struct row_t {
        std::string id;
        int64_t unix = 0;
        std::string json;
        std::string meta;
        std::string events;
        std::wstring dir;
        double at = 0;
        double length = 0;
        bool saved = false;
        bool done = false;
        bool autosave = false;
    };

    struct plan_t {
        std::vector<int> keep;
        std::string cuts = "[]";
    };

    std::wstring g_saved;
    std::map<std::string, row_t> g_rows;
    std::mutex g_rows_mu;
    std::thread g_lister;
    std::atomic<bool> g_listing = false;
    std::atomic<bool> g_list_again = false;
    std::thread g_saver;
    std::thread g_tabler;
    std::atomic<bool> g_cancel = false;
    std::atomic<bool> g_busy = false;
    std::atomic<int> g_auto = -1;
    std::atomic<bool> g_auto_running = false;
    std::atomic<bool> g_hold = false;
    std::atomic<bool> g_quit = false;
    std::mutex g_job_mu;
    std::mutex g_list_mu;
    std::thread g_ticker;
    std::condition_variable g_tick_cv;
    std::set<std::string> g_skip;
    std::mutex g_skip_mu;

    std::string saved_name(const std::string& id) {
        std::string name = id;
        std::replace(name.begin(), name.end(), '#', '_');
        return name;
    }

    bool timescale_of(const std::wstring& dir, double& ts) {
        std::string init;
        mp4::track_t track;
        if (!read_file(dir + L"\\init-stream0.m4s", init) || !mp4::init((const uint8_t*)init.data(), init.size(), track) || !track.timescale)
            return false;
        ts = track.timescale;
        return true;
    }

    bool chunk_end(const std::wstring& dir, int idx, double ts, double& to) {
        std::string data;
        std::vector<mp4::sample_t> samples;
        if (!read_file(dir + chunks::name(0, idx), data) || !mp4::fragments((const uint8_t*)data.data(), data.size(), samples) || samples.empty())
            return false;
        const mp4::sample_t& b = samples.back();
        to = ((int64_t)b.dts + b.cto + b.dur) / ts;
        return true;
    }

    void remove_dir(const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }

    void post_progress(const std::string& id, int done, int total) {
        window::post_json("{\"type\":\"replay_saving\",\"id\":\"" + json::escape(id) + "\",\"done\":" + std::to_string(done) + ",\"total\":" +
                          std::to_string(total) + "}");
    }

    bool copy_match(const std::string& id, const row_t& row, const plan_t& plan, const std::wstring& out, uint64_t& bytes, bool autosave) {
        double ts = 0;
        if (plan.keep.empty() || !timescale_of(row.dir, ts))
            return false;
        CreateDirectoryW(g_saved.c_str(), nullptr);
        CreateDirectoryW(out.c_str(), nullptr);
        for (const wchar_t* name : {L"\\init-stream0.m4s", L"\\init-stream1.m4s"})
            if (!CopyFileW((row.dir + name).c_str(), (out + name).c_str(), FALSE))
                return false;
        int total = (int)plan.keep.size();
        for (int i = 0; i < total; i++) {
            if (g_cancel)
                return false;
            for (int s = 0; s < 2; s++) {
                std::wstring name = chunks::name(s, plan.keep[i]);
                WIN32_FILE_ATTRIBUTE_DATA fa;
                if (!GetFileAttributesExW((row.dir + name).c_str(), GetFileExInfoStandard, &fa))
                    continue;
                if (!CopyFileW((row.dir + name).c_str(), (out + name).c_str(), FALSE))
                    return false;
                bytes += ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            }
            if (i % 10 == 0)
                post_progress(id, i, total);
        }
        std::vector<chunk_t> list = chunks::scan(out, 0);
        double to = 0;
        if (list.empty() || !chunk_end(out, list.back().idx, ts, to))
            return false;
        std::string j = "{" + row.meta + ",\"video\":\"saved:" + saved_name(id) + "\",\"codec\":\"" + codec_of(out) + "\",\"at\":" + num(row.at) +
                        ",\"from\":" + num(list.front().start / ts) + ",\"to\":" + num(to) + ",\"saved\":true,\"bytes\":" + std::to_string(bytes) +
                        ",\"cuts\":" + plan.cuts + (autosave ? ",\"auto\":true" : "") + row.events + "}";
        FILE* f = _wfopen((out + L"\\match.json").c_str(), L"wb");
        if (!f)
            return false;
        bool ok = fwrite(j.data(), 1, j.size(), f) == j.size();
        fclose(f);
        return ok;
    }

    bool auto_plan(const row_t& row, plan_t& plan) {
        double ts = 0;
        if (!timescale_of(row.dir, ts))
            return false;
        double lo = row.at - SAVE_MARGIN * CHUNK_SECONDS, hi = row.at + row.length + SAVE_MARGIN * CHUNK_SECONDS;
        for (const chunk_t& c : chunks::scan(row.dir, 0)) {
            double a = c.start / ts;
            if (a + CHUNK_SECONDS >= lo && a <= hi)
                plan.keep.push_back(c.idx);
        }
        return !plan.keep.empty();
    }

    void save_worker(std::string id, row_t row, plan_t plan, bool autosave) {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        std::wstring out = g_saved + L"\\" + text::wide(saved_name(id));
        std::wstring part = out + L".part";
        remove_dir(part);
        uint64_t bytes = 0;
        bool ok = (!autosave || auto_plan(row, plan)) && copy_match(id, row, plan, part, bytes, autosave) && MoveFileExW(part.c_str(), out.c_str(), 0);
        if (!ok)
            remove_dir(part);
        dbg::print("[replays] %s %s %s, %zu chunks, %.1f mb", autosave ? "auto save" : "save", id.c_str(), ok ? "done" : "failed", plan.keep.size(), bytes / 1048576.0);
        if (!ok && autosave && !g_cancel) {
            std::lock_guard<std::mutex> lock(g_skip_mu);
            g_skip.insert(id);
        }
        window::post_json("{\"type\":\"replay_saved\",\"id\":\"" + json::escape(id) + "\",\"ok\":" + (ok ? "true" : "false") + "}");
        g_auto_running = false;
        g_busy = false;
        refresh();
    }

    double live_length(const std::wstring& root, const rec_video_t& c) {
        std::wstring dir = root + L"\\video\\" + text::wide(c.folder);
        double ts = 0;
        if (!timescale_of(dir, ts))
            return 0;
        std::vector<chunk_t> list = chunks::scan(dir, 0);
        if (list.empty())
            return 0;
        double end = (double)list.back().start / ts + CHUNK_SECONDS;
        dbg::print("[replays] %s is still recording, %.0f s so far", c.folder.c_str(), end);
        return std::max(0.0, c.origin + end - c.start);
    }

    void add_saved(std::map<std::string, row_t>& rows) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_saved + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do {
            std::wstring name = fd.cFileName;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name[0] == L'.' || name.find(L'.') != std::wstring::npos)
                continue;
            row_t r;
            json_t j;
            std::string err;
            r.dir = g_saved + L"\\" + name;
            if (!read_file(r.dir + L"\\match.json", r.json) || !json::parse(r.json, j, err))
                continue;
            std::string id = json::str(json::at(j, "id"));
            if (id.empty())
                continue;
            r.id = id;
            r.unix = (int64_t)json::num(json::at(j, "unix"));
            r.saved = true;
            const json_t* a = json::at(j, "auto");
            r.autosave = a && a->kind == jk_bool && a->b;
            rows[id] = r;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    void tick();

    std::string usage(const std::map<std::string, row_t>& rows) {
        std::wstring dir;
        int64_t newest = 0;
        std::vector<double> lengths;
        for (const auto& [id, r] : rows) {
            if (r.length > 0)
                lengths.push_back(r.length);
            if (!r.saved && !r.dir.empty() && r.unix > newest) {
                newest = r.unix;
                dir = r.dir;
            }
        }
        uint64_t bytes = 0;
        int count = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = dir.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((dir + L"\\chunk-stream*.m4s").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                bytes += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                if (wcsncmp(fd.cFileName, L"chunk-stream0-", 14) == 0)
                    count++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        double median = 0;
        if (!lengths.empty()) {
            std::sort(lengths.begin(), lengths.end());
            median = lengths[lengths.size() / 2];
        }
        ULARGE_INTEGER free = {};
        CreateDirectoryW(g_saved.c_str(), nullptr);
        GetDiskFreeSpaceExW(g_saved.c_str(), &free, nullptr, nullptr);
        return ",\"rate\":" + num(count ? bytes / (count * CHUNK_SECONDS) : 0) + ",\"buffer\":" + num(count * CHUNK_SECONDS) + ",\"length\":" + num(median) + ",\"free\":" + std::to_string(free.QuadPart) +
               ",\"saved_dir\":\"" + json::escape(text::utf8(g_saved)) + "\"";
    }

    void save_auto() {
        std::string text = "keep=" + std::to_string(g_auto.load()) + "\n";
        {
            std::lock_guard<std::mutex> lock(g_skip_mu);
            for (const std::string& id : g_skip)
                text += "skip=" + id + "\n";
        }
        CreateDirectoryW(g_saved.c_str(), nullptr);
        FILE* f = _wfopen((g_saved + L"\\auto.txt").c_str(), L"wb");
        if (!f)
            return;
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }

    void init(const std::wstring& dir) {
        g_saved = dir;
        std::string text;
        if (read_file(dir + L"\\auto.txt", text)) {
            size_t at = 0;
            while (at < text.size()) {
                size_t end = text.find('\n', at);
                std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? text.size() : end + 1;
                if (line.compare(0, 5, "keep=") == 0)
                    g_auto = std::clamp(atoi(line.c_str() + 5), 0, AUTO_MAX);
                else if (line.compare(0, 5, "skip=") == 0)
                    g_skip.insert(line.substr(5));
            }
        }
        g_ticker = std::thread(tick);
        for (const wchar_t* junk : {L"\\*.part", L"\\*.del"}) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + junk).c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            do
                remove_dir(dir + L"\\" + fd.cFileName);
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    void cleanup() {
        g_cancel = true;
        {
            std::lock_guard<std::mutex> lock(g_list_mu);
            g_quit = true;
        }
        g_tick_cv.notify_all();
        if (g_ticker.joinable())
            g_ticker.join();
        if (g_lister.joinable())
            g_lister.join();
        {
            std::lock_guard<std::mutex> lock(g_job_mu);
            if (g_saver.joinable())
                g_saver.join();
        }
        if (g_tabler.joinable())
            g_tabler.join();
    }

    void autosave(const std::string& id);

    void tick() {
        int wait = AUTO_FIRST;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(g_list_mu);
                if (g_tick_cv.wait_for(lock, std::chrono::seconds(wait), [] { return g_quit.load(); }))
                    return;
            }
            wait = AUTO_EVERY;
            if (g_auto > 0)
                refresh();
        }
    }

    void list() {
        std::wstring user;
        std::wstring root = find_root(user);
        int mode = background_mode(user);
        std::string head = "{\"type\":\"replays\",\"folder\":\"" + json::escape(text::utf8(root)) + "\",\"background\":" + std::to_string(mode) +
                           ",\"keep\":" + std::to_string(g_auto.load());
        std::string pb;
        std::vector<rec_timeline_t> timelines;
        std::map<std::string, row_t> rows;
        if (!root.empty() && read_file(root + L"\\gamerecording.pb", pb))
            recordings::index(pb, timelines);
        std::map<std::string, std::string> codecs;
        for (const rec_timeline_t& t : timelines) {
            if (t.app != CS2)
                continue;
            std::string text;
            std::vector<rec_event_t> events;
            bool live = !read_file(root + L"\\timelines\\" + text::wide(t.name) + L".json", text);
            if (!live)
                recordings::events(text, events);
            else
                events = t.marks;
            std::vector<rec_match_t> matches = t.matches;
            recordings::extend(matches, events);
            std::vector<rec_video_t> videos = t.videos;
            for (rec_video_t& c : videos)
                if (c.duration <= 0)
                    c.duration = live_length(root, c);
            for (size_t i = 0; i < matches.size(); i++) {
                const rec_match_t& m = matches[i];
                const rec_video_t* v = nullptr;
                for (const rec_video_t& c : videos)
                    if (m.start + m.duration > c.start && m.start < c.start + c.duration && exists(root + L"\\video\\" + text::wide(c.folder)))
                        v = &c;
                std::string id = t.name + "#" + std::to_string(i);
                row_t r;
                r.id = id;
                r.unix = t.unix + (int64_t)m.start;
                r.length = m.duration;
                r.meta = "\"id\":\"" + json::escape(id) + "\",\"mode\":\"" + json::escape(m.mode) + "\",\"map\":\"" + json::escape(m.map) + "\",\"kda\":\"" +
                         json::escape(m.kda) + "\",\"score\":\"" + json::escape(m.score) + "\",\"unix\":" + std::to_string(r.unix) +
                         ",\"duration\":" + num(m.duration);
                std::string video;
                if (v) {
                    auto it = codecs.find(v->folder);
                    if (it == codecs.end())
                        it = codecs.emplace(v->folder, codec_of(root + L"\\video\\" + text::wide(v->folder))).first;
                    video = ",\"video\":\"" + json::escape(v->folder) + "\",\"codec\":\"" + it->second + "\",\"at\":" + num(m.start - v->origin) +
                            ",\"from\":" + num(v->start - v->origin) + ",\"to\":" + num(v->start + v->duration - v->origin);
                    r.dir = root + L"\\video\\" + text::wide(v->folder);
                    r.at = m.start - v->origin;
                    r.done = !live || i + 1 < matches.size() || v->start + v->duration > m.start + m.duration + LIVE_DONE_GAP;
                }
                r.events = ",\"events\":[";
                bool first = true;
                for (const rec_event_t& e : events) {
                    if (e.t < m.start - 2 || e.t > m.start + m.duration + 2)
                        continue;
                    r.events += std::string(first ? "" : ",") + "[" + num(e.t - m.start) + ",\"" + json::escape(e.kind) + "\",\"" + json::escape(e.icon) +
                                "\",\"" + json::escape(e.title) + "\",\"" + json::escape(e.text) + "\"]";
                    first = false;
                }
                r.events += "]";
                r.json = "{" + r.meta + video + (live ? ",\"live\":true" : "") + (r.done ? ",\"done\":true" : "") + r.events + "}";
                rows[id] = r;
            }
        }
        add_saved(rows);
        std::vector<const row_t*> order;
        for (const auto& [id, r] : rows)
            order.push_back(&r);
        std::sort(order.begin(), order.end(), [](const row_t* a, const row_t* b) { return a->unix > b->unix; });
        std::string out = head + (g_auto < 0 ? usage(rows) : "") + ",\"matches\":[";
        for (size_t i = 0; i < order.size(); i++)
            out += (i ? "," : "") + order[i]->json;
        window::post_json(out + "]}");
        dbg::print("[replays] %zu matches in %zu timelines", rows.size(), timelines.size());
        std::string fresh, stale;
        int left = g_auto;
        if (left > 0) {
            std::lock_guard<std::mutex> lock(g_skip_mu);
            for (const row_t* r : order) {
                bool keep = left > 0 && (r->saved || (r->done && !g_skip.count(r->id)));
                if (keep)
                    left--;
                if (keep && !r->saved && fresh.empty() && !g_skip.count(r->id))
                    fresh = r->id;
                if (!keep && r->saved && r->autosave && stale.empty())
                    stale = r->id;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_rows_mu);
            g_root = root;
            g_rows.swap(rows);
        }
        if (!stale.empty())
            remove(stale, false);
        else if (!fresh.empty())
            autosave(fresh);
    }

    void refresh() {
        std::lock_guard<std::mutex> lock(g_list_mu);
        if (g_quit)
            return;
        if (g_listing.exchange(true)) {
            g_list_again = true;
            return;
        }
        if (g_lister.joinable())
            g_lister.join();
        g_lister = std::thread([] {
            do {
                g_list_again = false;
                list();
            } while (g_list_again);
            g_listing = false;
        });
    }

    bool row_of(const std::string& id, row_t& out) {
        std::lock_guard<std::mutex> lock(g_rows_mu);
        auto it = g_rows.find(id);
        if (it == g_rows.end())
            return false;
        out = it->second;
        return true;
    }

    template <typename F>
    void start(F job) {
        std::lock_guard<std::mutex> lock(g_job_mu);
        if (g_saver.joinable())
            g_saver.join();
        if (g_quit) {
            g_busy = false;
            return;
        }
        g_cancel = false;
        g_saver = std::thread(job);
    }

    bool claim(bool user) {
        if (!g_busy.exchange(true))
            return true;
        if (!user || !g_auto_running)
            return false;
        g_hold = true;
        g_cancel = true;
        bool ok = false;
        for (int i = 0; i < REMOVE_TRIES * 4 && !ok; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            ok = !g_busy.exchange(true);
        }
        g_hold = false;
        return ok;
    }

    void save(const std::string& id, const std::vector<int>& keep, const std::vector<std::pair<double, double>>& cuts) {
        row_t row;
        if (!row_of(id, row) || row.saved || row.dir.empty() || g_saved.empty() || keep.empty() || !claim(true))
            return;
        plan_t plan;
        plan.keep = keep;
        plan.cuts = "[";
        for (size_t i = 0; i < cuts.size(); i++)
            plan.cuts += std::string(i ? "," : "") + "[" + num(cuts[i].first) + "," + num(cuts[i].second) + "]";
        plan.cuts += "]";
        {
            std::lock_guard<std::mutex> lock(g_skip_mu);
            g_skip.erase(id);
        }
        save_auto();
        start([id, row, plan] { save_worker(id, row, plan, false); });
    }

    void autosave(const std::string& id) {
        row_t row;
        ULARGE_INTEGER free = {};
        if (g_hold || g_quit || !row_of(id, row) || row.saved || row.dir.empty() || g_saved.empty())
            return;
        CreateDirectoryW(g_saved.c_str(), nullptr);
        if (!GetDiskFreeSpaceExW(g_saved.c_str(), &free, nullptr, nullptr) || free.QuadPart < AUTO_FREE_MIN) {
            dbg::print("[replays] not keeping %s, only %.1f gb free", id.c_str(), free.QuadPart / 1073741824.0);
            return;
        }
        if (g_busy.exchange(true))
            return;
        g_auto_running = true;
        start([id, row] { save_worker(id, row, plan_t(), true); });
    }

    void pin(const std::string& id) {
        row_t row;
        std::string text;
        if (!row_of(id, row) || !row.saved || !row.autosave || !read_file(row.dir + L"\\match.json", text))
            return;
        size_t at = text.find(",\"auto\":true");
        if (at == std::string::npos)
            return;
        text.erase(at, 12);
        FILE* f = _wfopen((row.dir + L"\\match.json").c_str(), L"wb");
        if (!f)
            return;
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
        refresh();
    }

    void set_keep(int n) {
        g_auto = std::clamp(n, 0, AUTO_MAX);
        save_auto();
        refresh();
    }

    void remove_worker(std::string name) {
        std::wstring dir = g_saved + L"\\" + text::wide(name);
        if (MoveFileExW(dir.c_str(), (dir + L".del").c_str(), 0))
            dir += L".del";
        for (int i = 0; i < REMOVE_TRIES && exists(dir); i++) {
            remove_dir(dir);
            if (exists(dir))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        dbg::print("[replays] removed %s%s", name.c_str(), exists(dir) ? ", some files are still in use" : "");
        g_busy = false;
        refresh();
    }

    void remove(const std::string& id, bool user) {
        std::string name = saved_name(id);
        if (g_saved.empty() || name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos || (!user && g_hold) ||
            !claim(user))
            return;
        if (user) {
            player::close();
            {
                std::lock_guard<std::mutex> lock(g_skip_mu);
                g_skip.insert(id);
            }
            save_auto();
        }
        start([name] { remove_worker(name); });
    }

    void table_worker(std::string id, row_t row) {
        double ts = 0;
        if (!timescale_of(row.dir, ts))
            return;
        std::vector<chunk_t> video = chunks::scan(row.dir, 0);
        std::map<int, uint64_t> sound;
        for (const chunk_t& c : chunks::scan(row.dir, 1))
            sound[c.idx] = c.bytes;
        double lo = row.at - SAVE_MARGIN * CHUNK_SECONDS, hi = row.at + row.length + SAVE_MARGIN * CHUNK_SECONDS;
        std::string j = "{\"type\":\"replay_chunks\",\"id\":\"" + json::escape(id) + "\",\"list\":[";
        bool first = true;
        for (size_t i = 0; i < video.size(); i++) {
            double a = video[i].start / ts;
            double b = i + 1 < video.size() ? video[i + 1].start / ts : a + CHUNK_SECONDS;
            if (b < lo || a > hi)
                continue;
            j += std::string(first ? "" : ",") + "[" + std::to_string(video[i].idx) + "," + num(a) + "," + num(b) + "," +
                 std::to_string(video[i].bytes + sound[video[i].idx]) + "]";
            first = false;
        }
        window::post_json(j + "]}");
    }

    void table(const std::string& id) {
        row_t row;
        if (!row_of(id, row) || row.dir.empty() || row.saved)
            return;
        if (g_tabler.joinable())
            g_tabler.join();
        g_tabler = std::thread(table_worker, id, row);
    }

    std::wstring video_dir(const std::string& folder) {
        bool saved = folder.compare(0, 6, "saved:") == 0;
        std::string name = saved ? folder.substr(6) : folder;
        if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            return {};
        std::wstring root;
        {
            std::lock_guard<std::mutex> lock(g_rows_mu);
            root = g_root;
        }
        std::wstring dir = saved ? g_saved + L"\\" + text::wide(name) : root + L"\\video\\" + text::wide(name);
        if (saved ? g_saved.empty() : root.empty())
            return {};
        return exists(dir + (saved ? L"\\match.json" : L"\\session.mpd")) ? dir : std::wstring();
    }
}
