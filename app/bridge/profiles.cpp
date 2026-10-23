#include "pch.h"

#include <winhttp.h>

#include "bridge/profiles.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"

namespace profiles {
    const int64_t MAX_AGE = 7 * 24 * 3600;
    const int64_t RECHECK = 10 * 60;
    const int WORKERS = 6;
    const auto BACKOFF = std::chrono::seconds(60);

    struct entry_t {
        std::string url;
        int64_t fetched = 0;
        bool file = false;
        bool vac = false;
        bool limited = false;
        bool hidden = false;
        int since = 0;
    };

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::deque<std::string> g_queue;
    std::map<std::string, entry_t> g_cache;
    std::thread g_workers[WORKERS];
    HINTERNET g_req[WORKERS] = {};
    std::atomic<bool> g_quit = false;
    std::wstring g_cache_path;
    std::wstring g_dir;
    HINTERNET g_session = nullptr;
    std::atomic<bool> g_online = true;
    bool g_dirty = false;

    int64_t now() {
        return (int64_t)time(nullptr);
    }

    bool valid_id(const std::string& id) {
        if (id.size() < 15 || id.size() > 20)
            return false;
        for (char c : id)
            if (c < '0' || c > '9')
                return false;
        return true;
    }

    std::wstring file_of(const std::string& id) {
        return g_dir + L"\\" + text::wide(id) + L".jpg";
    }

