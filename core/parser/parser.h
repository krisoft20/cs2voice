#pragma once

#include "parser/demo/demo.h"
#include "parser/entities/entities.h"
#include "parser/events/bullets.h"
#include "parser/events/game_events.h"
#include "parser/proto/packet.h"
#include "parser/sendtables/layout.h"
#include "parser/sendtables/serializers.h"
#include "parser/tables/string_tables.h"
#include "parser/usercmd/usercmd.h"

struct parser_t;

typedef void (*frame_fn_t)(parser_t& p, void* user);
typedef void (*msg_fn_t)(parser_t& p, const msg_t& m, void* user);
typedef void (*event_fn_t)(parser_t& p, const game_event_t& e, void* user);
typedef void (*usercmd_fn_t)(parser_t& p, const usercmd_t& c, void* user);
typedef void (*bullet_fn_t)(parser_t& p, const bullet_t& b, void* user);

struct callbacks_t {
    void* user = nullptr;
    frame_fn_t on_frame = nullptr;
    msg_fn_t on_msg = nullptr;
    event_fn_t on_event = nullptr;
    usercmd_fn_t on_usercmd = nullptr;
    bullet_fn_t on_bullet = nullptr;
};

struct fanout_t {
    callbacks_t list[4];
    int n = 0;
    const std::atomic<bool>* cancel = nullptr;
};

struct parser_t {
    demo_t demo;
    serializers_t ser;
    layouts_t lay;
    string_tables_t tables;
    entities_t ents;
    game_events_t events;
    usercmds_t cmds;
    bullet_t bullet;
    packet_t pkt;
    int tick = 0;
    int cmd = 0;
    uint32_t frames = 0;
    int32_t max_classes = 0;
    float tick_interval = 0.f;
    int baseline_table = -1;
    bool have_ser = false;
    bool ready = false;
    bool stop = false;
    bool skip_entities = false;
    bool use_full = false;
    std::vector<std::pair<std::string, size_t>> errors;
    std::string err;
};

namespace parser {
    bool open(parser_t& p, const char* path);
    bool run(parser_t& p, const callbacks_t& cb);
    bool resume(parser_t& p, const callbacks_t& cb);
    bool seek_last_full(parser_t& p);
    void note_error(parser_t& p, const std::string& what);
    void add(fanout_t& f, const callbacks_t& cb);
    callbacks_t fan(fanout_t& f);
}
