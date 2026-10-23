#pragma once

enum pflag_t : uint8_t {
    pf_valid = 1,
    pf_alive = 2,
    pf_scoped = 4,
    pf_walking = 8,
    pf_ducked = 16,
    pf_live = 32,
    pf_zoomed = 64,
};

struct pstate_t {
    float pos[3];
    float vel[3];
    float pitch;
    float yaw;
    float eye_z;
    int16_t health;
    uint8_t team;
    uint8_t flags;
    uint32_t spotted[2];
    int32_t pawn;
};

struct view_t {
    double tp;
    double tr;
    float pitch;
    float yaw;
};

struct click_t {
    int tick;
    double tp;
    float pitch;
    float yaw;
    double tr;
};

struct press_t {
    double down;
    double up;
};

struct cmd_t {
    int tick;
    int number;
    float pitch;
    float yaw;
    int32_t mouse_dx;
    int32_t mouse_dy;
    uint32_t buttons;
    uint16_t frames;
};

enum score_field_t {
    sc_kills,
    sc_deaths,
    sc_assists,
    sc_damage,
    sc_hs_kills,
    sc_utility,
    sc_flashed,
    sc_3k,
    sc_4k,
    sc_5k,
    sc_count,
};

struct player_t {
    int slot = -1;
    uint64_t steamid = 0;
    std::string name;
    int32_t score[sc_count] = {};
    std::vector<pstate_t> ticks;
    std::vector<view_t> views;
    std::vector<click_t> clicks;
    std::vector<press_t> presses;
    std::vector<cmd_t> cmds;
    double pressed = -1.0;
    uint64_t last_view = 0;
};

enum sound_kind_t : uint8_t {
    sk_step,
    sk_fire,
    sk_reload,
};

struct sound_t {
    int tick;
    int slot;
    uint8_t kind;
};

struct shot_t {
    int tick;
    int slot;
    std::string weapon;
};

struct fire_t {
    int tick;
    int slot;
    float pitch;
    float yaw;
    uint32_t item;
    float recoil;
    float inaccuracy;
    float spread;
};

struct hit_t {
    int tick;
    int attacker;
    int victim;
    float pitch;
    float yaw;
    double attack_t;
    double render_t;
    float distance;
    float recoil;
};

struct hurt_t {
    int tick;
    int attacker;
    int victim;
    int damage;
    int armor;
    int health;
    int hitgroup;
    std::string weapon;
};

struct round_end_t {
    int tick;
    int winner;
    int reason;
};

struct death_t {
    int tick;
    int attacker;
    int victim;
    int assister;
    bool headshot;
    bool smoke;
    int penetrated;
    std::string weapon;
};

struct smoke_t {
    int start;
    int end;
    int entity;
    float x, y, z;
};

struct blind_t {
    int tick;
    int slot;
    float seconds;
};

struct match_t {
    std::string map;
    int max_tick = 0;
    int server_offset = 0;
    double tick_interval = 0;
    int match_start = 0;
    std::vector<player_t> players;
    std::vector<sound_t> sounds;
    std::vector<shot_t> shots;
    std::vector<fire_t> fires;
    std::vector<hit_t> hits;
    std::vector<hurt_t> hurts;
    std::vector<death_t> deaths;
    std::vector<round_end_t> round_ends;
    std::vector<smoke_t> smokes;
    std::vector<blind_t> blinds;
    std::vector<uint8_t> live;
};

struct parser_t;
struct callbacks_t;
struct match_builder_t;

namespace model {
    bool build(match_t& m, const char* path, std::string& err);
    match_builder_t* begin(match_t& m, parser_t& p);
    void hook(match_builder_t* b, callbacks_t& cb);
    void finish(match_builder_t* b, parser_t& p);
    const pstate_t* at(const match_t& m, int slot, int tick);
    bool view_at(const match_t& m, int slot, int tick, float& pitch, float& yaw);
    int slot_of_pawn(const match_t& m, int pawn, int tick);
}
