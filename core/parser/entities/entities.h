#pragma once

#include "parser/sendtables/field_path.h"
#include "parser/sendtables/layout.h"
#include "parser/sendtables/serializers.h"

struct value_t {
    uint32_t w[3];
    uint32_t stamp;
};

struct entity_t {
    int32_t index = -1;
    int32_t cls = -1;
    int32_t serial = 0;
    int32_t layout = -1;
    bool active = false;
    std::vector<value_t> slots;
    std::vector<std::vector<value_t>> dyn;
    std::vector<std::string> strs;
};

struct ent_stats_t {
    size_t created = 0;
    size_t updated = 0;
    size_t deleted = 0;
    size_t left = 0;
    size_t failed = 0;
    size_t baseline_failed = 0;
    size_t fields = 0;
};

struct entities_t {
    const serializers_t* ser = nullptr;
    const layouts_t* lay = nullptr;
    std::vector<entity_t> list;
    std::vector<entity_t> baselines;
    std::vector<uint8_t> baseline_state;
    std::vector<std::vector<uint8_t>> baseline_data;
    std::vector<fpath_t> paths;
    std::vector<int32_t> touched;
    int cls_bits = 0;
    uint32_t stamp = 1;
    ent_stats_t stats;
    std::string err;
};

namespace entities {
    void init(entities_t& es, const serializers_t& s, const layouts_t& l, int cls_bits);
    void set_baseline(entities_t& es, int cls, const uint8_t* data, size_t n);
    bool packet(entities_t& es, const uint8_t* msg, size_t n);
    void reset(entities_t& es);
    const entity_t* get(const entities_t& es, int index);
}
