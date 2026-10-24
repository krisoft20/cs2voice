#pragma once

#include <atomic>

#include "demos/skins.h"
#include "demos/stats.h"

struct roster_player_t {
    int slot = -1;
    std::string name;
    std::string crosshair;
    int mute = 0;
    uint64_t steamid = 0;
    int team = -1;
    int voice = 0;
    int music = 0;
    player_stats_t stats;
    std::vector<skin_t> skins;
};

struct roster_team_t {
    int side = 0;
    std::string name;
    int score = 0;
};

struct roster_t {
    bool partial = false;
    bool ended = false;
    bool voice_known = false;
    bool pet_crash = false;
    std::string map;
    std::string server;
    int rounds = 0;
    int voice = 0;
    int duration = 0;
    roster_team_t teams[2];
    std::vector<roster_player_t> players;
    std::vector<int> winners;
    std::vector<round_info_t> rounds_info;
    float tick_interval = 0.f;
    std::string err;
};

typedef void (*early_fn_t)(const roster_t& r, void* user);

namespace reader {
    bool read(const std::string& path, roster_t& out, early_fn_t early = nullptr, void* user = nullptr, const std::atomic<bool>* cancel = nullptr);
    bool peek(const std::string& path, roster_t& out, const std::atomic<bool>* cancel = nullptr);
}
