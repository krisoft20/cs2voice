#include "pch.h"

#include <mutex>
#include <unordered_set>

#include "dbg.h"
#include "geo/kv3.h"
#include "geo/vpk.h"
#include "model/match.h"
#include "parser/entities/access.h"
#include "parser/parser.h"
#include "parser/proto/proto.h"
#include "text.h"

struct match_builder_t {
    match_t* m = nullptr;
    const std::unordered_set<uint32_t>* steps = nullptr;
    int controller_cls = -1;
    int rules = -1;
    field_key_t c_name = access::key("m_iszPlayerName");
    field_key_t c_steam = access::key("m_steamID");
    field_key_t c_pawn = access::key("m_hPlayerPawn");
    field_key_t cell[3] = {access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellX"),
                           access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellY"),
                           access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_cellZ")};
    field_key_t vec[3] = {access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecX"),
                          access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecY"),
                          access::key("CBodyComponent.m_skeletonInstance.m_vecOrigin.m_vecZ")};
    field_key_t vel[3] = {access::key("m_vecVelocity.m_vecX"), access::key("m_vecVelocity.m_vecY"),
                          access::key("m_vecVelocity.m_vecZ")};
    field_key_t eye = access::key("m_angEyeAngles");
    field_key_t view_z = access::key("m_vecViewOffset.m_vecZ");
    field_key_t health = access::key("m_iHealth");
    field_key_t life = access::key("m_lifeState");
    field_key_t team = access::key("m_iTeamNum");
    field_key_t scoped = access::key("m_bIsScoped");
    field_key_t walking = access::key("m_bIsWalking");
    field_key_t ducked = access::key("m_pMovementServices.m_bDucked");
    field_key_t fov = access::key("m_pCameraServices.m_iFOV");
    field_key_t spotted = access::key("m_entitySpottedState.m_bSpottedByMask");
    field_key_t warmup = access::key("m_pGameRules.m_bWarmupPeriod");
    field_key_t freeze = access::key("m_pGameRules.m_bFreezePeriod");
    field_key_t started = access::key("m_pGameRules.m_bHasMatchStarted");
    field_key_t win = access::key("m_pGameRules.m_iRoundWinStatus");
    field_key_t score[sc_count] = {
        access::key("m_pActionTrackingServices.m_matchStats.m_iKills"),          access::key("m_pActionTrackingServices.m_matchStats.m_iDeaths"),
        access::key("m_pActionTrackingServices.m_matchStats.m_iAssists"),        access::key("m_pActionTrackingServices.m_matchStats.m_iDamage"),
        access::key("m_pActionTrackingServices.m_matchStats.m_iHeadShotKills"),  access::key("m_pActionTrackingServices.m_matchStats.m_iUtilityDamage"),
        access::key("m_pActionTrackingServices.m_matchStats.m_iEnemiesFlashed"), access::key("m_pActionTrackingServices.m_matchStats.m_iEnemy3Ks"),
        access::key("m_pActionTrackingServices.m_matchStats.m_iEnemy4Ks"),       access::key("m_pActionTrackingServices.m_matchStats.m_iEnemy5Ks")};
    int pawn_cls = -1;
    int rules_cls = -1;
    std::vector<std::pair<int, int>> offsets;
    std::vector<std::vector<uint64_t>> recent;
    std::vector<size_t> recent_pos;
    std::vector<uint64_t> last_click;
    std::vector<std::pair<float, float>> last_angles;
    std::vector<uint8_t> unsorted;
    std::vector<int> new_match;
};

static const size_t recent_len = 256;

static bool get_flag(parser_t& p, field_key_t& k, const entity_t& e) {
    const field_ref_t* r = access::ref(k, p.lay, p.ser, e);
    return r && access::get_bool(e, *r);
}

static int64_t get_int(parser_t& p, field_key_t& k, const entity_t& e, int64_t def) {
    const field_ref_t* r = access::ref(k, p.lay, p.ser, e);
    return r ? access::get_int(e, *r) : def;
}

static float get_float(parser_t& p, field_key_t& k, const entity_t& e) {
    const field_ref_t* r = access::ref(k, p.lay, p.ser, e);
    return r ? access::get_float(e, *r) : 0.f;
}

static bool round_live(parser_t& p, match_builder_t& b) {
    const entity_t* gr = entities::get(p.ents, b.rules);
    if (!gr || gr->cls != b.rules_cls) {
        gr = nullptr;
        b.rules = -1;
        for (size_t i = 0; i < p.ents.list.size(); i++) {
            const entity_t* e = entities::get(p.ents, (int)i);
            if (e && p.ser.classes[e->cls] == "CCSGameRulesProxy") {
                b.rules = (int)i;
                b.rules_cls = e->cls;
                gr = e;
                break;
            }
        }
        if (!gr)
            return false;
    }
    return get_flag(p, b.started, *gr) && !get_flag(p, b.warmup, *gr) && !get_flag(p, b.freeze, *gr) &&
           get_int(p, b.win, *gr, 1) == 0;
}

static void fill_pawn(parser_t& p, match_builder_t& b, const entity_t& pe, pstate_t& s) {
    for (int i = 0; i < 3; i++) {
        const field_ref_t* c = access::ref(b.cell[i], p.lay, p.ser, pe);
        const field_ref_t* v = access::ref(b.vec[i], p.lay, p.ser, pe);
        if (c && v)
            s.pos[i] = (float)((double)access::get_u64(pe, *c) * 512.0 - 16384.0 + access::get_float(pe, *v));
        s.vel[i] = get_float(p, b.vel[i], pe);
    }
    const field_ref_t* eye = access::ref(b.eye, p.lay, p.ser, pe);
    if (eye) {
        float a[3];
        if (access::get_vec3(pe, *eye, a)) {
            s.pitch = a[0];
            s.yaw = a[1];
        }
    }
    s.eye_z = get_float(p, b.view_z, pe);
    s.health = (int16_t)get_int(p, b.health, pe, 0);
    s.team = (uint8_t)get_int(p, b.team, pe, 0);
    if (get_int(p, b.life, pe, 1) == 0 && s.health > 0)
        s.flags |= pf_alive;
    if (get_flag(p, b.scoped, pe))
        s.flags |= pf_scoped;
    if (get_int(p, b.fov, pe, 0) != 0)
        s.flags |= pf_zoomed;
    if (get_flag(p, b.walking, pe))
        s.flags |= pf_walking;
    if (get_flag(p, b.ducked, pe))
        s.flags |= pf_ducked;
    const field_ref_t* sp = access::ref(b.spotted, p.lay, p.ser, pe);
    if (sp) {
        s.spotted[0] = (uint32_t)access::get_u64(pe, *sp, 0);
        s.spotted[1] = (uint32_t)access::get_u64(pe, *sp, 1);
    }
}

static void on_frame(parser_t& p, void* user) {
    match_builder_t& b = *(match_builder_t*)user;
    match_t& m = *b.m;
    if (!p.ready || p.tick < 0)
        return;
    int tick = p.tick;
    if (tick > m.max_tick)
        m.max_tick = tick;
    bool live = round_live(p, b);
    if (m.live.size() <= (size_t)tick)
        m.live.resize(tick + 1, 0);
    m.live[tick] = live;

    for (int idx = 1; idx <= 64; idx++) {
        const entity_t* c = entities::get(p.ents, idx);
        if (!c)
            continue;
        if (b.controller_cls < 0 && p.ser.classes[c->cls] == "CCSPlayerController")
            b.controller_cls = c->cls;
        if (c->cls != b.controller_cls)
            continue;
        player_t& pl = m.players[idx - 1];
        pl.slot = idx - 1;
        if (!pl.steamid) {
            const field_ref_t* r = access::ref(b.c_steam, p.lay, p.ser, *c);
            if (r)
                pl.steamid = access::get_u64(*c, *r);
        }
        if (pl.name.empty() || (tick & 1023) == 0) {
            const field_ref_t* r = access::ref(b.c_name, p.lay, p.ser, *c);
            const std::string* s = r ? access::get_str(*c, *r) : nullptr;
            if (s)
                pl.name = *s;
        }
        if ((tick & 15) == 0)
            for (int k = 0; k < sc_count; k++) {
                const field_ref_t* r = access::ref(b.score[k], p.lay, p.ser, *c);
                if (r)
                    pl.score[k] = (int32_t)access::get_int(*c, *r);
            }

        pstate_t s = {};
        s.pawn = -1;
        const field_ref_t* ph = access::ref(b.c_pawn, p.lay, p.ser, *c);
        if (ph) {
            int pawn = (int)(access::get_u64(*c, *ph) & 0x3fff);
            const entity_t* pe = entities::get(p.ents, pawn);
            if (pe && b.pawn_cls < 0 && p.ser.classes[pe->cls] == "CCSPlayerPawn")
                b.pawn_cls = pe->cls;
            if (pe && pe->cls == b.pawn_cls) {
                s.pawn = pawn;
                s.flags |= pf_valid;
                fill_pawn(p, b, *pe, s);
            }
        }
        if (live)
            s.flags |= pf_live;
        if (pl.ticks.size() <= (size_t)tick)
            pl.ticks.resize(tick + 1, pstate_t{});
        pl.ticks[tick] = s;
    }
}

static void on_usercmd(parser_t&, const usercmd_t& c, void* user) {
    match_builder_t& b = *(match_builder_t*)user;
    match_t& m = *b.m;
    if (c.slot < 0 || c.slot >= (int)m.players.size() || c.tick < 0)
        return;
    player_t& pl = m.players[c.slot];
    if (pl.cmds.empty() || c.number > pl.cmds.back().number || pl.cmds.back().number - c.number > 4096)
        pl.cmds.push_back({c.tick, c.number, c.pitch, c.yaw, c.mouse_dx, c.mouse_dy, (uint32_t)(c.buttons[0] & 0xffffff),
                           (uint16_t)std::min<size_t>(c.history.size(), 0xffff)});
    int off = c.server_tick > 0 ? c.server_tick - c.tick : 0;
    if (c.server_tick > 0) {
        bool found = false;
        for (auto& o : b.offsets)
            if (o.first == off) {
                o.second++;
                found = true;
                break;
            }
        if (!found)
            b.offsets.push_back({off, 1});
    }

    if (c.sent_subtick) {
        for (const subtick_t& s : c.subtick) {
            if (s.button != in_attack)
                continue;
            double t = (double)(c.tick - 1) + s.when;
            if (s.pressed) {
                pl.pressed = t;
            } else if (pl.pressed >= 0) {
                pl.presses.push_back({pl.pressed, t});
                pl.pressed = -1;
            }
        }
    }
    if (!c.sent_history)
        return;

    std::vector<uint64_t>& recent = b.recent[c.slot];
    for (size_t i = 0; i < c.history.size(); i++) {
        const history_t& h = c.history[i];
        if (!h.has_angles || (c.server_tick > 0 && (double)h.player_tick + h.player_frac >= c.server_tick + 1.0))
            continue;
        uint32_t frac;
        memcpy(&frac, &h.player_frac, 4);
        uint64_t key = ((uint64_t)(uint32_t)h.player_tick << 32) | frac;
        std::pair<float, float>& last = b.last_angles[c.slot];
        bool stale = !h.fresh && (h.pitch != last.first || h.yaw != last.second);
        bool dup = false;
        if (key <= pl.last_view)
            dup = std::find(recent.begin(), recent.end(), key) != recent.end();
        if (!dup && !stale) {
            last = {h.pitch, h.yaw};
            if (key < pl.last_view)
                b.unsorted[c.slot] = 1;
            else
                pl.last_view = key;
            recent[b.recent_pos[c.slot]] = key;
            b.recent_pos[c.slot] = (b.recent_pos[c.slot] + 1) % recent_len;
            view_t v;
            v.tp = (double)h.player_tick + h.player_frac - off - 1;
            v.tr = (double)h.render_tick + h.render_frac - off;
            v.pitch = h.pitch;
            v.yaw = h.yaw;
            pl.views.push_back(v);
        }
        if ((int)i == c.attack1 && key != b.last_click[c.slot]) {
            b.last_click[c.slot] = key;
            click_t ck = {c.tick, (double)h.player_tick + h.player_frac - off - 1, stale ? last.first : h.pitch, stale ? last.second : h.yaw, (double)h.render_tick + h.render_frac - off};
            pl.clicks.push_back(ck);
        }
    }
}

static void on_bullet(parser_t&, const bullet_t& bl, void* user) {
    match_builder_t& b = *(match_builder_t*)user;
    match_t& m = *b.m;
    int tick = bl.tick < 0 ? 0 : bl.tick;
    int slot = model::slot_of_pawn(m, bullets::pawn(bl), tick);
    if (slot < 0)
        return;
    m.fires.push_back({tick, slot, bl.pitch, bl.yaw, bl.item_def, bl.recoil_index, bl.inaccuracy, bl.spread});
}

static int pawn_of(int64_t handle) {
    return (int)((uint32_t)handle & 0x3fff);
}

static uint32_t murmur2(const std::string& s, uint32_t seed) {
    const uint32_t mul = 0x5bd1e995;
    uint32_t len = (uint32_t)s.size(), h = seed ^ len;
    const uint8_t* d = (const uint8_t*)s.data();
    for (; len >= 4; d += 4, len -= 4) {
        uint32_t k;
        memcpy(&k, d, 4);
        k *= mul;
        k ^= k >> 24;
        k *= mul;
        h = h * mul ^ k;
    }
    switch (len) {
    case 3: h ^= d[2] << 16; [[fallthrough]];
    case 2: h ^= d[1] << 8; [[fallthrough]];
    case 1: h ^= d[0]; h *= mul;
    }
    h ^= h >> 13;
    h *= mul;
    return h ^ (h >> 15);
}

static const std::unordered_set<uint32_t>* step_sounds() {
    static std::unordered_set<uint32_t> set;
    static std::once_flag once;
    std::call_once(once, [] {
        std::string maps = vpk::maps_dir(), err;
        std::vector<uint8_t> file;
        const uint8_t* blk = nullptr;
        size_t len = 0;
        kv_node_t root;
        if (maps.empty() || !vpk::extract(maps.substr(0, maps.rfind('\\')) + "\\pak01_dir.vpk", "soundevents/game_sounds_footsteps.vsndevts_c", file, err) ||
            !vpk::block(file, "DATA", blk, len) || !kv3::read(blk, len, root, err)) {
            dbg::print("[model] no footstep sound names, steps come from player_footstep only: %s", err.c_str());
            return;
        }
        for (const std::string& k : root.keys) {
            std::string low = text::lower(k);
            if (low.find("silent") == std::string::npos && low[0] != '_')
                set.insert(murmur2(low, 0x53524332));
        }
    });
    return set.empty() ? nullptr : &set;
}

static void on_msg(parser_t& p, const msg_t& msg, void* user) {
    match_builder_t& b = *(match_builder_t*)user;
    if (msg.type != ge_sos_start_sound || !b.steps)
        return;
    uint32_t hash = 0;
    int ent = -1;
    proto_t r = proto::make(msg.data, msg.len);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 2 && f.wire == 5)
            hash = (uint32_t)f.val;
        else if (f.num == 3 && f.wire == 0)
            ent = (int)f.val;
    }
    if (!b.steps->count(hash))
        return;
    int tick = p.tick < 0 ? 0 : p.tick;
    int slot = model::slot_of_pawn(*b.m, ent, tick);
    if (slot >= 0)
        b.m->sounds.push_back({tick, slot, sk_step});
}

