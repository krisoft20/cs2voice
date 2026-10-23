#include "pch.h"

#include "parser/proto/proto.h"
#include "parser/usercmd/usercmd.h"

namespace usercmd {

static proto_t delta(const uint8_t* p, size_t n) {
    proto_t r = proto::make(p, n);
    r.deltas = true;
    return r;
}

static bool reset(const proto_field_t& f) {
    return f.wire == 7;
}

static bool apply_angles(const uint8_t* p, size_t n, float* a, float* b, float* c) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        float* dst = f.num == 1 ? a : f.num == 2 ? b : f.num == 3 ? c : nullptr;
        if (dst)
            *dst = reset(f) ? 0.f : proto::f32(f);
    }
    return !r.bad;
}

template <typename T, typename F>
static bool apply_repeated(const uint8_t* p, size_t n, std::vector<T>& items, F apply) {
    const uint8_t* end = p + n;
    while (p < end) {
        uint64_t key;
        if (!proto::varint(p, end, key))
            return false;
        uint64_t idx = key >> 3;
        uint32_t wire = (uint32_t)(key & 7);
        if (idx > 1024)
            return false;
        if (wire == 7) {
            items.resize((size_t)idx);
            continue;
        }
        if (wire != 2)
            return false;
        uint64_t len;
        if (!proto::varint(p, end, len) || len > (uint64_t)(end - p))
            return false;
        if (idx >= items.size())
            items.resize((size_t)idx + 1);
        if (!apply(p, (size_t)len, items[(size_t)idx]))
            return false;
        p += len;
    }
    return true;
}

static bool apply_interp(const uint8_t* p, size_t n, interp_t& in) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 1)
            in.src_tick = reset(f) ? -1 : proto::i32(f);
        else if (f.num == 2)
            in.dst_tick = reset(f) ? -1 : proto::i32(f);
        else if (f.num == 3)
            in.frac = reset(f) ? 0.f : proto::f32(f);
    }
    return !r.bad;
}

static bool apply_history(const uint8_t* p, size_t n, history_t& h) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        bool rs = reset(f);
        switch (f.num) {
        case 2:
            h.has_angles = !rs;
            h.fresh = !rs;
            if (rs)
                h.pitch = h.yaw = h.roll = 0.f;
            else if (!apply_angles(f.data, f.len, &h.pitch, &h.yaw, &h.roll))
                return false;
            break;
        case 4: h.render_tick = rs ? 0 : proto::i32(f); break;
        case 5: h.render_frac = rs ? 0.f : proto::f32(f); break;
        case 6: h.player_tick = rs ? 0 : proto::i32(f); break;
        case 7: h.player_frac = rs ? 0.f : proto::f32(f); break;
        case 12: {
            if (rs) {
                h.cl_interp_frac = 0.f;
                break;
            }
            proto_t cr = delta(f.data, f.len);
            proto_field_t g;
            while (proto::next(cr, g))
                if (g.num == 3)
                    h.cl_interp_frac = reset(g) ? 0.f : proto::f32(g);
            if (cr.bad)
                return false;
            break;
        }
        case 13:
        case 14:
        case 15: {
            interp_t& in = f.num == 13 ? h.sv_interp0 : f.num == 14 ? h.sv_interp1 : h.player_interp;
            if (rs)
                in = interp_t{};
            else if (!apply_interp(f.data, f.len, in))
                return false;
            break;
        }
        case 64: h.frame = rs ? 0 : proto::i32(f); break;
        case 65: h.target = rs ? -1 : proto::i32(f); break;
        }
    }
    return !r.bad;
}

static bool apply_subtick(const uint8_t* p, size_t n, subtick_t& s) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        bool rs = reset(f);
        switch (f.num) {
        case 1: s.button = rs ? 0 : f.val; break;
        case 2: s.pressed = rs ? false : f.val != 0; break;
        case 3: s.when = rs ? 0.f : proto::f32(f); break;
        case 4: s.forward = rs ? 0.f : proto::f32(f); break;
        case 5: s.left = rs ? 0.f : proto::f32(f); break;
        }
    }
    return !r.bad;
}

static bool apply_base(const uint8_t* p, size_t n, usercmd_t& c) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        bool rs = reset(f);
        switch (f.num) {
        case 2: c.base_client_tick = rs ? 0 : proto::i32(f); break;
        case 3: {
            if (rs) {
                c.buttons[0] = c.buttons[1] = c.buttons[2] = 0;
                break;
            }
            c.sent_buttons = true;
            proto_t br = delta(f.data, f.len);
            proto_field_t g;
            while (proto::next(br, g))
                if (g.num >= 1 && g.num <= 3)
                    c.buttons[g.num - 1] = reset(g) ? 0 : g.val;
            if (br.bad)
                return false;
            break;
        }
        case 4:
            c.sent_angles = true;
            if (rs)
                c.pitch = c.yaw = c.roll = 0.f;
            else if (!apply_angles(f.data, f.len, &c.pitch, &c.yaw, &c.roll))
                return false;
            break;
        case 5: c.forward_move = rs ? 0.f : proto::f32(f); break;
        case 6: c.left_move = rs ? 0.f : proto::f32(f); break;
        case 7: c.up_move = rs ? 0.f : proto::f32(f); break;
        case 8: c.impulse = rs ? 0 : proto::i32(f); break;
        case 9: c.weapon_select = rs ? 0 : proto::i32(f); break;
        case 10: c.random_seed = rs ? 0 : proto::i32(f); break;
        case 11:
            c.mouse_dx = rs ? 0 : proto::i32(f);
            c.sent_mouse = true;
            break;
        case 12:
            c.mouse_dy = rs ? 0 : proto::i32(f);
            c.sent_mouse = true;
            break;
        case 14: c.pawn = rs ? 0xffffff : (uint32_t)f.val; break;
        case 17: c.field17 = rs ? 0 : proto::i32(f); break;
        case 18:
            c.sent_subtick = true;
            if (rs)
                c.subtick.clear();
            else if (!apply_repeated(f.data, f.len, c.subtick, apply_subtick))
                return false;
            break;
        case 21: c.flags = rs ? 0 : proto::i32(f); break;
        }
    }
    return !r.bad;
}

