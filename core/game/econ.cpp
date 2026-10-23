#include "pch.h"

#include "game/econ.h"
#include "game/vpk.h"
#include "text.h"

namespace econ {
    const int FIRST_KNIFE = 500;
    const int LAST_KNIFE = 999;

    struct def_t {
        std::string name;
        std::string item_name;
        std::string prefab;
    };

    struct prefab_t {
        std::string item_name;
        std::string prefab;
    };

    struct paint_t {
        std::string name;
        std::string tag;
    };

    struct sticker_t {
        std::string item_name;
        std::string material;
    };

    struct rarity_t {
        int value = 0;
        std::string color;
        std::string loc;
        std::string next;
    };

    std::map<int, def_t> g_defs;
    std::map<std::string, prefab_t> g_prefabs;
    std::map<int, paint_t> g_paints;
    std::map<std::string, std::string> g_paint_rarity;
    std::map<std::string, std::string> g_loot_rarity;
    std::map<uint32_t, sticker_t> g_stickers;
    std::map<int, std::string> g_musics;
    std::map<std::string, std::string> g_colors;
    std::map<std::string, rarity_t> g_rarities;
    std::unordered_map<std::string, std::string> g_text;
    std::string g_lang;
    bool g_items = false;

    typedef void (*pair_fn_t)(const std::vector<std::string>& path, const std::string& key, const std::string& value);