static void on_event(parser_t& p, const game_event_t& e, void* user) {
    match_builder_t& b = *(match_builder_t*)user;
    match_t& m = *b.m;
    const std::string& name = e.desc->name;
    int tick = p.tick < 0 ? 0 : p.tick;
    if ((name == "player_footstep" && !b.steps) || name == "weapon_fire" || name == "weapon_reload") {
        int slot = model::slot_of_pawn(m, pawn_of(game_events::num(e, "userid_pawn", -1)), tick);
        if (slot < 0)
            return;
        uint8_t kind = name == "player_footstep" ? sk_step : name == "weapon_fire" ? sk_fire : sk_reload;
        m.sounds.push_back({tick, slot, kind});
        if (kind == sk_fire)
            m.shots.push_back({tick, slot, game_events::str(e, "weapon")});
    } else if (name == "bullet_damage") {
        hit_t h;
        h.tick = tick;
        h.attacker = model::slot_of_pawn(m, pawn_of(game_events::num(e, "attacker_pawn", -1)), tick);
        h.victim = model::slot_of_pawn(m, pawn_of(game_events::num(e, "victim_pawn", -1)), tick);
        h.pitch = game_events::flt(e, "shoot_ang_x", 0.f);
        h.yaw = game_events::flt(e, "shoot_ang_y", 0.f);
        h.attack_t = (double)game_events::num(e, "attack_tick_count", 0) + game_events::flt(e, "attack_tick_frac", 0.f);
        h.render_t = (double)game_events::num(e, "render_tick_count", 0) + game_events::flt(e, "render_tick_frac", 0.f);
        h.distance = game_events::flt(e, "distance", 0.f);
        h.recoil = game_events::flt(e, "recoil_index", 0.f);
        m.hits.push_back(h);
    } else if (name == "smokegrenade_detonate") {
        smoke_t sm;
        sm.start = tick;
        sm.end = 1 << 30;
        sm.entity = (int)game_events::num(e, "entityid", -1);
        sm.x = game_events::flt(e, "x", 0.f);
        sm.y = game_events::flt(e, "y", 0.f);
        sm.z = game_events::flt(e, "z", 0.f);
        m.smokes.push_back(sm);
    } else if (name == "smokegrenade_expired") {
        int id = (int)game_events::num(e, "entityid", -1);
        for (smoke_t& sm : m.smokes)
            if (sm.entity == id && sm.end == 1 << 30)
                sm.end = tick;
    } else if (name == "player_blind") {
        m.blinds.push_back({tick, (int)game_events::num(e, "userid", -1), game_events::flt(e, "blind_duration", 0.f)});
    } else if (name == "player_hurt") {
        hurt_t h;
        h.tick = tick;
        h.attacker = model::slot_of_pawn(m, pawn_of(game_events::num(e, "attacker_pawn", -1)), tick);
        h.victim = model::slot_of_pawn(m, pawn_of(game_events::num(e, "userid_pawn", -1)), tick);
        h.damage = (int)game_events::num(e, "dmg_health", 0);
        h.armor = (int)game_events::num(e, "dmg_armor", 0);
        h.health = (int)game_events::num(e, "health", 0);
        h.hitgroup = (int)game_events::num(e, "hitgroup", 0);
        h.weapon = game_events::str(e, "weapon");
        m.hurts.push_back(h);
    } else if (name == "begin_new_match") {
        b.new_match.push_back(tick);
    } else if (name == "round_end") {
        m.round_ends.push_back({tick, (int)game_events::num(e, "winner", 0), (int)game_events::num(e, "reason", 0)});
    } else if (name == "player_death") {
        death_t d;
        d.tick = tick;
        d.attacker = model::slot_of_pawn(m, pawn_of(game_events::num(e, "attacker_pawn", -1)), tick);
        d.victim = model::slot_of_pawn(m, pawn_of(game_events::num(e, "userid_pawn", -1)), tick);
        d.assister = model::slot_of_pawn(m, pawn_of(game_events::num(e, "assister_pawn", -1)), tick);
        d.headshot = game_events::num(e, "headshot", 0) != 0;
        d.smoke = game_events::num(e, "thrusmoke", 0) != 0;
        d.penetrated = (int)game_events::num(e, "penetrated", 0);
        d.weapon = game_events::str(e, "weapon");
        m.deaths.push_back(d);
    }
}

