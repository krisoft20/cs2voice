#include "pch.h"

#include <mutex>

#include "game/econ.h"
#include "game/weapons.h"
#include "geo/kv3.h"
#include "geo/vpk.h"

namespace weapons {
    std::map<int, std::string> g_names;
    std::map<std::string, std::pair<float, float>> g_speeds;
    double g_step_range = 0;
    std::mutex g_mu;
    bool g_loaded = false;

    double silent_at(const kv_node_t& ev) {
        const kv_node_t* c = kv3::get(ev, "distance_volume_mapping_curve");
        if (!c || c->type != kv_array)
            return 0;
        for (const kv_node_t& pt : c->items)
            if (pt.type == kv_array && pt.items.size() >= 2 && pt.items[0].d > 0 && pt.items[1].d <= 0)
                return pt.items[0].d;
        return 0;
    }

    void on_items(const std::vector<std::string>& path, const std::string& key, const std::string& value) {
        if (path.size() == 3 && path[0] == "items_game" && path[1] == "items" && key == "name")
            g_names[atoi(path[2].c_str())] = value;
    }

    bool speed_of(const kv_node_t& root, std::string name, std::pair<float, float>& out) {
        for (int guard = 0; guard < 8; guard++) {
            const kv_node_t* w = kv3::get(root, name.c_str());
            if (!w)
                return false;
            const kv_node_t* s = kv3::get(*w, "m_flMaxSpeed");
            if (s && s->type == kv_array && s->items.size() == 2) {
                out = {(float)s->items[0].d, (float)s->items[1].d};
                return true;
            }
            const kv_node_t* base = kv3::get(*w, "_base");
            if (!base || base->type != kv_string)
                return false;
            name = base->s;
        }
        return false;
    }

    bool load(const std::string& csgo_dir, std::string& err) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_loaded)
            return true;
        std::string pak = csgo_dir + "\\pak01_dir.vpk";
        std::vector<uint8_t> items, file;
        const uint8_t* blk = nullptr;
        size_t len = 0;
        kv_node_t root;
        if (!vpk::extract(pak, "scripts/items/items_game.txt", items, err) || !vpk::extract(pak, "scripts/weapons.vdata_c", file, err) ||
            !vpk::block(file, "DATA", blk, len) || !kv3::read(blk, len, root, err))
            return false;
        econ::parse((const char*)items.data(), (const char*)items.data() + items.size(), on_items);
        for (const std::string& k : root.keys) {
            std::pair<float, float> s;
            if (k.rfind("weapon_", 0) == 0 && speed_of(root, k, s))
                g_speeds[k] = s;
        }
        kv_node_t sounds;
        if (!vpk::extract(pak, "soundevents/game_sounds_footsteps.vsndevts_c", file, err) || !vpk::block(file, "DATA", blk, len) || !kv3::read(blk, len, sounds, err))
            return false;
        std::map<double, int> ranges;
        for (const kv_node_t& ev : sounds.items)
            if (double r = silent_at(ev); r > 0)
                ranges[r]++;
        int most = 0;
        for (auto& [r, n] : ranges)
            if (n > most) {
                most = n;
                g_step_range = r;
            }
        g_loaded = true;
        return true;
    }

    double step_range() {
        std::lock_guard<std::mutex> lock(g_mu);
        return g_step_range;
    }

    float max_speed(uint32_t def, bool alt) {
        std::lock_guard<std::mutex> lock(g_mu);
        auto n = g_names.find((int)def);
        if (n == g_names.end())
            return 0;
        auto s = g_speeds.find(n->second);
        if (s == g_speeds.end())
            return 0;
        return alt ? s->second.second : s->second.first;
    }
}
