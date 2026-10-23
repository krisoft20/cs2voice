#pragma once

enum ev_type_t {
    ev_string = 1,
    ev_float = 2,
    ev_long = 3,
    ev_short = 4,
    ev_byte = 5,
    ev_bool = 6,
    ev_uint64 = 7,
};

struct ev_desc_t {
    std::string name;
    std::vector<std::string> keys;
    std::vector<int> types;
};

struct ev_value_t {
    int type = 0;
    int64_t i = 0;
    uint64_t u = 0;
    float f = 0.f;
    std::string s;
};

struct game_event_t {
    int id = -1;
    int tick = 0;
    const ev_desc_t* desc = nullptr;
    std::vector<ev_value_t> vals;
};

struct game_events_t {
    std::vector<ev_desc_t> descs;
    game_event_t cur;
};

namespace game_events {
    bool load_list(game_events_t& ge, const uint8_t* msg, size_t n, std::string& err);
    bool parse(game_events_t& ge, const uint8_t* msg, size_t n, int tick, std::string& err);
    int key(const game_event_t& e, const char* name);
    int64_t num(const game_event_t& e, const char* name, int64_t def);
    float flt(const game_event_t& e, const char* name, float def);
    std::string str(const game_event_t& e, const char* name);
    std::string format(const game_event_t& e);
}