static bool apply_cmd(const uint8_t* p, size_t n, usercmd_t& c) {
    proto_t r = delta(p, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        bool rs = reset(f);
        switch (f.num) {
        case 1:
            if (!rs && !apply_base(f.data, f.len, c))
                return false;
            break;
        case 2:
            c.sent_history = true;
            if (rs)
                c.history.clear();
            else if (!apply_repeated(f.data, f.len, c.history, apply_history))
                return false;
            break;
        case 6: c.attack1 = rs ? -1 : proto::i32(f); break;
        case 7: c.attack2 = rs ? -1 : proto::i32(f); break;
        }
    }
    return !r.bad;
}

bool decode(usercmds_t& uc, const uint8_t* msg, size_t n, int tick) {
    uc.out.clear();
    proto_t r = delta(msg, n);
    proto_field_t outer;
    usercmd_t& tmp = uc.scratch;
    while (proto::next(r, outer)) {
        if (outer.num != 1 || outer.wire != 2)
            continue;
        int32_t slot = -1, number = 0, server_tick = 0, client_tick = 0;
        const uint8_t* body = nullptr;
        size_t body_len = 0;
        proto_t hr = delta(outer.data, outer.len);
        proto_field_t g;
        while (proto::next(hr, g)) {
            switch (g.num) {
            case 2: number = proto::i32(g); break;
            case 3: slot = proto::i32(g); break;
            case 4: server_tick = proto::i32(g); break;
            case 5: client_tick = proto::i32(g); break;
            case 6:
                body = g.data;
                body_len = g.len;
                break;
            }
        }
        if (hr.bad || slot < 0 || slot > 127) {
            uc.failed++;
            continue;
        }
        if ((size_t)slot >= uc.state.size())
            uc.state.resize(slot + 1);

        tmp = uc.state[slot];
        for (history_t& h : tmp.history)
            h.fresh = false;
        tmp.tick = tick;
        tmp.slot = slot;
        tmp.number = number;
        tmp.server_tick = server_tick;
        tmp.client_tick = client_tick;
        tmp.sent_angles = tmp.sent_mouse = tmp.sent_buttons = tmp.sent_history = tmp.sent_subtick = false;
        if (body && !apply_cmd(body, body_len, tmp)) {
            uc.failed++;
            continue;
        }
        std::swap(uc.state[slot], tmp);
        uc.decoded++;
        uc.out.push_back(slot);
    }
    return !r.bad;
}

std::string format(const usercmd_t& c) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%d %d %d %d %d %llu %llu %llu %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %d %d %u %d %d %d %d %d%d%d%d%d",
             c.slot, c.number, c.server_tick, c.client_tick, c.base_client_tick, (unsigned long long)c.buttons[0],
             (unsigned long long)c.buttons[1], (unsigned long long)c.buttons[2], c.pitch, c.yaw, c.roll,
             c.forward_move, c.left_move, c.up_move, c.impulse, c.weapon_select, c.random_seed, c.mouse_dx,
             c.mouse_dy, c.pawn, c.field17, c.flags, c.attack1, c.attack2, c.sent_angles, c.sent_mouse,
             c.sent_buttons, c.sent_history, c.sent_subtick);
    std::string out = buf;
    snprintf(buf, sizeof(buf), " S%zu", c.subtick.size());
    out += buf;
    for (const subtick_t& s : c.subtick) {
        snprintf(buf, sizeof(buf), " %llu %d %.9g %.9g %.9g", (unsigned long long)s.button, s.pressed, s.when,
                 s.forward, s.left);
        out += buf;
    }
    snprintf(buf, sizeof(buf), " H%zu", c.history.size());
    out += buf;
    for (const history_t& h : c.history) {
        snprintf(buf, sizeof(buf), " %.9g %.9g %.9g %d %.9g %d %.9g %.9g %d %d %.9g %d %d %.9g %d %d %.9g %d %d",
                 h.pitch, h.yaw, h.roll, h.render_tick, h.render_frac, h.player_tick, h.player_frac,
                 h.cl_interp_frac, h.sv_interp0.src_tick, h.sv_interp0.dst_tick, h.sv_interp0.frac,
                 h.sv_interp1.src_tick, h.sv_interp1.dst_tick, h.sv_interp1.frac, h.player_interp.src_tick,
                 h.player_interp.dst_tick, h.player_interp.frac, h.frame, h.target);
        out += buf;
    }
    return out;
}

}
