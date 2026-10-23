#include "pch.h"

#include "model/geom.h"
#include "perf/perf.h"

static double quantile(std::vector<double> v, double q) {
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    double at = q * (double)(v.size() - 1);
    size_t lo = (size_t)at;
    if (lo + 1 >= v.size())
        return v.back();
    return v[lo] + (v[lo + 1] - v[lo]) * (at - (double)lo);
}

static bool live_at(const match_t& m, int t) {
    return t >= 0 && t < (int)m.live.size() && m.live[t];
}

static int team_of(const match_t& m, int slot, int t) {
    const pstate_t* s = model::at(m, slot, t);
    return s ? s->team : 0;
}

static bool enemies(const match_t& m, int a, int b, int t) {
    int ta = team_of(m, a, t), tb = team_of(m, b, t);
    return ta >= 2 && tb >= 2 && ta != tb;
}

static vec3_t eye_of(const pstate_t& s) {
    return {s.pos[0], s.pos[1], s.pos[2] + (s.eye_z > 0 ? s.eye_z : 64.0)};
}

static int onset_of(const match_t& m, const map_geo_t& g, const vis_t& v, int slot, int target, int from, int look) {
    int k = from / v.stride;
    int stop = std::max(0, (from - look) / v.stride);
    while (k > stop && ((vis::seen(v, k * v.stride, slot) >> target) & 1))
        k--;
    if (k <= stop)
        return -1;
    int t0 = std::min(from, (k + 1) * v.stride);
    for (int t = k * v.stride + 1; t <= std::min(from, (k + 1) * v.stride); t++)
        if (vis::visible(m, g, t, slot, target)) {
            t0 = t;
            break;
        }
    return t0;
}

