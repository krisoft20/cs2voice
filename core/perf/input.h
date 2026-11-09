#pragma once

#include "model/match.h"

struct shot_sample_t {
    int tick = 0;
    bool good = false;
    uint32_t item = 0;
    float speed = 0;
    float limit = 0;
    int other = -1;
    int damage = 0;
    float off = -1;
    bool head = false;
    bool heard = false;
    bool kill = false;
    int ticks = 0;
};

struct shots_t {
    int first = 0;
    int first_hits = 0;
    int stops = 0;
    int stopped = 0;
    std::vector<shot_sample_t> first_at;
    std::vector<shot_sample_t> stop_at;
};

struct swings_t {
    int total = 0;
    int good = 0;
    std::vector<shot_sample_t> at;
};

namespace input {
    double fps(const match_t& m, int slot);
    double sens(const match_t& m, int slot);
    shots_t shots(const match_t& m, int slot);
    swings_t swings(const match_t& m, int slot);
}
