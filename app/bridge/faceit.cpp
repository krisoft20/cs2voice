#include "pch.h"

#include "bridge/faceit.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"
#include "web/web.h"

namespace faceit {
    const wchar_t* MATCH_URL = L"https://www.faceit.com/api/match/v2/match/";
    const wchar_t* RANK_URL = L"https://www.faceit.com/api/ranking/v1/globalranking/cs2/";
    const wchar_t* USER_URL = L"https://www.faceit.com/api/users/v1/nicknames/";
    const wchar_t* HISTORY_URL = L"https://www.faceit.com/api/stats/v1/stats/time/users/";
    const int CHALLENGER_LEVEL = 10;
    const int HISTORY_PAGE = 100;
    const int HISTORY_PAGES = 10;

    struct player_t {
        std::string steamid;
        std::string nick;
        std::string uid;
        int level = 0;
        int elo = 0;
        int rank = 0;
    };

    std::wstring g_cache_path;
    std::map<std::string, std::vector<player_t>> g_cache;
    std::set<std::string> g_asked;
    std::string g_history_for;
    std::string g_history;
    bool g_history_busy = false;

    std::string match_id(const std::string& name) {
        if (name.size() < 38 || name.compare(0, 2, "1-") != 0)
            return "";
        for (size_t i = 2; i < 38; i++) {
            bool dash = i == 10 || i == 15 || i == 20 || i == 25;
            if (dash ? name[i] != '-' : !isxdigit((unsigned char)name[i]))
                return "";
        }
        return text::lower(name.substr(0, 38));
    }

