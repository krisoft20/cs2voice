#include "pch.h"

#include "bridge/matches.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/window.h"

namespace matches {
    const char* FORMAT = "matches 1";
    const int RECORD = 4;

    std::mutex g_mu;
    std::wstring g_path;
    std::vector<std::string> g_lines;
    std::map<std::string, size_t> g_keys;
    std::set<std::string> g_old;

    void save() {
        FILE* f = _wfopen(g_path.c_str(), L"wb");
        if (!f)
            return;
        fprintf(f, "%s\n", FORMAT);
        for (const std::string& line : g_lines)
            fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }

    bool init(const std::wstring& path) {
        g_path = path;
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) {
            save();
            dbg::print("[matches] new file");
            return false;
        }
        std::string data;
        char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            data.append(buf, n);
        fclose(f);
        size_t at = data.find('\n');
        if (at == std::string::npos || data.compare(0, at, FORMAT) != 0) {
            dbg::print("[matches] unknown format, left as it is");
            return true;
        }
        at++;
        while (at < data.size()) {
            size_t end = data.find('\n', at);
            if (end == std::string::npos)
                end = data.size();
            std::string line = data.substr(at, end - at);
            at = end + 1;
            json_t v;
            std::string err;
            if (line.empty() || !json::parse(line, v, err))
                continue;
            std::string key = json::str(json::at(v, "key"));
            if (key.empty() || g_keys.count(key))
                continue;
            g_keys[key] = g_lines.size();
            g_lines.push_back(line);
            if ((int)json::num(json::at(v, "v")) < RECORD)
                g_old.insert(key);
        }
        dbg::print("[matches] %zu matches, %zu from an older version", g_lines.size(), g_old.size());
        return g_old.empty();
    }

    std::string number(double v) {
        if (v == (long long)v)
            return std::to_string((long long)v);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    }

    struct side_t {
        int rounds = 0, wins = 0, kills = 0, kast = 0;
        long long damage = 0, swing = 0;
    };

    std::string side_json(const char* name, const side_t& s, bool swing) {
        std::string pre = std::string(",\"") + name + "_";
        std::string out = pre + "rounds\":" + std::to_string(s.rounds) + pre + "wins\":" + std::to_string(s.wins) + pre + "kills\":" + std::to_string(s.kills) + pre +
                          "kast\":" + std::to_string(s.kast) + pre + "damage\":" + std::to_string(s.damage);
        if (swing)
            out += pre + "swing\":" + std::to_string(s.swing);
        return out;
    }

    std::string stats_json(const json_t& st, const json_t& winners) {
        std::string out = "{";
        for (const auto& [name, value] : st.obj)
            if (value.kind == jk_num)
                out += (out.size() > 1 ? ",\"" : "\"") + json::escape(name) + "\":" + number(value.num);
        const json_t* lines = json::at(st, "lines");
        long long swing = 0;
        int known = 0;
        side_t sides[2];
        for (size_t k = 0; lines && k < lines->arr.size(); k++) {
            const json_t& l = lines->arr[k];
            if (l.arr.size() < 5 || !l.arr[0].num)
                continue;
            int side = (int)l.arr[4].num;
            if (side != 2 && side != 3)
                continue;
            side_t& s = sides[side - 2];
            s.rounds++;
            s.wins += k < winners.arr.size() && (int)winners.arr[k].num == side;
            s.kills += (int)l.arr[1].num;
            s.damage += (long long)l.arr[3].num;
            s.kast += l.arr.size() > 5 && l.arr[5].num != 0;
            if (l.arr.size() > 6) {
                s.swing += (long long)l.arr[6].num;
                swing += (long long)l.arr[6].num;
                known++;
            }
        }
        std::string both = side_json("t", sides[0], known > 0) + side_json("ct", sides[1], known > 0);
        out += out.size() > 1 ? both : both.substr(1);
        if (known)
            out += ",\"swing\":" + std::to_string(swing);
        return out + "}";
    }

    std::string clutches(const json_t& st, const json_t& winners) {
        std::string out;
        const json_t* lines = json::at(st, "lines");
        for (size_t k = 0; lines && k < lines->arr.size() && k < winners.arr.size(); k++) {
            const json_t& l = lines->arr[k];
            if (l.arr.size() < 9 || !l.arr[0].num || l.arr[7].num <= 0)
                continue;
            out += std::string(out.empty() ? "" : ",") + "[" + std::to_string(k) + "," + std::to_string((int)l.arr[7].num) + "," +
                   ((int)winners.arr[k].num == (int)l.arr[4].num ? "1" : "0") + "," + std::to_string((int)l.arr[8].num) + "]";
        }
        return out;
    }

    std::map<int, std::map<std::string, int>> weapons(const json_t& v, const json_t& players) {
        std::map<int, std::map<std::string, int>> out;
        const json_t* info = json::at(v, "round_info");
        for (size_t k = 0; info && k < info->arr.size(); k++) {
            std::map<int, int> sides;
            for (const json_t& p : players.arr) {
                const json_t* st = json::at(p, "stats");
                const json_t* lines = st ? json::at(*st, "lines") : nullptr;
                if (lines && k < lines->arr.size() && lines->arr[k].arr.size() >= 5 && lines->arr[k].arr[0].num)
                    sides[(int)json::num(json::at(p, "slot"), -1)] = (int)lines->arr[k].arr[4].num;
            }
            const json_t* kills = json::at(info->arr[k], "kills");
            for (const json_t& kl : kills ? kills->arr : std::vector<json_t>{}) {
                if (kl.arr.size() < 6)
                    continue;
                int killer = (int)kl.arr[1].num, victim = (int)kl.arr[2].num;
                auto a = sides.find(killer), b = sides.find(victim);
                if (killer == victim || a == sides.end() || b == sides.end() || a->second == b->second)
                    continue;
                std::string w = kl.arr[5].str;
                if (w.find("knife") != std::string::npos || w.find("bayonet") != std::string::npos)
                    w = "knife";
                if (!w.empty())
                    out[killer][w]++;
            }
        }
        return out;
    }

    void note(const std::string& roster, int64_t mtime, bool quiet) {
        json_t v;
        std::string err;
        if (!json::parse(roster, v, err))
            return;
        const json_t* ok = json::at(v, "ok");
        const json_t* partial = json::at(v, "partial");
        const json_t* players = json::at(v, "players");
        const json_t* winners = json::at(v, "winners");
        const json_t* teams = json::at(v, "teams");
        if (!ok || !ok->b || !partial || partial->b || !players || players->kind != jk_arr || players->arr.empty() || !winners ||
            winners->kind != jk_arr || winners->arr.empty() || !teams || teams->kind != jk_arr || teams->arr.size() != 2)
            return;

        std::string map = json::str(json::at(v, "map"));
        std::vector<std::string> ids;
        for (const json_t& p : players->arr)
            ids.push_back(json::str(json::at(p, "steamid")));
        std::sort(ids.begin(), ids.end());
        std::string won;
        for (const json_t& w : winners->arr)
            won += std::to_string((int)w.num);
        std::string key = map + "|";
        for (size_t i = 0; i < ids.size(); i++)
            key += (i ? "," : "") + ids[i];
        key += "|" + won;

        std::string line = "{\"v\":" + std::to_string(RECORD) + ",\"key\":\"" + json::escape(key) + "\",\"date\":" + std::to_string(mtime) + ",\"path\":\"" +
                           json::escape(json::str(json::at(v, "path"))) + "\",\"map\":\"" + json::escape(map) + "\",\"server\":\"" +
                           json::escape(json::str(json::at(v, "server"))) + "\",\"duration\":" + std::to_string((int)json::num(json::at(v, "duration"))) +
                           ",\"rounds\":" + std::to_string((int)winners->arr.size()) + ",\"winners\":\"" + won + "\",\"score\":[" +
                           std::to_string((int)json::num(json::at(teams->arr[0], "score"))) + "," + std::to_string((int)json::num(json::at(teams->arr[1], "score"))) +
                           "],\"players\":[";
        std::map<int, std::map<std::string, int>> guns = weapons(v, *players);
        for (size_t i = 0; i < players->arr.size(); i++) {
            const json_t& p = players->arr[i];
            const json_t* st = json::at(p, "stats");
            std::string wp;
            for (const auto& [name, count] : guns[(int)json::num(json::at(p, "slot"), -1)])
                wp += std::string(wp.empty() ? "" : ",") + "\"" + json::escape(name) + "\":" + std::to_string(count);
            line += std::string(i ? "," : "") + "{\"steamid\":\"" + json::escape(json::str(json::at(p, "steamid"))) + "\",\"name\":\"" + json::escape(json::str(json::at(p, "name"))) +
                    "\",\"team\":" + std::to_string((int)json::num(json::at(p, "team"), -1)) + ",\"stats\":" + (st ? stats_json(*st, *winners) : "{}") + ",\"cl\":[" + (st ? clutches(*st, *winners) : "") + "],\"wp\":{" + wp + "}}";
        }
        line += "]}";

        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto it = g_keys.find(key);
            if (it != g_keys.end() && quiet && !g_old.count(key))
                return;
            g_old.erase(key);
            if (it != g_keys.end()) {
                if (g_lines[it->second] == line)
                    return;
                g_lines[it->second] = line;
                save();
            } else {
                g_keys[key] = g_lines.size();
                g_lines.push_back(line);
                FILE* f = _wfopen(g_path.c_str(), L"ab");
                if (f) {
                    fprintf(f, "%s\n", line.c_str());
                    fclose(f);
                }
            }
        }
        if (!quiet)
            window::post_json("{\"type\":\"matches_add\",\"match\":" + line + "}");
    }

    std::string json() {
        std::lock_guard<std::mutex> lock(g_mu);
        std::string out = "{\"type\":\"matches\",\"matches\":[";
        for (size_t i = 0; i < g_lines.size(); i++)
            out += (i ? "," : "") + g_lines[i];
        return out + "]}";
    }
}
