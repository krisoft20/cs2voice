#pragma once

#include "demos/stats.h"

struct swing_state_t {
    int ct = 0;
    int t = 0;
    bool planted = false;
    float eco = 0.f;
    float clock = 0.f;
    int map = -1;
};

struct swing_sample_t {
    swing_state_t state;
    bool ct_won = false;
};

struct swing_view_t {
    player_stats_t* players[stats_t::SLOTS] = {};
    const std::vector<int>* winners = nullptr;
    std::vector<round_info_t>* infos = nullptr;
    float interval = 0.f;
    std::string map;
};

namespace swing {
    int map_index(const std::string& name);
    bool settled(const swing_state_t& st);
    double chance(const swing_state_t& st);
    void compute(swing_view_t& v);
    void samples(const swing_view_t& v, std::vector<swing_sample_t>& out);
}