    void load() {
        FILE* f = _wfopen(g_cache_path.c_str(), L"rb");
        if (!f)
            return;
        char line[4096];
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
            if (col.size() < 6 || (col.size() - 1) % 5)
                continue;
            std::vector<player_t>& ps = g_cache[col[0]];
            ps.clear();
            for (size_t i = 1; i + 4 < col.size(); i += 5) {
                player_t p;
                p.steamid = col[i];
                p.level = atoi(col[i + 1].c_str());
                p.elo = atoi(col[i + 2].c_str());
                p.rank = atoi(col[i + 3].c_str());
                p.nick = col[i + 4];
                ps.push_back(p);
            }
        }
        fclose(f);
        dbg::print("[faceit] %zu cached matches", g_cache.size());
    }

    void save(const std::string& id, const std::vector<player_t>& ps) {
        FILE* f = _wfopen(g_cache_path.c_str(), L"ab");
        if (!f)
            return;
        std::string line = id;
        for (const player_t& p : ps)
            line += "\t" + p.steamid + "\t" + std::to_string(p.level) + "\t" + std::to_string(p.elo) + "\t" + std::to_string(p.rank) + "\t" + p.nick;
        fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }

    void send(const std::string& path, const std::vector<player_t>& ps) {
        std::string j = "{\"type\":\"faceit\",\"path\":\"" + json::escape(path) + "\",\"players\":[";
        for (size_t i = 0; i < ps.size(); i++) {
            const player_t& p = ps[i];
            if (i)
                j += ",";
            j += "{\"steamid\":\"" + p.steamid + "\",\"nick\":\"" + json::escape(p.nick) + "\",\"level\":" + std::to_string(p.level) +
                 ",\"elo\":" + std::to_string(p.elo) + ",\"rank\":" + std::to_string(p.rank) + "}";
        }
        window::post_json(j + "]}");
    }

    bool parse(const std::string& body, std::vector<player_t>& out, std::string& region) {
        json_t r;
        std::string err;
        if (!json::parse(body, r, err))
            return false;
        const json_t* payload = json::at(r, "payload");
        region = payload ? json::str(json::at(*payload, "region")) : "";
        const json_t* teams = payload ? json::at(*payload, "teams") : nullptr;
        if (!teams || teams->kind != jk_obj)
            return false;
        for (auto& [side, team] : teams->obj) {
            const json_t* roster = json::at(team, "roster");
            if (!roster || roster->kind != jk_arr)
                continue;
            for (const json_t& p : roster->arr) {
                player_t pl;
                pl.steamid = json::str(json::at(p, "gameId"));
                pl.nick = json::str(json::at(p, "nickname"));
                pl.uid = json::str(json::at(p, "id"));
                pl.level = (int)json::num(json::at(p, "gameSkillLevel"));
                pl.elo = (int)json::num(json::at(p, "elo"));
                if (!pl.steamid.empty() && !pl.nick.empty() && pl.nick.find('\t') == std::string::npos)
                    out.push_back(pl);
            }
        }
        return !out.empty();
    }

    void fetch_ranks(const std::string& id, const std::string& path, const std::string& region) {
        std::vector<player_t>& ps = g_cache[id];
        auto left = std::make_shared<int>(0);
        for (const player_t& p : ps)
            *left += p.level >= CHALLENGER_LEVEL && !p.uid.empty() && !region.empty();
        if (!*left) {
            save(id, ps);
            return;
        }
        for (size_t i = 0; i < ps.size(); i++) {
            if (ps[i].level < CHALLENGER_LEVEL || ps[i].uid.empty() || region.empty())
                continue;
            web::fetch_page(RANK_URL + text::wide(region + "/" + ps[i].uid), [id, path, i, left](bool ok, const std::string& body) {
                json_t r;
                std::string err;
                std::vector<player_t>& all = g_cache[id];
                if (ok && i < all.size() && json::parse(body, r, err))
                    all[i].rank = (int)json::num(json::at(r, "payload"));
                if (--*left)
                    return;
                save(id, all);
                send(path, all);
            });
        }
    }

    void init(const std::wstring& cache_path) {
        g_cache_path = cache_path;
        load();
    }

    void want(const std::string& path, const std::string& name) {
        std::string id = match_id(name);
        if (id.empty())
            return;
        auto it = g_cache.find(id);
        if (it != g_cache.end()) {
            send(path, it->second);
            return;
        }
        if (!g_asked.insert(id).second)
            return;
        web::fetch_page(MATCH_URL + text::wide(id), [id, path](bool ok, const std::string& body) {
            std::vector<player_t> ps;
            std::string region;
            if (!ok || !parse(body, ps, region)) {
                dbg::print("[faceit] %s: no roster", id.c_str());
                return;
            }
            g_cache[id] = ps;
            dbg::print("[faceit] %s: %zu players", id.c_str(), ps.size());
            send(path, ps);
            fetch_ranks(id, path, region);
        });
    }

    void history_page(const std::string& steamid, const std::string& uid, int elo, int page, std::shared_ptr<std::string> items) {
        std::wstring url = HISTORY_URL + text::wide(uid + "/games/cs2?page=" + std::to_string(page) + "&size=" + std::to_string(HISTORY_PAGE));
        web::fetch_page(url, [steamid, uid, elo, page, items](bool ok, const std::string& body) {
            json_t r;
            std::string err;
            size_t got = 0;
            if (ok && json::parse(body, r, err) && r.kind == jk_arr) {
                got = r.arr.size();
                for (const json_t& m : r.arr) {
                    std::string id = json::str(json::at(m, "matchId")), map = json::str(json::at(m, "i1")), score = json::str(json::at(m, "i18"));
                    int after = atoi(json::str(json::at(m, "elo")).c_str());
                    if (id.empty() || after <= 0)
                        continue;
                    int a = atoi(score.c_str()), b = 0;
                    size_t slash = score.find('/');
                    if (slash != std::string::npos)
                        b = atoi(score.c_str() + slash + 1);
                    *items += std::string(items->empty() ? "" : ",") + "[" + std::to_string((long long)(json::num(json::at(m, "date")) / 1000)) + "," + std::to_string(after) + "," +
                              std::to_string(atoi(json::str(json::at(m, "elo_delta")).c_str())) + ",\"" + json::escape(text::lower(id)) + "\",\"" + json::escape(map) + "\"," +
                              (json::str(json::at(m, "i10")) == "1" ? "1" : "0") + "," + std::to_string(a) + "," + std::to_string(b) + "]";
                }
            }
            if (got == (size_t)HISTORY_PAGE && page + 1 < HISTORY_PAGES) {
                history_page(steamid, uid, elo, page + 1, items);
                return;
            }
            g_history_busy = false;
            g_history_for = steamid;
            g_history = "{\"type\":\"faceit_history\",\"steamid\":\"" + json::escape(steamid) + "\",\"elo\":" + std::to_string(elo) + ",\"items\":[" + *items + "]}";
            dbg::print("[faceit] history: %d pages, elo %d", page + 1, elo);
            window::post_json(g_history);
        });
    }

    void history(const std::string& steamid) {
        if (g_history_for == steamid && !g_history.empty()) {
            window::post_json(g_history);
            return;
        }
        if (g_history_busy || steamid.empty())
            return;
        std::string nick;
        for (const auto& [id, ps] : g_cache)
            for (const player_t& p : ps)
                if (p.steamid == steamid)
                    nick = p.nick;
        for (char c : nick)
            if (!isalnum((unsigned char)c) && c != '_' && c != '-')
                nick.clear();
        if (nick.empty())
            return;
        g_history_busy = true;
        web::fetch_page(USER_URL + text::wide(nick), [steamid](bool ok, const std::string& body) {
            json_t r;
            std::string err;
            const json_t* payload = ok && json::parse(body, r, err) ? json::at(r, "payload") : nullptr;
            const json_t* games = payload ? json::at(*payload, "games") : nullptr;
            const json_t* cs2 = games ? json::at(*games, "cs2") : nullptr;
            std::string uid = payload ? json::str(json::at(*payload, "id")) : "";
            if (!cs2 || uid.empty() || json::str(json::at(*cs2, "game_id")) != steamid) {
                g_history_busy = false;
                dbg::print("[faceit] history: no profile");
                return;
            }
            history_page(steamid, uid, (int)json::num(json::at(*cs2, "faceit_elo")), 0, std::make_shared<std::string>());
        });
    }

    void clear() {
        g_cache.clear();
        g_asked.clear();
        g_history.clear();
        g_history_for.clear();
        DeleteFileW(g_cache_path.c_str());
    }
}
