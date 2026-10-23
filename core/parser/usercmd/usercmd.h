#pragma once

enum button_t : uint64_t {
    in_attack = 1ull << 0,
    in_jump = 1ull << 1,
    in_duck = 1ull << 2,
    in_forward = 1ull << 3,
    in_back = 1ull << 4,
    in_use = 1ull << 5,
    in_move_left = 1ull << 9,
    in_move_right = 1ull << 10,
    in_attack2 = 1ull << 11,
    in_reload = 1ull << 13,
    in_speed = 1ull << 16,
};

struct interp_t {
    int32_t src_tick = -1;
    int32_t dst_tick = -1;
    float frac = 0.f;
};

struct history_t {
    float pitch = 0.f, yaw = 0.f, roll = 0.f;
    int32_t render_tick = 0;
    float render_frac = 0.f;
    int32_t player_tick = 0;
    float player_frac = 0.f;
    float cl_interp_frac = 0.f;
    interp_t sv_interp0, sv_interp1, player_interp;
    int32_t frame = 0;
    int32_t target = -1;
    bool has_angles = false;
    bool fresh = false;
};

struct subtick_t {
    uint64_t button = 0;
    bool pressed = false;
    float when = 0.f;
    float forward = 0.f;
    float left = 0.f;
};

struct usercmd_t {
    int32_t tick = 0;
    int32_t slot = -1;
    int32_t number = 0;
    int32_t server_tick = 0;
    int32_t client_tick = 0;

    int32_t base_client_tick = 0;
    uint64_t buttons[3] = {};
    float pitch = 0.f, yaw = 0.f, roll = 0.f;
    float forward_move = 0.f, left_move = 0.f, up_move = 0.f;
    int32_t impulse = 0, weapon_select = 0, random_seed = 0;
    int32_t mouse_dx = 0, mouse_dy = 0;
    uint32_t pawn = 0xffffff;
    int32_t field17 = 0;
    int32_t flags = 0;
    std::vector<subtick_t> subtick;

    std::vector<history_t> history;
    int32_t attack1 = -1, attack2 = -1;

    bool sent_angles = false;
    bool sent_mouse = false;
    bool sent_buttons = false;
    bool sent_history = false;
    bool sent_subtick = false;
};

struct usercmds_t {
    std::vector<usercmd_t> state;
    usercmd_t scratch;
    std::vector<int32_t> out;
    size_t decoded = 0;
    size_t failed = 0;
};

namespace usercmd {
    bool decode(usercmds_t& uc, const uint8_t* msg, size_t n, int tick);
    std::string format(const usercmd_t& c);
}
