#pragma once

#include "geo/map.h"
#include "model/match.h"
#include "model/vis.h"

enum perf_stat_t {
    ps_kd,
    ps_adr,
    ps_kast,
    ps_hs,
    ps_accuracy,
    ps_head_hits,
    ps_ttd,
    ps_reaction,
    ps_placement,
    ps_wallbang,
    ps_smoke,
    ps_count,
};

struct perf_sample_t {
    int tick = 0;
    int other = -1;
    double value = 0;
};

struct perf_t {
    int rounds = 0;
    int kast_rounds = 0;
    int shots = 0;
    int hits = 0;
    int head_hits = 0;
    int kills = 0;
    int wallbangs = 0;
    int smokes = 0;
    std::vector<double> ttd_ms;
    std::vector<double> reaction_ms;
    std::vector<double> placement_deg;
    std::vector<perf_sample_t> ttd_at;
    std::vector<perf_sample_t> reaction_at;
    std::vector<perf_sample_t> placement_at;
    double value[ps_count] = {};
    bool has[ps_count] = {};
};

namespace perf {
    extern const char* names[ps_count];
    std::vector<std::pair<int, int>> rounds(const match_t& m);
    bool gun(const std::string& weapon);
    void measure(const match_t& m, const map_geo_t* g, const vis_t* v, int slot, perf_t& out);
}
