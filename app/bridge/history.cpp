#include "pch.h"

#include "bridge/history.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/window.h"

namespace history {
    const char* FORMAT = "history 1";

    std::mutex g_mu;
    std::wstring g_path;
    std::vector<std::string> g_lines;
    std::set<std::string> g_keys;

    bool init(const std::wstring& path) {
        g_path = path;
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) {
            f = _wfopen(path.c_str(), L"wb");
            if (f) {
                fprintf(f, "%s\n", FORMAT);
                fclose(f);
            }
            dbg::print("[history] new file");
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
            dbg::print("[history] unknown format, left as it is");
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
            if (key.empty() || !g_keys.insert(key).second)
                continue;
            g_lines.push_back(line);
        }
        dbg::print("[history] %zu matches", g_lines.size());
        return true;
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
        std::string key = map + "|";
        for (size_t i = 0; i < ids.size(); i++)
            key += (i ? "," : "") + ids[i];
        key += "|";
        for (const json_t& w : winners->arr)
            key += std::to_string((int)w.num);

        std::string line = "{\"key\":\"" + json::escape(key) + "\",\"date\":" + std::to_string(mtime) + ",\"path\":\"" + json::escape(json::str(json::at(v, "path"))) +
                           "\",\"map\":\"" + json::escape(map) + "\",\"score\":[" + std::to_string((int)json::num(json::at(teams->arr[0], "score"))) + "," +
                           std::to_string((int)json::num(json::at(teams->arr[1], "score"))) + "],\"players\":[";
        for (size_t i = 0; i < players->arr.size(); i++) {
            const json_t& p = players->arr[i];
            const json_t* st = json::at(p, "stats");
            int k = st ? (int)json::num(json::at(*st, "kills")) : 0;
            int d = st ? (int)json::num(json::at(*st, "deaths")) : 0;
            int a = st ? (int)json::num(json::at(*st, "assists")) : 0;
            line += std::string(i ? "," : "") + "[\"" + json::escape(json::str(json::at(p, "steamid"))) + "\",\"" + json::escape(json::str(json::at(p, "name"))) + "\"," +
                    std::to_string((int)json::num(json::at(p, "team"), -1)) + "," + std::to_string(k) + "," + std::to_string(d) + "," + std::to_string(a) + "]";
        }
        line += "]}";

        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_keys.insert(key).second)
                return;
            g_lines.push_back(line);
            FILE* f = _wfopen(g_path.c_str(), L"ab");
            if (f) {
                fprintf(f, "%s\n", line.c_str());
                fclose(f);
            }
        }
        if (!quiet)
            window::post_json("{\"type\":\"history_add\",\"match\":" + line + "}");
    }

    std::string json() {
        std::lock_guard<std::mutex> lock(g_mu);
        std::string out = "{\"type\":\"history\",\"matches\":[";
        for (size_t i = 0; i < g_lines.size(); i++)
            out += (i ? "," : "") + g_lines[i];
        return out + "]}";
    }
}