namespace perf {

const char* names[ps_count] = {"kd", "adr", "kast", "hs", "accuracy", "head_hits", "ttd", "reaction", "placement", "wallbang", "smoke"};

std::vector<std::pair<int, int>> rounds(const match_t& m) {
    std::vector<std::pair<int, int>> out;
    for (int t = 0; t < (int)m.live.size(); t++) {
        if (!m.live[t])
            continue;
        if (t == 0 || !m.live[t - 1])
            out.push_back({t, t});
        out.back().second = t;
    }
    return out;
}

bool gun(const std::string& weapon) {
    std::string w = weapon.rfind("weapon_", 0) == 0 ? weapon.substr(7) : weapon;
    if (w.empty() || w.rfind("knife", 0) == 0 || w == "bayonet")
        return false;
    static const char* other[] = {"hegrenade", "flashbang", "smokegrenade", "decoy", "molotov", "incgrenade", "inferno", "c4", "planted_c4", "taser", "world"};
    for (const char* o : other)
        if (w == o)
            return false;
    return true;
}

void measure(const match_t& m, const map_geo_t* g, const vis_t* v, int slot, perf_t& out) {
    out = perf_t{};
    if (slot < 0 || slot >= (int)m.players.size())
        return;
    const player_t& pl = m.players[slot];
    double dt = m.tick_interval > 0 ? m.tick_interval : 1.0 / 64;
    int trade = (int)std::lround(5.0 / dt);

    for (auto [s, e] : rounds(m)) {
        int mine = team_of(m, slot, s);
        if (mine < 2)
            continue;
        out.rounds++;
        int end = e + 1;
        bool k = false, a = false, t = false, died = false;
        for (const death_t& d : m.deaths) {
            if (d.tick < s || d.tick > end)
                continue;
            k |= d.attacker == slot && d.victim != slot && enemies(m, slot, d.victim, d.tick);
            a |= d.assister == slot && enemies(m, slot, d.victim, d.tick);
            if (d.victim != slot)
                continue;
            died = true;
            for (const death_t& r : m.deaths)
                if (r.victim == d.attacker && r.attacker >= 0 && r.attacker != slot && r.tick >= d.tick && r.tick <= d.tick + trade &&
                    team_of(m, r.attacker, r.tick) == mine)
                    t = true;
        }
        const pstate_t* st = model::at(m, slot, end);
        bool survived = !died && st && (st->flags & pf_alive);
        out.kast_rounds += k || a || t || survived;
    }

    for (const death_t& d : m.deaths) {
        if (d.tick < m.match_start || d.attacker != slot || d.victim == slot || !enemies(m, slot, d.victim, d.tick))
            continue;
        out.kills++;
        out.wallbangs += d.penetrated > 0;
        out.smokes += d.smoke;
    }

    for (const shot_t& sh : m.shots)
        out.shots += sh.slot == slot && live_at(m, sh.tick) && gun(sh.weapon);
    std::set<std::pair<int, int>> once;
    for (const hurt_t& h : m.hurts) {
        if (h.attacker != slot || !live_at(m, h.tick) || !gun(h.weapon) || !enemies(m, slot, h.victim, h.tick))
            continue;
        if (!once.insert({h.tick, h.victim}).second)
            continue;
        out.hits++;
        out.head_hits += h.hitgroup == 1;
    }

    if (g && v) {
        int gap = (int)std::lround(3.0 / dt), look = (int)std::lround(10.0 / dt), second = (int)std::lround(1.0 / dt);
        std::map<int, int> last;
        for (const hurt_t& h : m.hurts) {
            if (h.attacker != slot || !live_at(m, h.tick) || !gun(h.weapon) || !enemies(m, slot, h.victim, h.tick))
                continue;
            auto it = last.find(h.victim);
            bool fresh = it == last.end() || h.tick - it->second > gap;
            last[h.victim] = h.tick;
            if (!fresh)
                continue;
            int seen_at = -1;
            for (int tt = h.tick; tt >= h.tick - 2 && seen_at < 0; tt--)
                if (vis::visible(m, *g, tt, slot, h.victim))
                    seen_at = tt;
            if (seen_at < 0)
                continue;
            int on = onset_of(m, *g, *v, slot, h.victim, seen_at, look);
            if (on >= 0)
                out.ttd_ms.push_back((h.tick - on) * dt * 1000.0);
        }

        std::vector<double> clicks;
        for (const click_t& c : pl.clicks)
            clicks.push_back(c.tp);
        for (const player_t& en : m.players) {
            if (en.slot < 0 || en.slot == slot)
                continue;
            int last_vis = -1000000;
            bool was = false;
            for (int k = 1; k < v->ticks; k++) {
                int t = k * v->stride;
                uint64_t mask = vis::seen(*v, t, slot);
                bool now = (mask >> en.slot) & 1;
                if (now && !was && live_at(m, t - v->stride)) {
                    int t0 = t;
                    for (int tt = t - v->stride + 1; tt <= t; tt++)
                        if (vis::visible(m, *g, tt, slot, en.slot)) {
                            t0 = tt;
                            break;
                        }
                    const pstate_t* sp = model::at(m, slot, t0);
                    const pstate_t* se = model::at(m, en.slot, t0);
                    bool alone = (mask & ~(1ull << en.slot)) == 0;
                    if (sp && se && (sp->flags & pf_live) && t0 - last_vis >= gap && alone) {
                        float pitch, yaw;
                        if (model::view_at(m, slot, t0, pitch, yaw))
                            out.placement_deg.push_back(geom::angle_between(geom::forward(pitch, yaw), geom::sub(eye_of(*se), eye_of(*sp))));
                        bool held = false;
                        for (const press_t& pr : pl.presses)
                            held |= pr.down <= t0 && pr.up >= t0;
                        auto c = std::upper_bound(clicks.begin(), clicks.end(), (double)t0);
                        if (!held && c != clicks.end() && *c - t0 < second)
                            out.reaction_ms.push_back((*c - t0) * dt * 1000.0);
                    }
                }
                if (now)
                    last_vis = t;
                was = now;
            }
        }
    }

    const int32_t* sc = pl.score;
    out.value[ps_kd] = (double)sc[sc_kills] / std::max(1, sc[sc_deaths]);
    out.has[ps_kd] = out.rounds > 0;
    out.value[ps_adr] = out.rounds ? (double)sc[sc_damage] / out.rounds : 0;
    out.has[ps_adr] = out.rounds > 0;
    out.value[ps_kast] = out.rounds ? (double)out.kast_rounds / out.rounds : 0;
    out.has[ps_kast] = out.rounds > 0;
    out.value[ps_hs] = sc[sc_kills] ? (double)sc[sc_hs_kills] / sc[sc_kills] : 0;
    out.has[ps_hs] = sc[sc_kills] > 0;
    out.value[ps_accuracy] = out.shots ? (double)out.hits / out.shots : 0;
    out.has[ps_accuracy] = out.shots > 0;
    out.value[ps_head_hits] = out.hits ? (double)out.head_hits / out.hits : 0;
    out.has[ps_head_hits] = out.hits > 0;
    out.value[ps_wallbang] = out.kills ? (double)out.wallbangs / out.kills : 0;
    out.has[ps_wallbang] = out.kills > 0;
    out.value[ps_smoke] = out.kills ? (double)out.smokes / out.kills : 0;
    out.has[ps_smoke] = out.kills > 0;
    const std::vector<double>* lists[3] = {&out.ttd_ms, &out.reaction_ms, &out.placement_deg};
    for (int i = 0; i < 3; i++) {
        out.has[ps_ttd + i] = lists[i]->size() >= 3;
        out.value[ps_ttd + i] = out.has[ps_ttd + i] ? quantile(*lists[i], 0.5) : 0;
    }
}

}
