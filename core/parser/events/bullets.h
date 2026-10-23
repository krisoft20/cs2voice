#pragma once

struct bullet_t {
    int32_t tick = 0;
    float origin[3] = {};
    float pitch = 0.f, yaw = 0.f, roll = 0.f;
    uint32_t weapon = 0xffffff;
    uint32_t mode = 0;
    uint32_t seed = 0;
    uint32_t player = 0xffffff;
    float inaccuracy = 0.f;
    float recoil_index = 0.f;
    float spread = 0.f;
    int32_t sound_type = 0;
    uint32_t item_def = 0;
    float ent_origin[3] = {};
    uint32_t bullets_left = 0;
    uint32_t attack_type = 0;
    int32_t server_tick = 0;
};

namespace bullets {
    bool decode(bullet_t& b, const uint8_t* msg, size_t n, int tick);
    int pawn(const bullet_t& b);
    std::string format(const bullet_t& b);
}
