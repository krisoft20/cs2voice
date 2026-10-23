#include "pch.h"

#include "parser/events/bullets.h"
#include "parser/proto/proto.h"

namespace bullets {

static bool vec3(const proto_field_t& f, float* v) {
    proto_t r = proto::make(f.data, f.len);
    r.deltas = true;
    proto_field_t g;
    while (proto::next(r, g))
        if (g.num >= 1 && g.num <= 3)
            v[g.num - 1] = proto::f32(g);
    return !r.bad;
}

bool decode(bullet_t& b, const uint8_t* msg, size_t n, int tick) {
    b = bullet_t{};
    b.tick = tick;
    proto_t r = proto::make(msg, n);
    r.deltas = true;
    proto_field_t f;
    float ang[3] = {};
    while (proto::next(r, f)) {
        switch (f.num) {
        case 1:
            if (!vec3(f, b.origin))
                return false;
            break;
        case 2:
            if (!vec3(f, ang))
                return false;
            b.pitch = ang[0];
            b.yaw = ang[1];
            b.roll = ang[2];
            break;
        case 3: b.weapon = (uint32_t)f.val; break;
        case 4: b.mode = (uint32_t)f.val; break;
        case 5: b.seed = (uint32_t)f.val; break;
        case 6: b.player = (uint32_t)f.val; break;
        case 7: b.inaccuracy = proto::f32(f); break;
        case 8: b.recoil_index = proto::f32(f); break;
        case 9: b.spread = proto::f32(f); break;
        case 10: b.sound_type = proto::i32(f); break;
        case 11: b.item_def = (uint32_t)f.val; break;
        case 13:
            if (!vec3(f, b.ent_origin))
                return false;
            break;
        case 14: b.bullets_left = (uint32_t)f.val; break;
        case 15: b.attack_type = (uint32_t)f.val; break;
        case 19: b.server_tick = proto::i32(f); break;
        }
    }
    return !r.bad;
}

int pawn(const bullet_t& b) {
    int idx = (int)(b.player & 0x3fff);
    return idx == 0x3fff ? -1 : idx;
}

std::string format(const bullet_t& b) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%.9g %.9g %.9g %.9g %.9g %.9g %u %u %u %u %.9g %.9g %.9g %d %u %.9g %.9g %.9g %u %u %d",
             b.origin[0], b.origin[1], b.origin[2], b.pitch, b.yaw, b.roll, b.weapon, b.mode, b.seed, b.player,
             b.inaccuracy, b.recoil_index, b.spread, b.sound_type, b.item_def, b.ent_origin[0], b.ent_origin[1],
             b.ent_origin[2], b.bullets_left, b.attack_type, b.server_tick);
    return buf;
}

}
