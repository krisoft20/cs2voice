#pragma once

#include "model/match.h"

struct shots_t {
    int first = 0;
    int first_hits = 0;
    int stops = 0;
    int stopped = 0;
};

struct swings_t {
    int close = 0;
    int good = 0;
};

namespace input {
    double fps(const match_t& m, int slot);
    double sens(const match_t& m, int slot);
    shots_t shots(const match_t& m, int slot);
    swings_t swings(const match_t& m, int slot);
}
