#pragma once

#include "geo/map.h"
#include "model/match.h"

struct vis_t {
    int stride = 4;
    int ticks = 0;
    std::vector<uint64_t> los;
};

namespace vis {
    void build(vis_t& v, const match_t& m, const map_geo_t& g, int stride);
    void set_threads(int n);
    uint64_t seen(const vis_t& v, int tick, int slot);
    bool visible(const match_t& m, const map_geo_t& g, int tick, int viewer, int target);
    bool smoked(const match_t& m, int tick, const fvec_t& a, const fvec_t& b);
}