namespace model {

const pstate_t* at(const match_t& m, int slot, int tick) {
    if (slot < 0 || slot >= (int)m.players.size() || tick < 0)
        return nullptr;
    const player_t& pl = m.players[slot];
    if ((size_t)tick >= pl.ticks.size() || !(pl.ticks[tick].flags & pf_valid))
        return nullptr;
    return &pl.ticks[tick];
}

bool view_at(const match_t& m, int slot, int tick, float& pitch, float& yaw) {
    const pstate_t* s = at(m, slot, tick);
    if (!s)
        return false;
    pitch = s->pitch;
    yaw = s->yaw;
    const std::vector<view_t>& vs = m.players[slot].views;
    auto it = std::upper_bound(vs.begin(), vs.end(), (double)tick, [](double t, const view_t& v) { return t < v.tp; });
    if (it != vs.begin() && (double)tick - (it - 1)->tp < 2.0) {
        pitch = (it - 1)->pitch;
        yaw = (it - 1)->yaw;
    }
    return true;
}

int slot_of_pawn(const match_t& m, int pawn, int tick) {
    if (pawn <= 0 || pawn >= 0x3fff)
        return -1;
    for (int back = 0; back < 64; back++) {
        int t = tick - back;
        if (t < 0)
            break;
        for (const player_t& pl : m.players)
            if ((size_t)t < pl.ticks.size() && pl.ticks[t].pawn == pawn)
                return pl.slot;
    }
    return -1;
}

match_builder_t* begin(match_t& m, parser_t& p) {
    m = match_t{};
    m.map = p.demo.info.map;
    m.players.resize(64);
    match_builder_t* b = new match_builder_t();
    b->m = &m;
    b->recent.assign(64, std::vector<uint64_t>(recent_len, ~0ull));
    b->recent_pos.assign(64, 0);
    b->last_click.assign(64, ~0ull);
    b->last_angles.assign(64, {1e9f, 1e9f});
    b->unsorted.assign(64, 0);
    b->steps = step_sounds();
    return b;
}

void hook(match_builder_t* b, callbacks_t& cb) {
    cb.user = b;
    cb.on_frame = on_frame;
    cb.on_usercmd = on_usercmd;
    cb.on_event = on_event;
    cb.on_bullet = on_bullet;
    cb.on_msg = on_msg;
}

bool build(match_t& m, const char* path, std::string& err) {
    parser_t* p = new parser_t();
    if (!parser::open(*p, path)) {
        err = p->err;
        demo::close(p->demo);
        delete p;
        return false;
    }
    match_builder_t* b = begin(m, *p);
    callbacks_t cb;
    hook(b, cb);
    bool ok = parser::run(*p, cb);
    if (!ok)
        err = p->err;
    finish(b, *p);
    demo::close(p->demo);
    delete p;
    return ok;
}

void finish(match_builder_t* bp, parser_t& p) {
    match_builder_t& b = *bp;
    match_t& m = *b.m;
    m.tick_interval = p.tick_interval;
    int best = 0;
    for (auto& o : b.offsets)
        if (o.second > best) {
            best = o.second;
            m.server_offset = o.first;
        }
    for (hit_t& h : m.hits) {
        h.attack_t -= m.server_offset;
        h.render_t -= m.server_offset;
    }

    for (size_t s = 0; s < m.players.size(); s++) {
        player_t& pl = m.players[s];
        if (b.unsorted[s])
            std::stable_sort(pl.views.begin(), pl.views.end(), [](const view_t& a, const view_t& c) { return a.tp < c.tp; });
        if (pl.ticks.size() < (size_t)m.max_tick + 1 && pl.slot >= 0)
            pl.ticks.resize(m.max_tick + 1, pstate_t{});
    }
    if (m.live.size() < (size_t)m.max_tick + 1)
        m.live.resize(m.max_tick + 1, 0);
    for (int t : b.new_match) {
        bool after = false;
        for (size_t k = t; k < m.live.size() && !after; k++)
            after = m.live[k];
        if (after)
            m.match_start = std::max(m.match_start, t);
    }
    for (int t = 0; t < m.match_start && t < (int)m.live.size(); t++) {
        m.live[t] = 0;
        for (player_t& pl : m.players)
            if ((size_t)t < pl.ticks.size())
                pl.ticks[t].flags &= ~pf_live;
    }

    std::vector<int> lasted;
    for (const smoke_t& sm : m.smokes)
        if (sm.end != 1 << 30)
            lasted.push_back(sm.end - sm.start);
    std::sort(lasted.begin(), lasted.end());
    int typical = lasted.empty() ? 0 : lasted[lasted.size() / 2];
    for (smoke_t& sm : m.smokes) {
        if (sm.end != 1 << 30)
            continue;
        int end = typical ? sm.start + typical : m.max_tick;
        for (int t = sm.start + 1; t < end && t < (int)m.live.size(); t++) {
            if (m.live[t - 1] && !m.live[t]) {
                end = t;
                break;
            }
        }
        sm.end = end;
    }
    delete bp;
}

}
