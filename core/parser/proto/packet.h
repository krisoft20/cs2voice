#pragma once

#include "parser/core/bits.h"

enum msg_id_t {
    net_tick = 4,
    svc_server_info = 40,
    svc_create_string_table = 44,
    svc_update_string_table = 45,
    svc_voice_init = 46,
    svc_voice_data = 47,
    svc_clear_all_string_tables = 51,
    svc_packet_entities = 55,
    svc_user_cmds = 76,
    ge_game_event_list = 205,
    ge_game_event = 207,
    ge_sos_start_sound = 208,
    te_fire_bullets = 452,
};

struct msg_t {
    uint32_t type = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

struct packet_t {
    bits_t b;
    std::vector<uint8_t> scratch;
    bool bad = false;
};

namespace packet {
    bool begin(packet_t& p, const uint8_t* frame, size_t n);
    bool next(packet_t& p, msg_t& m);
}
