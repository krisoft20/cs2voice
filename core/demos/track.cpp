#include "pch.h"

#include "demos/track.h"
#include "parser/entities/access.h"
#include "parser/parser.h"

namespace track {
    const uint32_t MAGIC = 0x54325343;
    const uint32_t VERSION = 3;
    const int EVERY = 4;
    const int NADE_EVERY = 2;
    const int SLOTS = 64;
    const int FLASH_AMMO = 14;
    const int FLASHBANG = 43;
    const int CMD_KEEP = 192;
    const int THROW_BACK = 64;

#pragma pack(push, 1)
    struct sample_t {
        uint8_t slot = 0;
        uint8_t flags = 0;
        uint8_t hp = 0;
        uint8_t armor = 0;
        float x = 0, y = 0;
        int16_t z = 0;
        uint16_t yaw = 0;
        uint16_t item = 0;
    };
#pragma pack(pop)

    enum watch_t : int8_t {
        w_none = -1,
        w_smoke,
        w_flash,
        w_he,
        w_molotov,
        w_decoy,
        w_inferno,
        w_c4,
        w_planted,
    };

    const char* NADE_NAMES[] = {"smoke", "flash", "he", "molotov", "decoy"};

    struct round_t {
        int start = 0;
        int live = -1;
        int end = -1;
        int time = -1;
    };

    struct throw_t {
        int release = -1;
        float pitch = 0, yaw = 0;
        float at[3] = {};
        float from[3] = {};
        int jump = 0;
        int keys = 0;
        int click = 0;
        int walked = 0;
    };

    struct cmd_rec_t {
        int tick = 0;
        uint64_t buttons = 0;
        float pitch = 0, yaw = 0;
        float pos[3] = {};
        bool has_pos = false;
        bool ground = false;
        float attack_up = -1;
        float jump_down = -1;
    };

    struct nade_t {
        int kind = 0;
        int thrower = -1;
        int start = 0;
        int det = -1;
        int end = -1;
        float pos[3] = {};
        float last[3] = {};
        int points = 0;
        bool linked = false;
        bool resolved = false;
        throw_t thrown;
        std::string path;
    };

    struct fire_t {
        int nade = -1;
        int owner = -1;
        int start = 0;
        int end = -1;
        bool bound = false;
        uint64_t mask[2] = {};
        std::vector<std::pair<float, float>> cells;
        std::string frames;
    };

    struct live_t {
        int serial = 0;
        int8_t kind = w_none;
        int rec = -1;
    };

    struct inv_t {
        int money = -1;
        std::vector<int> items;
    };