    void load() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"rb");
        if (!f)
            return;
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            files::chomp(s);
            std::vector<std::string> col;
            size_t at = 0;
            while (true) {
                size_t tab = s.find('\t', at);
                col.push_back(s.substr(at, tab - at));
                if (tab == std::string::npos)
                    break;
                at = tab + 1;
            }
            if (col.size() != 7)
                continue;
            entry_t e;
            e.fetched = strtoll(col[1].c_str(), nullptr, 10);
            e.vac = col[2] == "1";
            e.limited = col[3] == "1";
            e.hidden = col[4] == "1";
            e.since = atoi(col[5].c_str());
            e.url = col[6];
            e.file = GetFileAttributesW(file_of(col[0]).c_str()) != INVALID_FILE_ATTRIBUTES;
            g_cache[col[0]] = e;
        }
        fclose(f);
    }

    void save() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"wb");
        if (!f)
            return;
        for (const auto& [id, e] : g_cache)
            fprintf(f, "%s\t%lld\t%d\t%d\t%d\t%d\t%s\n", id.c_str(), (long long)e.fetched, e.vac, e.limited, e.hidden, e.since, e.url.c_str());
        fclose(f);
        g_dirty = false;
    }

    int get(int w, const std::wstring& host, const std::wstring& path, std::string& body) {
        HINTERNET con = WinHttpConnect(g_session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!con)
            return 0;
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_quit && req) {
                WinHttpCloseHandle(req);
                req = nullptr;
            }
            g_req[w] = req;
        }
        DWORD status = 0;
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);
        }
        while (status == 200) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
                break;
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &got)) {
                status = 0;
                break;
            }
            body.append(chunk.data(), got);
        }
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_req[w])
                WinHttpCloseHandle(g_req[w]);
            g_req[w] = nullptr;
        }
        WinHttpCloseHandle(con);
        return status == 200 && body.empty() ? 0 : (int)status;
    }

    std::string tag(const std::string& xml, const char* name) {
        std::string open = std::string("<") + name + ">", close = std::string("</") + name + ">";
        size_t a = xml.find(open);
        size_t b = a == std::string::npos ? a : xml.find(close, a);
        if (b == std::string::npos)
            return {};
        std::string v = xml.substr(a + open.size(), b - a - open.size());
        if (v.rfind("<![CDATA[", 0) == 0 && v.size() >= 12)
            v = v.substr(9, v.size() - 12);
        return v;
    }

    int year_of(const std::string& since) {
        size_t comma = since.rfind(", ");
        if (since.empty())
            return 0;
        if (comma == std::string::npos) {
            time_t t = time(nullptr);
            tm local;
            localtime_s(&local, &t);
            return local.tm_year + 1900;
        }
        return atoi(since.c_str() + comma + 2);
    }

    void read_facts(const std::string& xml, entry_t& e) {
        e.vac = tag(xml, "vacBanned") == "1";
        e.limited = tag(xml, "isLimitedAccount") == "1";
        e.hidden = tag(xml, "privacyState") != "public";
        e.since = year_of(tag(xml, "memberSince"));
    }

    std::string avatar_of(const std::string& xml) {
        size_t at = xml.find("<avatarMedium>");
        if (at == std::string::npos)
            return {};
        size_t open = xml.find("http", at);
        size_t close = xml.find_first_of("]<", open);
        if (open == std::string::npos || close == std::string::npos)
            return {};
        std::string url = xml.substr(open, close - open);
        return url.rfind("https://", 0) == 0 ? url : std::string();
    }

    bool write_file(const std::wstring& path, const std::string& data) {
        std::wstring tmp = path + L".part";
        FILE* f = _wfopen(tmp.c_str(), L"wb");
        if (!f)
            return false;
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (ok)
            ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        if (!ok)
            DeleteFileW(tmp.c_str());
        return ok;
    }

    std::string version(const std::string& url) {
        size_t a = url.rfind('/') + 1, b = url.find_first_of("_.", a);
        return url.substr(a, b == std::string::npos ? b : b - a);
    }

    void post(const std::map<std::string, entry_t>& items) {
        if (items.empty())
            return;
        std::string j = "{\"type\":\"profiles\",\"items\":{";
        for (const auto& [id, e] : items) {
            if (j.back() != '{')
                j += ",";
            j += "\"" + id + "\":{\"avatar\":\"" + (e.file ? json::escape(version(e.url)) : std::string()) + "\",\"vac\":" +
                 (e.vac ? "true" : "false") + ",\"limited\":" + (e.limited ? "true" : "false") + ",\"hidden\":" +
                 (e.hidden ? "true" : "false") + ",\"since\":" + std::to_string(e.since) + "}";
        }
        j += "}}";
        window::post_json(j);
    }

    int fetch(int w, const std::string& id, std::string& url, const std::string& have, entry_t& facts) {
        if (url.empty()) {
            std::string xml;
            int status = get(w, L"steamcommunity.com", text::wide("/profiles/" + id + "?xml=1"), xml);
            if (status != 200)
                return status;
            read_facts(xml, facts);
            url = avatar_of(xml);
            if (url.empty() || url == have)
                return 200;
        }
        size_t slash = url.find('/', 8);
        if (slash == std::string::npos)
            return 0;
        std::string img;
        int status = get(w, text::wide(url.substr(8, slash - 8)), text::wide(url.substr(slash)), img);
        if (status == 200 && !write_file(file_of(id), img))
            return 0;
        return status;
    }

    void work(int w) {
        while (true) {
            std::string id, url, old;
            bool have = false;
            entry_t facts;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || !g_queue.empty(); });
                if (g_quit)
                    return;
                id = g_queue.front();
                g_queue.pop_front();
                auto it = g_cache.find(id);
                if (it != g_cache.end()) {
                    facts = it->second;
                    old = it->second.url;
                    have = it->second.file;
                    if (!have && now() - it->second.fetched < MAX_AGE)
                        url = old;
                }
            }
            int status = fetch(w, id, url, have ? old : std::string(), facts);
            if (g_quit)
                return;
            if (status == 429) {
                std::unique_lock<std::mutex> lock(g_mu);
                g_queue.push_front(id);
                dbg::print("[profiles] steam says slow down");
                g_cv.wait_for(lock, BACKOFF, [] { return g_quit.load(); });
                continue;
            }
            if (status != 200) {
                dbg::print("[profiles] %s failed %d", id.c_str(), status);
                continue;
            }
            facts.url = url;
            facts.fetched = now();
            facts.file = !url.empty();
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cache[id] = facts;
                g_dirty = true;
                if (g_queue.empty())
                    save();
            }
            post({{id, facts}});
        }
    }

    bool init(const std::wstring& cache_path, const std::wstring& dir) {
        g_cache_path = cache_path;
        g_dir = dir;
        CreateDirectoryW(dir.c_str(), nullptr);
        load();
        g_session = WinHttpOpen(L"cs2voice", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session) {
            dbg::print("[profiles] no http session");
            return true;
        }
        WinHttpSetTimeouts(g_session, 4000, 4000, 4000, 6000);
        for (int i = 0; i < WORKERS; i++)
            g_workers[i] = std::thread(work, i);
        return true;
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
            g_queue.clear();
            for (HINTERNET& r : g_req) {
                if (r)
                    WinHttpCloseHandle(r);
                r = nullptr;
            }
            if (g_dirty)
                save();
        }
        g_cv.notify_all();
        for (std::thread& t : g_workers)
            if (t.joinable())
                t.join();
        if (g_session)
            WinHttpCloseHandle(g_session);
        g_session = nullptr;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cache.clear();
        files::wipe(g_dir, L"*.jpg");
        save();
    }

    void keep(const std::vector<std::string>& ids) {
        std::map<std::string, bool> used;
        for (const std::string& id : ids)
            used[id] = true;
        int n = 0;
        std::lock_guard<std::mutex> lock(g_mu);
        for (auto it = g_cache.begin(); it != g_cache.end();) {
            if (used.count(it->first)) {
                ++it;
                continue;
            }
            if (it->second.file)
                DeleteFileW(file_of(it->first).c_str());
            it = g_cache.erase(it);
            n++;
        }
        if (n) {
            dbg::print("[profiles] dropped %d players no demo has anymore", n);
            save();
        }
    }

    void set_online(bool on) {
        g_online = on;
        dbg::print("[profiles] steam %s", on ? "on" : "off");
    }

    void want(const std::vector<std::string>& ids, bool urgent) {
        std::map<std::string, entry_t> known;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (const std::string& id : ids) {
                if (!valid_id(id) || known.count(id))
                    continue;
                auto it = g_cache.find(id);
                bool fresh = it != g_cache.end() && now() - it->second.fetched < (urgent ? RECHECK : MAX_AGE);
                if (it != g_cache.end())
                    known[id] = it->second;
                if ((fresh && (it->second.file || it->second.url.empty())) || !g_session || !g_online)
                    continue;
                auto q = std::find(g_queue.begin(), g_queue.end(), id);
                if (q != g_queue.end()) {
                    if (!urgent)
                        continue;
                    g_queue.erase(q);
                }
                if (urgent)
                    g_queue.push_front(id);
                else
                    g_queue.push_back(id);
            }
        }
        g_cv.notify_all();
        post(known);
    }
}
