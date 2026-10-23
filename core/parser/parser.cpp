#include "pch.h"

#include "parser/parser.h"
#include "parser/proto/proto.h"
#include "parser/sendtables/field_path.h"

namespace parser {

void note_error(parser_t& p, const std::string& what) {
    for (auto& e : p.errors) {
        if (e.first == what) {
            e.second++;
            return;
        }
    }
    p.errors.push_back({what, 1});
}

bool open(parser_t& p, const char* path) {
    field_path::init();
    if (!demo::open(p.demo, path)) {
        p.err = p.demo.err;
        return false;
    }
    return true;
}

static void push_baselines(parser_t& p, const std::vector<int>& changed) {
    if (!p.ready || p.baseline_table < 0)
        return;
    const string_table_t& t = p.tables.list[p.baseline_table];
    for (int i : changed) {
        const table_entry_t& e = t.entries[i];
        if (e.key.empty() || e.data.empty())
            continue;
        char* end = nullptr;
        long cls = strtol(e.key.c_str(), &end, 10);
        if (end && !*end && cls >= 0)
            entities::set_baseline(p.ents, (int)cls, e.data.data(), e.data.size());
    }
}

static void push_all_baselines(parser_t& p) {
    std::vector<int> all;
    for (size_t i = 0; i < p.tables.list[p.baseline_table].entries.size(); i++)
        all.push_back((int)i);
    push_baselines(p, all);
}

static int class_bits(parser_t& p) {
    if (p.max_classes > 0) {
        int bits = 0;
        while ((1 << (bits + 1)) <= p.max_classes)
            bits++;
        return bits + 1;
    }
    int bits = 1;
    while ((1 << bits) < (int)p.ser.classes.size())
        bits++;
    return bits;
}

static bool setup(parser_t& p) {
    std::string err;
    if (!layout::build(p.lay, p.ser, err)) {
        p.err = "layout: " + err;
        return false;
    }
    entities::init(p.ents, p.ser, p.lay, class_bits(p));
    p.ready = true;
    if (p.baseline_table >= 0)
        push_all_baselines(p);
    return true;
}

static void server_info(parser_t& p, const msg_t& m) {
    proto_t r = proto::make(m.data, m.len);
    proto_field_t f;
    while (proto::next(r, f))
        if (f.num == 11 && f.wire == 0)
            p.max_classes = proto::i32(f);
        else if (f.num == 13 && f.wire == 5)
            p.tick_interval = proto::f32(f);
}

static void table_msg(parser_t& p, const msg_t& m) {
    std::string err;
    int index = -1;
    bool ok = m.type == svc_create_string_table
                  ? string_tables::create(p.tables, m.data, m.len, index, err)
                  : string_tables::update(p.tables, m.data, m.len, index, err);
    if (!ok) {
        note_error(p, "string table: " + err);
        return;
    }
    if (index >= 0 && p.tables.list[index].name == "instancebaseline") {
        p.baseline_table = index;
        push_baselines(p, p.tables.changed);
    }
}

static void packet_frame(parser_t& p, const frame_t& f, const callbacks_t& cb) {
    if (!packet::begin(p.pkt, f.data, f.len)) {
        note_error(p, "bad packet frame");
        return;
    }
    msg_t m;
    std::string err;
    while (packet::next(p.pkt, m)) {
        switch (m.type) {
        case svc_server_info:
            server_info(p, m);
            break;
        case svc_create_string_table:
        case svc_update_string_table:
            table_msg(p, m);
            break;
        case svc_clear_all_string_tables:
            string_tables::clear(p.tables);
            p.baseline_table = -1;
            break;
        case svc_packet_entities:
            if (p.skip_entities)
                break;
            if (!p.ready) {
                note_error(p, "entities before class info");
                break;
            }
            p.ents.stamp++;
            if (!entities::packet(p.ents, m.data, m.len))
                note_error(p, p.ents.err);
            break;
        case ge_game_event_list:
            if (!game_events::load_list(p.events, m.data, m.len, err))
                note_error(p, err);
            break;
        case ge_game_event:
            if (!cb.on_event)
                break;
            if (game_events::parse(p.events, m.data, m.len, f.tick, err))
                cb.on_event(p, p.events.cur, cb.user);
            else
                note_error(p, "game event: " + err);
            break;
        case svc_user_cmds:
            if (!cb.on_usercmd)
                break;
            if (!usercmd::decode(p.cmds, m.data, m.len, f.tick))
                note_error(p, "bad user commands");
            for (int32_t slot : p.cmds.out)
                cb.on_usercmd(p, p.cmds.state[slot], cb.user);
            break;
        case te_fire_bullets:
            if (!cb.on_bullet)
                break;
            if (bullets::decode(p.bullet, m.data, m.len, f.tick))
                cb.on_bullet(p, p.bullet, cb.user);
            else
                note_error(p, "bad fire bullets");
            break;
        }
        if (cb.on_msg)
            cb.on_msg(p, m, cb.user);
    }
    if (p.pkt.bad)
        note_error(p, "truncated packet");
}

static void full_frame(parser_t& p, const frame_t& f, const callbacks_t& cb) {
    proto_t r = proto::make(f.data, f.len);
    proto_field_t pf;
    frame_t inner = f;
    bool have = false;
    std::string err;
    while (proto::next(r, pf)) {
        if (pf.num == 1 && pf.wire == 2) {
            if (!string_tables::load(p.tables, pf.data, pf.len, err)) {
                note_error(p, err);
            } else if (p.baseline_table >= 0) {
                push_all_baselines(p);
            }
        } else if (pf.num == 2 && pf.wire == 2) {
            inner.data = pf.data;
            inner.len = pf.len;
            have = true;
        }
    }
    if (r.bad || !have) {
        note_error(p, "bad full packet");
        return;
    }
    entities::reset(p.ents);
    packet_frame(p, inner, cb);
}

bool seek_last_full(parser_t& p) {
    size_t at = 0;
    if (!p.ready || !demo::last_full(p.demo, at))
        return false;
    p.demo.pos = at;
    p.use_full = true;
    return true;
}

bool run(parser_t& p, const callbacks_t& cb) {
    demo::rewind(p.demo);
    return resume(p, cb);
}

bool resume(parser_t& p, const callbacks_t& cb) {
    frame_t f;
    std::string err;
    while (!p.stop && demo::next(p.demo, f)) {
        p.tick = f.tick;
        p.cmd = f.cmd;
        switch (f.cmd) {
        case dem_send_tables:
            if (!serializers::parse(p.ser, f.data, f.len, err)) {
                p.err = "send tables: " + err;
                return false;
            }
            p.have_ser = true;
            break;
        case dem_class_info:
            if (!serializers::parse_classes(p.ser, f.data, f.len, err)) {
                p.err = "class info: " + err;
                return false;
            }
            if (!p.have_ser) {
                p.err = "class info before send tables";
                return false;
            }
            if (!setup(p))
                return false;
            break;
        case dem_packet:
        case dem_signon_packet:
            p.frames++;
            packet_frame(p, f, cb);
            if (cb.on_frame)
                cb.on_frame(p, cb.user);
            break;
        case dem_full_packet:
            p.frames++;
            if (p.use_full)
                full_frame(p, f, cb);
            if (cb.on_frame)
                cb.on_frame(p, cb.user);
            break;
        }
    }
    if (!p.demo.err.empty()) {
        p.err = p.demo.err;
        return false;
    }
    return true;
}

static void fan_frame(parser_t& p, void* user) {
    fanout_t& f = *(fanout_t*)user;
    if (f.cancel && *f.cancel)
        p.stop = true;
    for (int i = 0; i < f.n; i++)
        if (f.list[i].on_frame)
            f.list[i].on_frame(p, f.list[i].user);
}

static void fan_msg(parser_t& p, const msg_t& m, void* user) {
    fanout_t& f = *(fanout_t*)user;
    for (int i = 0; i < f.n; i++)
        if (f.list[i].on_msg)
            f.list[i].on_msg(p, m, f.list[i].user);
}

static void fan_event(parser_t& p, const game_event_t& e, void* user) {
    fanout_t& f = *(fanout_t*)user;
    for (int i = 0; i < f.n; i++)
        if (f.list[i].on_event)
            f.list[i].on_event(p, e, f.list[i].user);
}

static void fan_usercmd(parser_t& p, const usercmd_t& c, void* user) {
    fanout_t& f = *(fanout_t*)user;
    for (int i = 0; i < f.n; i++)
        if (f.list[i].on_usercmd)
            f.list[i].on_usercmd(p, c, f.list[i].user);
}

static void fan_bullet(parser_t& p, const bullet_t& b, void* user) {
    fanout_t& f = *(fanout_t*)user;
    for (int i = 0; i < f.n; i++)
        if (f.list[i].on_bullet)
            f.list[i].on_bullet(p, b, f.list[i].user);
}

void add(fanout_t& f, const callbacks_t& cb) {
    if (f.n < 4)
        f.list[f.n++] = cb;
}

callbacks_t fan(fanout_t& f) {
    callbacks_t cb;
    cb.user = &f;
    cb.on_frame = fan_frame;
    for (int i = 0; i < f.n; i++) {
        if (f.list[i].on_msg)
            cb.on_msg = fan_msg;
        if (f.list[i].on_event)
            cb.on_event = fan_event;
        if (f.list[i].on_usercmd)
            cb.on_usercmd = fan_usercmd;
        if (f.list[i].on_bullet)
            cb.on_bullet = fan_bullet;
    }
    return cb;
}

}
