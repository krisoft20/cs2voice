#pragma once

namespace gsi {
    struct mark_t {
        double at = 0;
        std::string icon;
        std::string title;
    };

    struct round_t {
        int n = 0;
        int side = 0;
        int winner = 0;
        int reason = 0;
        int kills = 0;
        int hs = 0;
        int dmg = -1;
        int money = -1;
        int equip = -1;
    };

    struct match_t {
        bool on = false;
        bool whole = false;
        double start = 0;
        double seen = 0;
        std::string map;
        std::string mode;
        int ours = -1;
        int theirs = -1;
        int kills = -1;
        int deaths = -1;
        int assists = -1;
        std::vector<mark_t> marks;
        std::vector<round_t> rounds;
    };

    void init();
    void cleanup();
    bool match(match_t& out);
    bool playing();
    bool alive();
}
