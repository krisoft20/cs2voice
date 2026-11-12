#include "pch.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>

#include "bridge/gsi.h"
#include "bridge/replays.h"
#include "dbg.h"
#include "demos/finder.h"
#include "json/json.h"
#include "text.h"

namespace gsi {
    const unsigned short PORT = 37218;
    const char* TOKEN = "cs2voice";
    const size_t MAX_REQUEST = 1 << 20;
    const DWORD READ_TIMEOUT_MS = 2000;
    const double REFRESH_GAP = 20;
    const double STALE_GAP = 1800;
    const double PLAY_STALE = 45;

    const char* CFG = R"("cs2voice"
{
    "uri" "http://127.0.0.1:37218"
    "timeout" "5.0"
    "buffer" "0.1"
    "throttle" "0.5"
    "heartbeat" "30.0"
    "auth"
    {
        "token" "cs2voice"
    }
    "data"
    {
        "provider" "1"
        "map" "1"
        "round" "1"
        "player_id" "1"
        "player_state" "1"
        "player_match_stats" "1"
    }
}
)";

    std::mutex g_mu;
    match_t g_cur;
    std::string g_key;
    std::string g_phase;
    std::string g_round_phase;
    std::string g_bomb;
    std::string g_team;
    round_t g_round;
    bool g_in_round = false;
    std::atomic<double> g_heard = 0;
    double g_refreshed = 0;
    std::thread g_thread;
    std::atomic<bool> g_quit = false;
    std::atomic<SOCKET> g_listen = INVALID_SOCKET;

    double now() {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::string pretty_map(const std::string& name) {
        std::string s = name;
        size_t u = s.find('_');
        if (u != std::string::npos && u <= 2)
            s = s.substr(u + 1);
        if (s == "dust2")
            return "Dust II";
        if (!s.empty())
            s[0] = (char)toupper((unsigned char)s[0]);
        return s;
    }

    std::string pretty_mode(const std::string& mode) {
        if (mode == "scrimcomp2v2")
            return "Wingman";
        if (mode == "gungameprogressive")
            return "Arms Race";
        std::string s = mode;
        if (!s.empty())
            s[0] = (char)toupper((unsigned char)s[0]);
        return s;
    }

    void install() {
        std::string dir = finder::game_dir();
        if (dir.empty())
            return;
        std::wstring path = text::wide(dir + "\\cfg\\gamestate_integration_cs2voice.cfg");
        std::string have;
        if (FILE* f = _wfopen(path.c_str(), L"rb")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                have.append(buf, n);
            fclose(f);
        }
        if (have == CFG)
            return;
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (!f) {
            dbg::print("[gsi] cant write %s", text::utf8(path).c_str());
            return;
        }
        fwrite(CFG, 1, strlen(CFG), f);
        fclose(f);
        dbg::print("[gsi] installed %s, cs2 reads it on the next start", text::utf8(path).c_str());
    }

    void mark(double at, const std::string& icon, const std::string& title) {
        g_cur.marks.push_back({at, icon, title});
    }

    int side_of(const std::string& team) {
        return team == "CT" ? 3 : team == "T" ? 2 : 0;
    }

    int reason_of(const std::string& why) {
        if (why.find("defuse") != std::string::npos)
            return 7;
        if (why.find("bomb") != std::string::npos)
            return 1;
        if (why.find("time") != std::string::npos)
            return 12;
        if (why.find("elimination") != std::string::npos)
            return 9;
        return 0;
    }

    void keep_round(const round_t& r) {
        for (round_t& x : g_cur.rounds)
            if (x.n == r.n) {
                x = r;
                return;
            }
        g_cur.rounds.push_back(r);
    }

    void handle(const std::string& body) {
        json_t j;
        std::string err;
        if (!json::parse(body, j, err))
            return;
        const json_t* auth = json::at(j, "auth");
        if (!auth || json::str(json::at(*auth, "token")) != TOKEN)
            return;
        double t = now();
        g_heard = t;
        const json_t* prov = json::at(j, "provider");
        std::string me = prov ? json::str(json::at(*prov, "steamid")) : "";
        const json_t* map = json::at(j, "map");
        const json_t* round = json::at(j, "round");
        const json_t* player = json::at(j, "player");
        bool refresh = false;
        bool over = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!map) {
                refresh = g_cur.on;
                g_cur.on = false;
                g_phase.clear();
                g_round_phase.clear();
                g_bomb.clear();
            } else {
                std::string name = json::str(json::at(*map, "name"));
                std::string mode = json::str(json::at(*map, "mode"));
                std::string phase = json::str(json::at(*map, "phase"));
                int done = (int)json::num(json::at(*map, "round"));
                std::string key = name + "|" + mode;
                bool fresh = g_cur.start == 0 || key != g_key || (g_phase == "gameover" && phase != "gameover") || t - g_cur.seen > STALE_GAP;
                if (fresh) {
                    g_cur = match_t{};
                    g_cur.start = t;
                    g_cur.whole = done == 0;
                    g_key = key;
                    g_team.clear();
                    g_round_phase.clear();
                    g_bomb.clear();
                    g_in_round = false;
                    refresh = true;
                    dbg::print("[gsi] match on %s (%s), round %d", name.c_str(), mode.c_str(), done);
                }
                g_cur.on = phase != "gameover";
                g_cur.seen = t;
                g_cur.map = pretty_map(name);
                g_cur.mode = pretty_mode(mode);
                bool own = player && !me.empty() && json::str(json::at(*player, "steamid")) == me;
                std::string rp = round ? json::str(json::at(*round, "phase")) : "";
                if (rp == "freezetime" && (!g_in_round || g_round.n != done + 1)) {
                    g_round = round_t{};
                    g_round.n = done + 1;
                    g_in_round = true;
                }
                if (own) {
                    std::string team = json::str(json::at(*player, "team"));
                    if (team == "CT" || team == "T")
                        g_team = team;
                    if (const json_t* ps = json::at(*player, "state"); ps && g_in_round) {
                        g_round.side = side_of(g_team);
                        if (rp == "freezetime")
                            g_round.money = (int)json::num(json::at(*ps, "money"), -1);
                        if (rp == "live" && g_round.equip < 0)
                            g_round.equip = (int)json::num(json::at(*ps, "equip_value"), -1);
                        g_round.kills = std::max(g_round.kills, (int)json::num(json::at(*ps, "round_kills")));
                        g_round.hs = std::max(g_round.hs, (int)json::num(json::at(*ps, "round_killhs")));
                        if (const json_t* d = json::at(*ps, "round_totaldmg"))
                            g_round.dmg = std::max(g_round.dmg, (int)json::num(d));
                    }
                    if (const json_t* st = json::at(*player, "match_stats")) {
                        int deaths = (int)json::num(json::at(*st, "deaths"));
                        if (g_cur.deaths >= 0 && deaths > g_cur.deaths)
                            mark(t, "cs2_death", "Death");
                        g_cur.kills = (int)json::num(json::at(*st, "kills"));
                        g_cur.assists = (int)json::num(json::at(*st, "assists"));
                        g_cur.deaths = deaths;
                    }
                }
                if (!g_team.empty()) {
                    const json_t* ct = json::at(*map, "team_ct");
                    const json_t* tt = json::at(*map, "team_t");
                    int cts = ct ? (int)json::num(json::at(*ct, "score")) : 0;
                    int ts = tt ? (int)json::num(json::at(*tt, "score")) : 0;
                    g_cur.ours = g_team == "CT" ? cts : ts;
                    g_cur.theirs = g_team == "CT" ? ts : cts;
                }
                if (round) {
                    if (g_round_phase == "freezetime" && rp == "live") {
                        mark(t, "cs2_" + std::to_string(done + 1), "Round " + std::to_string(done + 1));
                        refresh = refresh || t - g_refreshed > REFRESH_GAP;
                    }
                    if (rp == "over" && g_round_phase != "over" && g_in_round) {
                        g_round.winner = side_of(json::str(json::at(*round, "win_team")));
                        if (!g_round.side)
                            g_round.side = side_of(g_team);
                        keep_round(g_round);
                        g_in_round = false;
                        refresh = refresh || t - g_refreshed > REFRESH_GAP;
                    }
                    g_round_phase = rp;
                    std::string bomb = json::str(json::at(*round, "bomb"));
                    if (bomb != g_bomb) {
                        if (bomb == "planted")
                            mark(t, "cs2_bomb_plant", "Bomb planted");
                        else if (bomb == "defused")
                            mark(t, "cs2_bomb_defused", "Bomb defused");
                        else if (bomb == "exploded")
                            mark(t, "cs2_bomb_exploded", "Bomb exploded");
                    }
                    g_bomb = bomb;
                }
                if (const json_t* wins = json::at(*map, "round_wins"); wins && wins->kind == jk_obj)
                    for (const auto& [k, v] : wins->obj)
                        for (round_t& r : g_cur.rounds)
                            if (r.n == atoi(k.c_str())) {
                                r.reason = reason_of(v.str);
                                if (!r.winner)
                                    r.winner = v.str.rfind("ct_", 0) == 0 ? 3 : v.str.rfind("t_", 0) == 0 ? 2 : 0;
                            }
                over = phase == "gameover" && g_phase != "gameover";
                g_phase = phase;
            }
            if (refresh || over)
                g_refreshed = t;
        }
        if (over) {
            dbg::print("[gsi] match over");
            replays::nudge();
        } else if (refresh) {
            replays::refresh();
        }
    }

    bool alive() {
        return now() - g_heard < PLAY_STALE;
    }

    bool playing() {
        std::lock_guard<std::mutex> lock(g_mu);
        return g_cur.on && now() - g_cur.seen < PLAY_STALE;
    }

    size_t content_length(const std::string& head) {
        std::string h = text::lower(head);
        size_t at = h.find("content-length:");
        return at == std::string::npos ? 0 : (size_t)strtoull(h.c_str() + at + 15, nullptr, 10);
    }

    void reply(SOCKET c) {
        std::string req;
        char buf[8192];
        size_t body = std::string::npos, need = 0;
        while (req.size() < MAX_REQUEST) {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            req.append(buf, (size_t)n);
            if (body == std::string::npos) {
                size_t h = req.find("\r\n\r\n");
                if (h == std::string::npos)
                    continue;
                body = h + 4;
                need = content_length(req.substr(0, h));
            }
            if (req.size() >= body + need)
                break;
        }
        static const char ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(c, ok, (int)sizeof(ok) - 1, 0);
        if (body != std::string::npos && req.size() >= body + need)
            handle(req.substr(body, need));
    }

    void serve() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return;
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(PORT);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        if (bind(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR || listen(s, 4) == SOCKET_ERROR) {
            dbg::print("[gsi] cant listen on port %d, error %d", PORT, WSAGetLastError());
            closesocket(s);
            return;
        }
        g_listen = s;
        if (g_quit) {
            if (g_listen.exchange(INVALID_SOCKET) != INVALID_SOCKET)
                closesocket(s);
            return;
        }
        dbg::print("[gsi] listening on 127.0.0.1:%d", PORT);
        while (!g_quit) {
            SOCKET c = accept(s, nullptr, nullptr);
            if (c == INVALID_SOCKET)
                break;
            DWORD to = READ_TIMEOUT_MS;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
            reply(c);
            closesocket(c);
        }
    }

    void init() {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w))
            return;
        install();
        g_thread = std::thread(serve);
    }

    void cleanup() {
        g_quit = true;
        SOCKET s = g_listen.exchange(INVALID_SOCKET);
        if (s != INVALID_SOCKET)
            closesocket(s);
        if (g_thread.joinable())
            g_thread.join();
        WSACleanup();
    }

    bool match(match_t& out) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_cur.start == 0)
            return false;
        out = g_cur;
        return true;
    }
}
