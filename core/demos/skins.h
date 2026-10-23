#pragma once

#include "parser/entities/access.h"

struct parser_t;

struct skin_t {
    uint64_t id = 0;
    uint32_t account = 0;
    int def = 0;
    int paint = 0;
    int seed = 0;
    float wear = 0.f;
    int stattrak = -1;
    bool gloves = false;
    uint8_t sides = 0;
    std::string tag;
    uint32_t stickers[5] = {};
};

struct item_keys_t {
    field_key_t def;
    field_key_t high;
    field_key_t low;
    field_key_t account;
    field_key_t tag;
    field_key_t attrs;
    field_key_t attr_def;
    field_key_t attr_raw;
};

struct skins_t {
    item_keys_t weapon;
    item_keys_t gloves;
    field_key_t fallback_paint = access::key("m_nFallbackPaintKit");
    field_key_t fallback_seed = access::key("m_nFallbackSeed");
    field_key_t fallback_wear = access::key("m_flFallbackWear");
    field_key_t fallback_stattrak = access::key("m_nFallbackStatTrak");
    field_key_t steam = access::key("m_steamID");
    field_key_t music = access::key("m_pInventoryServices.m_unMusicID");
    field_key_t team = access::key("m_iTeamNum");
    std::map<uint32_t, int> teams;
    int next = 0;
    std::map<uint64_t, skin_t> items;
    std::map<uint32_t, int> musics;
};

namespace skins {
    void init(skins_t& s);
    void on_frame(skins_t& s, parser_t& p);
    void of(const skins_t& s, uint64_t steamid, std::vector<skin_t>& out, int& music);
}
