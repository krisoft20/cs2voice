#include "pch.h"

#include "parser/events/game_events.h"
#include "parser/proto/proto.h"

namespace game_events {

bool load_list(game_events_t& ge, const uint8_t* msg, size_t n, std::string& err) {
    size_t added = 0;
    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num != 1 || f.wire != 2)
            continue;
        int id = -1;
        ev_desc_t d;
        proto_t dr = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(dr, g)) {
            if (g.num == 1 && g.wire == 0) {
                id = proto::i32(g);
            } else if (g.num == 2 && g.wire == 2) {
                d.name = proto::str(g);
            } else if (g.num == 3 && g.wire == 2) {
                std::string key;
                int type = ev_long;
                proto_t kr = proto::make(g.data, g.len);
                proto_field_t h;
                while (proto::next(kr, h)) {
                    if (h.num == 1 && h.wire == 0)
                        type = proto::i32(h);
                    else if (h.num == 2 && h.wire == 2)
                        key = proto::str(h);
                }
                d.keys.push_back(key);
                d.types.push_back(type);
            }
        }
        if (id < 0 || id > 65535 || d.name.empty())
            continue;
        if ((size_t)id >= ge.descs.size())
            ge.descs.resize(id + 1);
        ge.descs[id] = std::move(d);
        added++;
    }
    if (r.bad || !added) {
        err = "bad game event list";
        return false;
    }
    return true;
}

bool parse(game_events_t& ge, const uint8_t* msg, size_t n, int tick, std::string& err) {
    game_event_t& e = ge.cur;
    e.id = -1;
    e.tick = tick;
    e.desc = nullptr;
    e.vals.clear();

    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 2 && f.wire == 0) {
            e.id = proto::i32(f);
            continue;
        }
        if (f.num != 3 || f.wire != 2)
            continue;
        ev_value_t v;
        proto_t kr = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(kr, g)) {
            switch (g.num) {
            case 2:
                if (g.wire == 2) {
                    v.type = ev_string;
                    v.s = proto::str(g);
                }
                break;
            case 3:
                if (g.wire == 5) {
                    v.type = ev_float;
                    v.f = proto::f32(g);
                }
                break;
            case 4:
            case 5:
            case 6:
                if (g.wire == 0) {
                    v.type = g.num == 4 ? ev_long : g.num == 5 ? ev_short : ev_byte;
                    v.i = (int64_t)g.val;
                }
                break;
            case 7:
                if (g.wire == 0) {
                    v.type = ev_bool;
                    v.i = g.val != 0;
                }
                break;
            case 8:
                if (g.wire == 0) {
                    v.type = ev_uint64;
                    v.u = g.val;
                }
                break;
            }
        }
        e.vals.push_back(std::move(v));
    }
    if (r.bad) {
        err = "bad game event";
        return false;
    }
    if (e.id < 0 || (size_t)e.id >= ge.descs.size() || ge.descs[e.id].name.empty()) {
        err = "no descriptor for game event " + std::to_string(e.id);
        return false;
    }
    e.desc = &ge.descs[e.id];
    if (e.vals.size() > e.desc->keys.size())
        e.vals.resize(e.desc->keys.size());
    return true;
}

int key(const game_event_t& e, const char* name) {
    if (!e.desc)
        return -1;
    for (size_t i = 0; i < e.vals.size(); i++)
        if (e.desc->keys[i] == name)
            return (int)i;
    return -1;
}

int64_t num(const game_event_t& e, const char* name, int64_t def) {
    int k = key(e, name);
    if (k < 0)
        return def;
    const ev_value_t& v = e.vals[k];
    switch (v.type) {
    case ev_float:
        return (int64_t)v.f;
    case ev_uint64:
        return (int64_t)v.u;
    case ev_string:
    case 0:
        return def;
    }
    return v.i;
}

float flt(const game_event_t& e, const char* name, float def) {
    int k = key(e, name);
    if (k < 0)
        return def;
    const ev_value_t& v = e.vals[k];
    if (v.type == ev_float)
        return v.f;
    if (v.type == ev_uint64)
        return (float)v.u;
    if (v.type == ev_string || !v.type)
        return def;
    return (float)v.i;
}

std::string str(const game_event_t& e, const char* name) {
    int k = key(e, name);
    if (k < 0 || e.vals[k].type != ev_string)
        return std::string();
    return e.vals[k].s;
}

std::string format(const game_event_t& e) {
    std::string out = e.desc ? e.desc->name : "?";
    char buf[64];
    for (size_t i = 0; i < e.vals.size(); i++) {
        const ev_value_t& v = e.vals[i];
        out += ' ';
        out += e.desc->keys[i];
        out += '=';
        switch (v.type) {
        case ev_string:
            out += v.s;
            continue;
        case ev_float:
            snprintf(buf, sizeof(buf), "%.4f", (double)v.f);
            break;
        case ev_bool:
            snprintf(buf, sizeof(buf), "%s", v.i ? "true" : "false");
            break;
        case ev_uint64:
            snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v.u);
            break;
        case 0:
            snprintf(buf, sizeof(buf), "-");
            break;
        default:
            snprintf(buf, sizeof(buf), "%lld", (long long)v.i);
        }
        out += buf;
    }
    return out;
}

}