    struct state_t {
        field_key_t steam = access::key("m_steamID");
        field_key_t team = access::key("m_iTeamNum");
        field_key_t alive = access::key("m_bPawnIsAlive");
        field_key_t pawn = access::key("m_hPlayerPawn");
        field_key_t health = access::key("m_iHealth");
        field_key_t eye = access::key("m_angEyeAngles");
        field_key_t money = access::key("m_pInGameMoneyServices.m_iAccount");
        field_key_t armor = access::key("m_ArmorValue");
        field_key_t helmet = access::key("m_pItemServices.m_bHasHelmet");
        field_key_t defuser = access::key("m_pItemServices.m_bHasDefuser");
        field_key_t defusing = access::key("m_bIsDefusing");
        field_key_t scoped = access::key("m_bIsScoped");
        field_key_t weapon = access::key("m_pWeaponServices.m_hActiveWeapon");
        field_key_t weapons = access::key("m_pWeaponServices.m_hMyWeapons");
        field_key_t ammo = access::key("m_pWeaponServices.m_iAmmo");
        field_key_t item = access::key("m_AttributeManager.m_Item.m_iItemDefinitionIndex");
        field_key_t thrower = access::key("m_hThrower");
        field_key_t owner = access::key("m_hOwnerEntity");
        field_key_t fire_count = access::key("m_fireCount");
        field_key_t fire_pos = access::key("m_firePositions");
        field_key_t fire_on = access::key("m_bFireIsBurning");
        field_key_t timer = access::key("m_flTimerLength");
        field_key_t being_defused = access::key("m_bBeingDefused");
        field_key_t defuse_len = access::key("m_flDefuseLength");
        field_key_t defuser_pawn = access::key("m_hBombDefuser");
        field_key_t round_time = access::key("m_pGameRules.m_iRoundTime");
        field_key_t ground_flags = access::key("m_fFlags");
        field_key_t cell[3] = {access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellX"),
                               access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellY"),
                               access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellZ")};
        field_key_t vec[3] = {access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecX"),
                              access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecY"),
                              access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecZ")};
        int controller_cls = -1;
        int rules_cls = -1;
        int rules = -1;
        std::vector<int8_t> kinds;
        int next = 0;
        int next_nade = 0;
        uint64_t steamid[SLOTS] = {};
        std::unordered_map<int, int> pawn_slot;
        std::map<int, live_t> watch;
        std::string samples;
        uint32_t sample_count = 0;
        std::vector<round_t> rounds;
        std::vector<nade_t> nades;
        std::vector<fire_t> fires;
        std::map<int, int> nade_of;
        std::map<int, int> fire_of;
        std::vector<cmd_rec_t> cmds[SLOTS];
        std::vector<int> unresolved;
        inv_t inv[SLOTS];
        int carrier = -2;
        float bomb_at[3] = {};
        bool bomb_defusing = false;
        std::string kills;
        std::string hurts;
        std::string shots;
        std::string blinds;
        std::string bomb;
        std::string inventory;
        bool live = false;
        const std::atomic<bool>* cancel = nullptr;
    };

    void put(std::string& out, const void* p, size_t n) {
        out.append((const char*)p, n);
    }

    std::string num(double v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    }

    void add(std::string& list, const std::string& item) {
        list += (list.empty() ? "" : ",") + item;
    }

    bool world_pos(parser_t& p, state_t& s, const entity_t& e, float* out) {
        for (int i = 0; i < 3; i++) {
            const field_ref_t* c = access::ref(s.cell[i], p.lay, p.ser, e);
            const field_ref_t* v = access::ref(s.vec[i], p.lay, p.ser, e);
            if (!c || !v)
                return false;
            out[i] = (float)((double)access::get_u64(e, *c) * 512.0 - 16384.0 + access::get_float(e, *v));
        }
        return true;
    }

    const entity_t* handle(parser_t& p, uint64_t h) {
        if ((h & 0x7fff) == 0x7fff)
            return nullptr;
        return entities::get(p.ents, (int)(h & 0x3fff));
    }

    int read_int(parser_t& p, field_key_t& k, const entity_t& e, int def = 0) {
        const field_ref_t* r = access::ref(k, p.lay, p.ser, e);
        return r ? (int)access::get_int(e, *r) : def;
    }

    bool read_bool(parser_t& p, field_key_t& k, const entity_t& e) {
        const field_ref_t* r = access::ref(k, p.lay, p.ser, e);
        return r && access::get_bool(e, *r);
    }

    int slot_of(parser_t& p, state_t& s, uint64_t h) {
        const entity_t* e = handle(p, h);
        if (!e)
            return -1;
        auto it = s.pawn_slot.find(e->index);
        return it == s.pawn_slot.end() ? -1 : it->second;
    }

    void map_pawns(parser_t& p, state_t& s) {
        if (s.controller_cls < 0)
            return;
        s.pawn_slot.clear();
        for (int slot = 0; slot < SLOTS; slot++) {
            const entity_t* c = entities::get(p.ents, slot + 1);
            if (!c || c->cls != s.controller_cls)
                continue;
            const field_ref_t* ph = access::ref(s.pawn, p.lay, p.ser, *c);
            const entity_t* pe = ph ? handle(p, access::get_u64(*c, *ph)) : nullptr;
            if (pe)
                s.pawn_slot[pe->index] = slot;
        }
    }

    void classes(parser_t& p, state_t& s) {
        static const std::pair<const char*, watch_t> names[] = {
            {"CSmokeGrenadeProjectile", w_smoke}, {"CFlashbangProjectile", w_flash}, {"CHEGrenadeProjectile", w_he},
            {"CMolotovProjectile", w_molotov},    {"CDecoyProjectile", w_decoy},     {"CInferno", w_inferno},
            {"CC4", w_c4},                        {"CPlantedC4", w_planted},
        };
        s.kinds.assign(p.ser.classes.size(), w_none);
        for (size_t i = 0; i < p.ser.classes.size(); i++) {
            const std::string& c = p.ser.classes[i];
            if (c == "CCSPlayerController")
                s.controller_cls = (int)i;
            else if (c == "CCSGameRulesProxy")
                s.rules_cls = (int)i;
            for (auto& [n, k] : names)
                if (c == n)
                    s.kinds[i] = (int8_t)k;
        }
    }

    void inventory(parser_t& p, state_t& s, int slot, const entity_t& c, const entity_t* pe) {
        inv_t now;
        now.money = read_int(p, s.money, c);
        const field_ref_t* w = pe ? access::ref(s.weapons, p.lay, p.ser, *pe) : nullptr;
        uint32_t n = w ? access::count(*pe, *w) : 0;
        int flashes = 0;
        if (pe) {
            const field_ref_t* a = access::ref(s.ammo, p.lay, p.ser, *pe);
            flashes = a ? (int)access::get_int(*pe, *a, FLASH_AMMO) : 0;
        }
        for (uint32_t i = 0; i < n && i < 64; i++) {
            const entity_t* we = handle(p, access::get_u64(*pe, *w, (int)i));
            if (!we)
                continue;
            int def = read_int(p, s.item, *we, -1);
            if (def < 0)
                continue;
            now.items.push_back(def);
            for (int k = 1; def == FLASHBANG && k < flashes; k++)
                now.items.push_back(def);
        }
        inv_t& was = s.inv[slot];
        if (now.money == was.money && now.items == was.items)
            return;
        was = now;
        std::string list;
        for (int d : now.items)
            add(list, std::to_string(d));
        add(s.inventory, "[" + std::to_string(p.tick) + "," + std::to_string(slot) + "," + std::to_string(now.money) + ",[" + list + "]]");
    }

    void sample(parser_t& p, state_t& s) {
        std::vector<sample_t> rows;
        s.pawn_slot.clear();
        for (int slot = 0; slot < SLOTS; slot++) {
            const entity_t* c = entities::get(p.ents, slot + 1);
            if (!c || c->cls != s.controller_cls)
                continue;
            int side = read_int(p, s.team, *c);
            if (side < 2 || side > 3)
                continue;
            const field_ref_t* st = access::ref(s.steam, p.lay, p.ser, *c);
            if (st && !s.steamid[slot])
                s.steamid[slot] = access::get_u64(*c, *st);
            const field_ref_t* ph = access::ref(s.pawn, p.lay, p.ser, *c);
            const entity_t* pe = ph ? handle(p, access::get_u64(*c, *ph)) : nullptr;
            inventory(p, s, slot, *c, pe);
            if (!pe)
                continue;
            s.pawn_slot[pe->index] = slot;
            float pos[3];
            if (!world_pos(p, s, *pe, pos))
                continue;
            sample_t r;
            r.slot = (uint8_t)slot;
            r.x = pos[0];
            r.y = pos[1];
            r.z = (int16_t)std::clamp(lroundf(pos[2]), -32768l, 32767l);
            const field_ref_t* eye = access::ref(s.eye, p.lay, p.ser, *pe);
            if (eye) {
                float ang[3] = {};
                access::get_vec3(*pe, *eye, ang);
                float yaw = fmodf(ang[1], 360.f);
                if (yaw < 0)
                    yaw += 360.f;
                r.yaw = (uint16_t)(lroundf(yaw / 360.f * 65536.f) & 0xffff);
            }
            r.hp = (uint8_t)std::clamp(read_int(p, s.health, *pe), 0, 255);
            const field_ref_t* al = access::ref(s.alive, p.lay, p.ser, *c);
            bool alive = al ? access::get_bool(*c, *al) : r.hp > 0;
            r.flags = (uint8_t)((alive ? 1 : 0) | (side == 3 ? 2 : 0) | (read_bool(p, s.helmet, *pe) ? 8 : 0) | (read_bool(p, s.defuser, *pe) ? 16 : 0) |
                                (read_bool(p, s.defusing, *pe) ? 32 : 0) | (read_bool(p, s.scoped, *pe) ? 64 : 0));
            r.armor = (uint8_t)std::clamp(read_int(p, s.armor, *pe), 0, 255);
            const field_ref_t* wh = access::ref(s.weapon, p.lay, p.ser, *pe);
            const entity_t* we = wh ? handle(p, access::get_u64(*pe, *wh)) : nullptr;
            r.item = we ? (uint16_t)read_int(p, s.item, *we) : 0;
            rows.push_back(r);
        }
        for (sample_t& r : rows)
            if (r.slot == s.carrier)
                r.flags |= 4;
        int32_t tick = p.tick;
        uint8_t n = (uint8_t)rows.size();
        put(s.samples, &tick, 4);
        put(s.samples, &n, 1);
        for (const sample_t& r : rows)
            put(s.samples, &r, sizeof(r));
        s.sample_count++;
    }

    void bomb_row(state_t& s, int tick, const char* kind, int slot, const float* pos, double extra) {
        add(s.bomb, "[" + std::to_string(tick) + ",\"" + kind + "\"," + std::to_string(slot) + "," + num(pos[0]) + "," + num(pos[1]) + "," +
                        num(pos[2]) + "," + num(extra) + "]");
    }

    int open_nade(parser_t& p, state_t& s, int kind, int thrower) {
        nade_t n;
        n.kind = kind;
        n.thrower = thrower;
        n.start = p.tick;
        s.nades.push_back(n);
        s.unresolved.push_back((int)s.nades.size() - 1);
        return (int)s.nades.size() - 1;
    }

    void resolve(state_t& s, int i) {
        nade_t& n = s.nades[i];
        n.resolved = true;
        if (n.thrower < 0 || n.thrower >= SLOTS)
            return;
        const std::vector<cmd_rec_t>& q = s.cmds[n.thrower];
        int at = -1;
        for (int k = (int)q.size() - 1; k >= 0; k--)
            if (q[k].tick <= n.start) {
                at = k;
                break;
            }
        if (at < 0)
            return;
        const uint64_t attack = in_attack | in_attack2;
        int rel = -1;
        for (int k = at; k >= 0 && q[k].tick >= n.start - THROW_BACK; k--)
            if (q[k].attack_up >= 0 || (k > 0 && (q[k - 1].buttons & attack) && !(q[k].buttons & attack))) {
                rel = k;
                break;
            }
        if (rel < 0)
            return;
        const cmd_rec_t& r = q[rel];
        throw_t& t = n.thrown;
        t.release = r.tick;
        t.pitch = q[at].pitch;
        t.yaw = q[at].yaw;
        memcpy(t.at, r.pos, sizeof(t.at));
        uint64_t held = rel > 0 ? q[rel - 1].buttons : r.buttons;
        t.click = ((held & in_attack) ? 1 : 0) | ((held & in_attack2) ? 2 : 0);
        bool near_jump = r.jump_down >= 0 || (rel > 0 && q[rel - 1].jump_down >= 0) || (rel + 1 < (int)q.size() && q[rel + 1].jump_down >= 0);
        if (r.jump_down >= 0 && r.jump_down == r.attack_up)
            t.jump = 1;
        else if (near_jump)
            t.jump = 2;
        else if (r.has_pos && !r.ground)
            t.jump = 3;
        t.keys = ((r.buttons & in_forward) ? 1 : 0) | ((r.buttons & in_move_left) ? 2 : 0) | ((r.buttons & in_back) ? 4 : 0) |
                 ((r.buttons & in_move_right) ? 8 : 0) | ((r.buttons & in_duck) ? 16 : 0) | ((r.buttons & in_speed) ? 32 : 0);
        int first = rel;
        while ((r.buttons & in_forward) && first > 0 && (q[first - 1].buttons & in_forward))
            first--;
        t.walked = (r.buttons & in_forward) ? r.tick - q[first].tick + 1 : 0;
        memcpy(t.from, q[first].pos, sizeof(t.from));
    }

    void on_usercmd(parser_t& p, const usercmd_t& c, void* user) {
        state_t& s = *(state_t*)user;
        if (c.slot < 0 || c.slot >= SLOTS || s.controller_cls < 0)
            return;
        cmd_rec_t r;
        r.tick = c.tick;
        r.buttons = c.buttons[0];
        r.pitch = c.pitch;
        r.yaw = c.yaw;
        for (const subtick_t& st : c.subtick) {
            if ((st.button & (in_attack | in_attack2)) && !st.pressed)
                r.attack_up = st.when;
            if (st.button == in_jump && st.pressed)
                r.jump_down = st.when;
        }
        const entity_t* ctl = entities::get(p.ents, c.slot + 1);
        if (ctl && ctl->cls == s.controller_cls) {
            const field_ref_t* ph = access::ref(s.pawn, p.lay, p.ser, *ctl);
            const entity_t* pe = ph ? handle(p, access::get_u64(*ctl, *ph)) : nullptr;
            if (pe && world_pos(p, s, *pe, r.pos)) {
                r.has_pos = true;
                r.ground = (read_int(p, s.ground_flags, *pe) & 1) != 0;
            }
        }
        std::vector<cmd_rec_t>& q = s.cmds[c.slot];
        q.push_back(r);
        if ((int)q.size() > 2 * CMD_KEEP)
            q.erase(q.begin(), q.begin() + CMD_KEEP);
        for (size_t i = 0; i < s.unresolved.size();) {
            const nade_t& n = s.nades[s.unresolved[i]];
            if (n.thrower == c.slot && c.tick > n.start + 1) {
                resolve(s, s.unresolved[i]);
                s.unresolved.erase(s.unresolved.begin() + i);
            } else {
                i++;
            }
        }
    }

    int open_fire(parser_t& p, state_t& s) {
        fire_t f;
        f.start = p.tick;
        s.fires.push_back(f);
        return (int)s.fires.size() - 1;
    }

    std::string throw_json(const throw_t& t) {
        if (t.release < 0)
            return "[]";
        char buf[320];
        snprintf(buf, sizeof(buf), "[%d,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d]", t.release, t.pitch, t.yaw, t.at[0], t.at[1], t.at[2], t.from[0],
                 t.from[1], t.from[2], t.jump, t.keys, t.click, t.walked);
        return buf;
    }

    void link_fires(state_t& s) {
        for (fire_t& f : s.fires) {
            int best = -1;
            for (int i = 0; i < (int)s.nades.size(); i++) {
                const nade_t& n = s.nades[i];
                if (n.kind != w_molotov || n.thrower != f.owner || n.linked || n.det < 0)
                    continue;
                if (best < 0 || abs(n.det - f.start) < abs(s.nades[best].det - f.start))
                    best = i;
            }
            if (best >= 0) {
                s.nades[best].linked = true;
                f.nade = best;
            }
        }
    }

    void close_watch(parser_t& p, state_t& s, const live_t& w) {
        if (w.kind <= w_decoy && w.rec >= 0) {
            nade_t& n = s.nades[w.rec];
            if (n.det < 0) {
                n.det = p.tick;
                memcpy(n.pos, n.last, sizeof(n.pos));
            }
            if (n.end < 0 && n.kind != w_molotov)
                n.end = p.tick;
        } else if (w.kind == w_inferno && w.rec >= 0) {
            if (s.fires[w.rec].end < 0)
                s.fires[w.rec].end = p.tick;
        }
    }

    void track_nade(parser_t& p, state_t& s, const entity_t& e, live_t& w) {
        float pos[3];
        if (!world_pos(p, s, e, pos))
            return;
        if (w.rec < 0) {
            const field_ref_t* t = access::ref(s.thrower, p.lay, p.ser, e);
            int thrower = t ? slot_of(p, s, access::get_u64(e, *t)) : -1;
            if (thrower < 0 && t) {
                map_pawns(p, s);
                thrower = slot_of(p, s, access::get_u64(e, *t));
            }
            auto it = s.nade_of.find(e.index);
            if (it != s.nade_of.end() && s.nades[it->second].start == p.tick && s.nades[it->second].points == 0)
                w.rec = it->second;
            else
                w.rec = open_nade(p, s, w.kind, thrower);
            s.nade_of[e.index] = w.rec;
        }
        nade_t& n = s.nades[w.rec];
        if (n.det >= 0)
            return;
        if (n.points > 0 && pos[0] == n.last[0] && pos[1] == n.last[1] && pos[2] == n.last[2])
            return;
        memcpy(n.last, pos, sizeof(pos));
        n.points++;
        add(n.path, std::to_string(p.tick) + "," + num(pos[0]) + "," + num(pos[1]) + "," + num(pos[2]));
    }

    void track_fire(parser_t& p, state_t& s, const entity_t& e, live_t& w) {
        if (w.rec < 0) {
            auto it = s.fire_of.find(e.index);
            w.rec = it != s.fire_of.end() && !s.fires[it->second].bound && s.fires[it->second].end < 0 ? it->second : open_fire(p, s);
            s.fire_of[e.index] = w.rec;
            s.fires[w.rec].bound = true;
        }
        fire_t& f = s.fires[w.rec];
        if (f.owner < 0) {
            const field_ref_t* o = access::ref(s.owner, p.lay, p.ser, e);
            f.owner = o ? slot_of(p, s, access::get_u64(e, *o)) : -1;
        }
        const field_ref_t* fc = access::ref(s.fire_count, p.lay, p.ser, e);
        const field_ref_t* fp = access::ref(s.fire_pos, p.lay, p.ser, e);
        const field_ref_t* fo = access::ref(s.fire_on, p.lay, p.ser, e);
        if (!fc || !fp)
            return;
        int n = std::clamp((int)access::get_int(e, *fc), 0, 128);
        uint64_t mask[2] = {};
        bool moved = false;
        for (int i = 0; i < n; i++) {
            float v[3] = {};
            access::get_vec3(e, *fp, v, i);
            if (i >= (int)f.cells.size()) {
                f.cells.push_back({v[0], v[1]});
                moved = true;
            } else if (f.cells[i].first != v[0] || f.cells[i].second != v[1]) {
                f.cells[i] = {v[0], v[1]};
                moved = true;
            }
            if (!fo || access::get_bool(e, *fo, i))
                mask[i >> 6] |= 1ull << (i & 63);
        }
        if (!moved && mask[0] == f.mask[0] && mask[1] == f.mask[1])
            return;
        f.mask[0] = mask[0];
        f.mask[1] = mask[1];
        std::string list;
        for (int i = 0; i < n; i++)
            if (mask[i >> 6] >> (i & 63) & 1)
                add(list, num(f.cells[i].first) + "," + num(f.cells[i].second));
        add(f.frames, "[" + std::to_string(p.tick) + (list.empty() ? "" : ",") + list + "]");
    }

    void track_bomb(parser_t& p, state_t& s, const entity_t& e, live_t& w) {
        w.rec = 0;
        const field_ref_t* o = access::ref(s.owner, p.lay, p.ser, e);
        int slot = o ? slot_of(p, s, access::get_u64(e, *o)) : -1;
        if (slot >= 0) {
            if (slot != s.carrier) {
                float none[3] = {};
                bomb_row(s, p.tick, "carry", slot, none, 0);
            }
            s.carrier = slot;
            return;
        }
        float pos[3];
        if (!world_pos(p, s, e, pos))
            return;
        if (s.carrier == -1 && pos[0] == s.bomb_at[0] && pos[1] == s.bomb_at[1] && pos[2] == s.bomb_at[2])
            return;
        s.carrier = -1;
        memcpy(s.bomb_at, pos, sizeof(pos));
        bomb_row(s, p.tick, "drop", -1, pos, 0);
    }

    void track_planted(parser_t& p, state_t& s, const entity_t& e, live_t& w) {
        if (w.rec < 0) {
            w.rec = 0;
            float pos[3] = {};
            world_pos(p, s, e, pos);
            const field_ref_t* t = access::ref(s.timer, p.lay, p.ser, e);
            const field_ref_t* o = access::ref(s.owner, p.lay, p.ser, e);
            bomb_row(s, p.tick, "planted", o ? slot_of(p, s, access::get_u64(e, *o)) : -1, pos, t ? access::get_float(e, *t) : 0);
            s.carrier = -2;
            s.bomb_defusing = false;
        }
        bool now = read_bool(p, s.being_defused, e);
        if (now == s.bomb_defusing)
            return;
        s.bomb_defusing = now;
        const field_ref_t* d = access::ref(s.defuser_pawn, p.lay, p.ser, e);
        const field_ref_t* l = access::ref(s.defuse_len, p.lay, p.ser, e);
        float none[3] = {};
        bomb_row(s, p.tick, now ? "defuse" : "stop", d ? slot_of(p, s, access::get_u64(e, *d)) : -1, none, now && l ? access::get_float(e, *l) : 0);
    }

    void watch_all(parser_t& p, state_t& s, bool move) {
        for (int idx : p.ents.touched) {
            const entity_t* e = entities::get(p.ents, idx);
            if (!e || e->cls < 0 || e->cls >= (int)s.kinds.size() || s.kinds[e->cls] == w_none)
                continue;
            auto it = s.watch.find(idx);
            if (it != s.watch.end() && it->second.serial == e->serial && it->second.kind == s.kinds[e->cls])
                continue;
            if (it != s.watch.end())
                close_watch(p, s, it->second);
            live_t w;
            w.serial = e->serial;
            w.kind = s.kinds[e->cls];
            s.watch[idx] = w;
        }
        for (auto it = s.watch.begin(); it != s.watch.end();) {
            const entity_t* e = entities::get(p.ents, it->first);
            live_t& w = it->second;
            if (!e || e->serial != w.serial || e->cls < 0 || e->cls >= (int)s.kinds.size() || s.kinds[e->cls] != w.kind) {
                close_watch(p, s, w);
                it = s.watch.erase(it);
                continue;
            }
            if (!move && w.rec >= 0) {
                ++it;
                continue;
            }
            if (w.kind <= w_decoy)
                track_nade(p, s, *e, w);
            else if (w.kind == w_inferno)
                track_fire(p, s, *e, w);
            else if (w.kind == w_c4)
                track_bomb(p, s, *e, w);
            else if (w.kind == w_planted)
                track_planted(p, s, *e, w);
            ++it;
        }
    }

    void on_frame(parser_t& p, void* user) {
        state_t& s = *(state_t*)user;
        if (s.cancel && *s.cancel)
            p.stop = true;
        if (!p.ready)
            return;
        if (s.kinds.size() != p.ser.classes.size())
            classes(p, s);
        if (p.tick >= s.next) {
            s.next = p.tick + EVERY;
            sample(p, s);
        }
        bool move = p.tick >= s.next_nade;
        if (move)
            s.next_nade = p.tick + NADE_EVERY;
        if (move || !p.ents.touched.empty())
            watch_all(p, s, move);
    }

    int nade_by_entity(parser_t& p, state_t& s, const game_event_t& e, int kind) {
        int ent = (int)game_events::num(e, "entityid", -1);
        auto it = s.nade_of.find(ent);
        if (it != s.nade_of.end())
            return it->second;
        int i = open_nade(p, s, kind, (int)game_events::num(e, "userid", -1));
        s.nade_of[ent] = i;
        return i;
    }

    void detonate(parser_t& p, state_t& s, const game_event_t& e, int kind, bool ends) {
        nade_t& n = s.nades[nade_by_entity(p, s, e, kind)];
        if (n.det < 0) {
            n.det = p.tick;
            n.pos[0] = game_events::flt(e, "x", 0);
            n.pos[1] = game_events::flt(e, "y", 0);
            n.pos[2] = game_events::flt(e, "z", 0);
        }
        if (n.thrower < 0)
            n.thrower = (int)game_events::num(e, "userid", -1);
        if (ends)
            n.end = p.tick;
    }

    const entity_t* rules(parser_t& p, state_t& s) {
        const entity_t* r = entities::get(p.ents, s.rules);
        if (r && r->cls == s.rules_cls)
            return r;
        for (size_t i = 0; i < p.ents.list.size(); i++) {
            const entity_t* e = entities::get(p.ents, (int)i);
            if (e && e->cls == s.rules_cls) {
                s.rules = (int)i;
                return e;
            }
        }
        return nullptr;
    }

    void on_event(parser_t& p, const game_event_t& e, void* user) {
        state_t& s = *(state_t*)user;
        auto who = [&](const char* k) { return std::to_string(game_events::num(e, k, -1)); };
        switch (e.desc->kind) {
        case ek_round_prestart:
            if (!s.rounds.empty() && s.rounds.back().end < 0)
                s.rounds.back().end = p.tick;
            s.rounds.push_back({p.tick});
            break;
        case ek_round_freeze_end:
            if (!s.rounds.empty() && s.rounds.back().live < 0) {
                s.rounds.back().live = p.tick;
                const entity_t* r = rules(p, s);
                s.rounds.back().time = r ? read_int(p, s.round_time, *r, -1) : -1;
            }
            break;
        case ek_round_officially_ended:
            if (!s.rounds.empty() && s.rounds.back().end < 0)
                s.rounds.back().end = p.tick;
            break;
        case ek_round_announce_match_start: {
            s.live = true;
            s.kills.clear();
            s.hurts.clear();
            s.shots.clear();
            s.blinds.clear();
            s.bomb.clear();
            s.inventory.clear();
            for (inv_t& v : s.inv)
                v = inv_t();
            s.nades.clear();
            s.unresolved.clear();
            s.fires.clear();
            s.nade_of.clear();
            s.fire_of.clear();
            for (auto& [idx, w] : s.watch)
                w.rec = -1;
            s.carrier = -2;
            std::vector<round_t> open;
            for (const round_t& r : s.rounds)
                if (r.end < 0)
                    open.push_back(r);
            s.rounds = open;
            break;
        }
        case ek_player_death: {
            int flags = (game_events::num(e, "penetrated", 0) ? 1 : 0) | (game_events::num(e, "thrusmoke", 0) ? 2 : 0) |
                        (game_events::num(e, "attackerblind", 0) ? 4 : 0) | (game_events::num(e, "noscope", 0) ? 8 : 0) |
                        (game_events::num(e, "assistedflash", 0) ? 16 : 0) | (game_events::num(e, "attackerinair", 0) ? 32 : 0);
            add(s.kills, "[" + std::to_string(p.tick) + "," + who("attacker") + "," + who("userid") + "," + (game_events::num(e, "headshot", 0) ? "1" : "0") +
                             ",\"" + game_events::str(e, "weapon") + "\"," + who("assister") + "," + std::to_string(flags) + "]");
            break;
        }
        case ek_player_hurt:
            add(s.hurts, "[" + std::to_string(p.tick) + "," + who("attacker") + "," + who("userid") + "," + who("dmg_health") + "," + who("dmg_armor") + "," +
                             who("health") + "," + who("hitgroup") + ",\"" + game_events::str(e, "weapon") + "\"]");
            break;
        case ek_weapon_fire:
            add(s.shots, "[" + std::to_string(p.tick) + "," + who("userid") + ",\"" + game_events::str(e, "weapon") + "\"]");
            break;
        case ek_player_blind: {
            int nade = -1;
            auto it = s.nade_of.find((int)game_events::num(e, "entityid", -1));
            if (it != s.nade_of.end())
                nade = it->second;
            add(s.blinds, "[" + std::to_string(p.tick) + "," + who("userid") + "," + who("attacker") + "," +
                              std::to_string(game_events::flt(e, "blind_duration", 0)) + "," + std::to_string(nade) + "]");
            break;
        }
        case ek_smokegrenade_detonate:
            detonate(p, s, e, w_smoke, false);
            break;
        case ek_smokegrenade_expired:
            detonate(p, s, e, w_smoke, true);
            break;
        case ek_flashbang_detonate:
            detonate(p, s, e, w_flash, true);
            break;
        case ek_hegrenade_detonate:
            detonate(p, s, e, w_he, true);
            break;
        case ek_decoy_started:
            detonate(p, s, e, w_decoy, false);
            break;
        case ek_decoy_detonate:
            detonate(p, s, e, w_decoy, true);
            break;
        case ek_inferno_startburn: {
            int ent = (int)game_events::num(e, "entityid", -1);
            if (!s.fire_of.count(ent))
                s.fire_of[ent] = open_fire(p, s);
            break;
        }
        case ek_inferno_expire: {
            auto it = s.fire_of.find((int)game_events::num(e, "entityid", -1));
            if (it != s.fire_of.end() && s.fires[it->second].end < 0)
                s.fires[it->second].end = p.tick;
            break;
        }
        case ek_bomb_beginplant:
        case ek_bomb_abortplant:
        case ek_bomb_defused:
        case ek_bomb_exploded: {
            const char* kind = e.desc->kind == ek_bomb_beginplant ? "beginplant" : e.desc->kind == ek_bomb_abortplant ? "abortplant" : e.desc->kind == ek_bomb_defused ? "defused" : "exploded";
            float none[3] = {};
            bomb_row(s, p.tick, kind, (int)game_events::num(e, "userid", -1), none, 0);
            break;
        }
        default:
            break;
        }
    }

    state_t* begin(const std::atomic<bool>* cancel) {
        state_t* s = new state_t();
        s->cancel = cancel;
        return s;
    }

    void hook(state_t* s, callbacks_t& cb) {
        cb.user = s;
        cb.on_frame = on_frame;
        cb.on_event = on_event;
        cb.on_usercmd = on_usercmd;
    }

    bool extract(const std::string& demo, std::string& blob, std::string& err, const std::atomic<bool>* cancel) {
        parser_t* p = new parser_t();
        if (!parser::open(*p, demo.c_str())) {
            err = p->err;
            demo::close(p->demo);
            delete p;
            return false;
        }
        state_t* s = begin(cancel);
        callbacks_t cb;
        hook(s, cb);
        bool ran = parser::run(*p, cb);
        bool ok = finish(s, *p, ran, blob, err);
        demo::close(p->demo);
        delete p;
        return ok;
    }

    bool finish(state_t* s, parser_t& p, bool ran, std::string& blob, std::string& err) {
        bool ok = ran || s->sample_count > 0;
        if (s->cancel && *s->cancel)
            ok = false;
        if (!s->rounds.empty() && s->rounds.back().end < 0)
            s->rounds.back().end = p.tick;
        std::string players;
        for (int i = 0; i < SLOTS; i++)
            if (s->steamid[i])
                add(players, "[" + std::to_string(i) + ",\"" + std::to_string(s->steamid[i]) + "\"]");
        std::string rounds;
        for (const round_t& r : s->rounds)
            add(rounds, "[" + std::to_string(r.start) + "," + std::to_string(r.live) + "," + std::to_string(r.end) + "," + std::to_string(r.time) + "]");
        for (int i : s->unresolved)
            resolve(*s, i);
        link_fires(*s);
        for (const fire_t& f : s->fires)
            if (f.nade >= 0 && s->nades[f.nade].end < 0)
                s->nades[f.nade].end = f.end;
        std::string nades;
        for (const nade_t& n : s->nades)
            add(nades, "[\"" + std::string(NADE_NAMES[n.kind]) + "\"," + std::to_string(n.thrower) + "," + std::to_string(n.start) + "," +
                           std::to_string(n.det) + "," + std::to_string(n.end) + "," + num(n.pos[0]) + "," + num(n.pos[1]) + "," + num(n.pos[2]) + ",[" +
                           n.path + "]," + throw_json(n.thrown) + "]");
        std::string fires;
        for (const fire_t& f : s->fires)
            add(fires, "[" + std::to_string(f.nade) + "," + std::to_string(f.owner) + "," + std::to_string(f.start) + "," + std::to_string(f.end) + ",[" +
                           f.frames + "]]");
        std::string head = "{\"tick_interval\":" + std::to_string(p.tick_interval) + ",\"ticks\":" + std::to_string(p.tick) + ",\"map\":\"" +
                           p.demo.info.map + "\",\"players\":[" + players + "],\"rounds\":[" + rounds + "],\"kills\":[" + s->kills + "],\"hurts\":[" +
                           s->hurts + "],\"shots\":[" + s->shots + "],\"blinds\":[" + s->blinds + "],\"nades\":[" + nades + "],\"fires\":[" + fires +
                           "],\"bomb\":[" + s->bomb + "],\"inventory\":[" + s->inventory + "]}";
        blob.clear();
        uint32_t len = (uint32_t)head.size();
        put(blob, &MAGIC, 4);
        put(blob, &VERSION, 4);
        put(blob, &len, 4);
        blob += head;
        put(blob, &s->sample_count, 4);
        blob += s->samples;
        if (!ok && err.empty())
            err = p.err.empty() ? "cancelled" : p.err;
        delete s;
        return ok;
    }
}