    void parse(const char* p, const char* end, pair_fn_t fn) {
        std::vector<std::string> path;
        std::string key, tok;
        bool have_key = false;
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                p++;
            } else if (c == '/' && p + 1 < end && p[1] == '/') {
                while (p < end && *p != '\n')
                    p++;
            } else if (c == '{') {
                path.push_back(have_key ? key : std::string());
                have_key = false;
                p++;
            } else if (c == '}') {
                if (!path.empty())
                    path.pop_back();
                have_key = false;
                p++;
            } else if (c == '"') {
                tok.clear();
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end) {
                        p++;
                        tok += *p == 'n' ? '\n' : *p;
                    } else {
                        tok += *p;
                    }
                    p++;
                }
                p++;
                if (have_key) {
                    fn(path, key, tok);
                    have_key = false;
                } else {
                    key = tok;
                    have_key = true;
                }
            } else {
                tok.clear();
                while (p < end && !strchr(" \t\r\n{}\"", *p))
                    tok += *p++;
                if (tok[0] == '[')
                    continue;
                if (have_key) {
                    fn(path, key, tok);
                    have_key = false;
                } else {
                    key = tok;
                    have_key = true;
                }
            }
        }
    }

    void on_items(const std::vector<std::string>& path, const std::string& key, const std::string& value) {
        if (path.size() < 2 || path[0] != "items_game")
            return;
        const std::string& sec = path[1];
        if (path.size() == 3) {
            const std::string& id = path[2];
            if (sec == "items") {
                def_t& d = g_defs[atoi(id.c_str())];
                if (key == "name")
                    d.name = value;
                else if (key == "item_name")
                    d.item_name = value;
                else if (key == "prefab")
                    d.prefab = value;
            } else if (sec == "prefabs") {
                prefab_t& f = g_prefabs[id];
                if (key == "item_name")
                    f.item_name = value;
                else if (key == "prefab")
                    f.prefab = value;
            } else if (sec == "paint_kits") {
                paint_t& k = g_paints[atoi(id.c_str())];
                if (key == "name")
                    k.name = value;
                else if (key == "description_tag")
                    k.tag = value;
            } else if (sec == "sticker_kits") {
                sticker_t& k = g_stickers[(uint32_t)strtoul(id.c_str(), nullptr, 10)];
                if (key == "item_name")
                    k.item_name = value;
                else if (key == "sticker_material")
                    k.material = value;
            } else if (sec == "music_definitions") {
                if (key == "loc_name")
                    g_musics[atoi(id.c_str())] = value;
            } else if (sec == "colors") {
                if (key == "hex_color")
                    g_colors[id] = value;
            } else if (sec == "rarities") {
                rarity_t& r = g_rarities[id];
                if (key == "color")
                    r.color = value;
                else if (key == "value")
                    r.value = atoi(value.c_str());
                else if (key == "loc_key_weapon")
                    r.loc = value;
                else if (key == "next_rarity")
                    r.next = value;
            } else if (sec == "client_loot_lists" && key[0] == '[') {
                size_t cut = id.rfind('_');
                if (cut != std::string::npos)
                    g_loot_rarity[key] = id.substr(cut + 1);
            }
        } else if (path.size() == 2 && sec == "paint_kits_rarity") {
            g_paint_rarity[key] = value;
        }
    }

    void on_text(const std::vector<std::string>& path, const std::string& key, const std::string& value) {
        if (path.size() == 2 && text::lower(path[1]) == "tokens" && key.rfind("[english]", 0) != 0)
            g_text[text::lower(key)] = value;
    }

    std::string text(const std::string& token) {
        if (token.empty())
            return token;
        auto it = g_text.find(text::lower(token[0] == '#' ? token.substr(1) : token));
        return it == g_text.end() ? token : it->second;
    }

    bool load(const std::string& csgo_dir, const std::string& lang, std::string& err) {
        std::string pak = csgo_dir + "\\pak01_dir.vpk";
        if (!g_items) {
            std::vector<uint8_t> raw;
            if (!vpk::extract(pak, "scripts/items/items_game.txt", raw, err))
                return false;
            parse((const char*)raw.data(), (const char*)raw.data() + raw.size(), on_items);
            g_items = true;
        }
        if (g_lang != lang) {
            std::vector<uint8_t> raw;
            std::string file = lang == "pl" ? "polish" : "english";
            if (!vpk::extract(pak, "resource/csgo_" + file + ".txt", raw, err))
                return false;
            g_text.clear();
            size_t skip = raw.size() >= 3 && raw[0] == 0xef && raw[1] == 0xbb && raw[2] == 0xbf ? 3 : 0;
            parse((const char*)raw.data() + skip, (const char*)raw.data() + raw.size(), on_text);
            g_lang = lang;
        }
        return true;
    }

    std::string slot_of(const def_t& d) {
        static const char* slots[][2] = {{"secondary", "pistol"}, {"smg", "smg"},      {"shotgun", "heavy"},
                                         {"machinegun", "heavy"}, {"rifle", "rifle"}, {"sniper_rifle", "rifle"}};
        std::string p = d.prefab;
        for (int guard = 0; guard < 16 && !p.empty(); guard++) {
            std::string first = p.substr(0, p.find(' '));
            for (const auto& s : slots)
                if (first == s[0])
                    return s[1];
            auto it = g_prefabs.find(first);
            if (it == g_prefabs.end())
                break;
            p = it->second.prefab;
        }
        return "other";
    }

    std::string item_name(const def_t& d) {
        if (!d.item_name.empty())
            return d.item_name;
        std::string p = d.prefab;
        for (int guard = 0; guard < 16 && !p.empty(); guard++) {
            auto it = g_prefabs.find(p);
            if (it == g_prefabs.end())
                break;
            if (!it->second.item_name.empty())
                return it->second.item_name;
            p = it->second.prefab;
        }
        return std::string();
    }

    void rarity(const std::string& r, econ_item_t& out) {
        auto it = g_rarities.find(r);
        if (it == g_rarities.end())
            return;
        out.rarity = text(it->second.loc);
        out.rank = it->second.value;
        auto c = g_colors.find(it->second.color);
        if (c != g_colors.end())
            out.color = c->second;
    }

    bool item(int def, int paint, float wear, bool gloves, econ_item_t& out) {
        auto d = g_defs.find(def);
        if (d == g_defs.end())
            return false;
        out.weapon = text(item_name(d->second));
        out.star = gloves || (def >= FIRST_KNIFE && def <= LAST_KNIFE);
        out.slot = gloves ? "gloves" : out.star ? "knife" : slot_of(d->second);
        const std::string& cls = d->second.name;
        out.base_icon = "panorama/images/econ/weapons/base_weapons/" + cls + "_png.vtex_c";
        out.icon = out.base_icon;
        std::string r = out.star ? "ancient" : "default";
        auto pk = g_paints.find(paint);
        if (paint > 0 && pk != g_paints.end()) {
            out.skin = text(pk->second.tag);
            const char* variant = wear < 0.15f ? "light" : wear < 0.38f ? "medium" : "heavy";
            out.icon = "panorama/images/econ/default_generated/" + cls + "_" + pk->second.name + "_" + variant + "_png.vtex_c";
            for (const char* v : {"light", "medium", "heavy"})
                out.variants.push_back("panorama/images/econ/default_generated/" + cls + "_" + pk->second.name + "_" + v + "_png.vtex_c");
            if (!out.star) {
                auto loot = g_loot_rarity.find("[" + pk->second.name + "]" + cls);
                if (loot != g_loot_rarity.end() && g_rarities.count(loot->second)) {
                    r = loot->second;
                } else {
                    auto pr = g_paint_rarity.find(pk->second.name);
                    if (pr != g_paint_rarity.end() && g_rarities.count(pr->second))
                        r = g_rarities[pr->second].next.empty() ? pr->second : g_rarities[pr->second].next;
                }
            }
        }
        rarity(r, out);
        return true;
    }

    bool sticker(uint32_t id, std::string& name, std::string& icon) {
        auto it = g_stickers.find(id);
        if (it == g_stickers.end())
            return false;
        name = text(it->second.item_name);
        icon = it->second.material.empty() ? std::string() : "panorama/images/econ/stickers/" + text::lower(it->second.material) + "_png.vtex_c";
        return true;
    }

    std::string music(int id) {
        auto it = g_musics.find(id);
        return it == g_musics.end() ? std::string() : text(it->second);
    }

    std::string wear_name(float wear) {
        int n = wear < 0.07f ? 0 : wear < 0.15f ? 1 : wear < 0.38f ? 2 : wear < 0.45f ? 3 : 4;
        return text("SFUI_InvTooltip_Wear_Amount_" + std::to_string(n));
    }
}
