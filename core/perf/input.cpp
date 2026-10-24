#include "pch.h"

#include "game/weapons.h"
#include "model/geom.h"
#include "perf/input.h"

namespace input {
    const double M_YAW = 0.022;
    const size_t WINDOW = 2000;
    const size_t MIN_MOVES = 500;
    const double ACCURATE_SPEED = 0.34;

    struct move_t {
        double d;
        double ulp;
    };

    double ulp_of(float x) {
        float f = fabsf(x);
        return (double)nextafterf(f, INFINITY) - (double)f;
    }

    bool on_grid(const move_t& x, double q) {
        double k = std::round(fabs(x.d) / q);
        return k >= 1 && fabs(fabs(x.d) - k * q) <= (1.5 + 0.5 * k) * x.ulp + k * q * 1e-6;
    }

    double refine(const std::vector<move_t>& v, size_t from, size_t to, double q) {
        for (int it = 0; it < 3 && q > 0; it++) {
            double sxy = 0, sxx = 0;
            for (size_t i = from; i < to; i++) {
                double x = fabs(v[i].d), k = std::round(x / q);
                if (k < 1 || fabs(x / q - k) > 0.25)
                    continue;
                sxy += x * k;
                sxx += k * k;
            }
            if (sxx > 0)
                q = sxy / sxx;
        }
        return q;
    }

    double step(const std::vector<move_t>& v, size_t from, size_t to, double& share) {
        std::vector<double> a;
        for (size_t i = from; i < to; i++)
            a.push_back(fabs(v[i].d));
        std::sort(a.begin(), a.end());
        std::vector<std::pair<double, int>> groups;
        double first = -1;
        for (double x : a) {
            if (first > 0 && x <= first * 1.001) {
                groups.back().first += x;
                groups.back().second++;
            } else {
                groups.push_back({x, 1});
                first = x;
            }
        }
        std::vector<std::pair<int, double>> top;
        for (auto& [sum, n] : groups)
            top.push_back({n, sum / n});
        std::partial_sort(top.begin(), top.begin() + std::min<size_t>(3, top.size()), top.end(),
                          [](const std::pair<int, double>& x, const std::pair<int, double>& y) { return x.first > y.first; });
        double best = 0, best_score = -1e300, best_hits = 0;
        for (size_t t = 0; t < top.size() && t < 3; t++) {
            for (int j = 1; j <= 8; j++) {
                double q = refine(v, from, to, top[t].second / j);
                if (q <= 0)
                    continue;
                double hits = 0, chance = 0;
                for (size_t i = from; i < to; i++) {
                    hits += on_grid(v[i], q);
                    chance += std::min(1.0, 2 * ((1.5 + 0.5 * std::round(fabs(v[i].d) / q)) * v[i].ulp + fabs(v[i].d) * 1e-6) / q);
                }
                if (hits - chance > best_score) {
                    best_score = hits - chance;
                    best = q;
                    best_hits = hits;
                }
            }
        }
        share = to > from ? best_hits / (to - from) : 0;
        return best;
    }

    double fps(const match_t& m, int slot) {
        if (slot < 0 || slot >= (int)m.players.size())
            return 0;
        const std::vector<view_t>& views = m.players[slot].views;
        std::vector<double> gaps;
        for (size_t i = 1; i < views.size(); i++) {
            double g = views[i].tp - views[i - 1].tp;
            if (g > 0 && g < 1.0)
                gaps.push_back(g);
        }
        if (gaps.size() < WINDOW)
            return 0;
        std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
        return 64.0 / gaps[gaps.size() / 2];
    }

    bool aimed_gun(uint32_t item) {
        if (item == 25 || item == 27 || item == 29 || item == 35 || item == 31)
            return false;
        return (item >= 1 && item < 42) || item == 60 || item == 61 || item == 63 || item == 64;
    }

    bool sniper(uint32_t item) {
        return item == 9 || item == 11 || item == 38 || item == 40;
    }

    bool rifle(uint32_t item) {
        return item == 7 || item == 8 || item == 10 || item == 13 || item == 16 || item == 39 || item == 60;
    }

    bool enemy_seen(const match_t& m, int slot, int team, int tick) {
        for (const player_t& en : m.players) {
            if (en.slot < 0 || en.slot == slot || slot >= 64)
                continue;
            const pstate_t* b = model::at(m, en.slot, tick);
            if (b && (b->flags & pf_alive) && b->team >= 2 && b->team != team && ((b->spotted[slot >> 5] >> (slot & 31)) & 1))
                return true;
        }
        return false;
    }

    shots_t shots(const match_t& m, int slot) {
        shots_t out;
        std::set<int> first_ticks;
        for (const fire_t& f : m.fires) {
            if (f.slot != slot)
                continue;
            const pstate_t* s = model::at(m, slot, f.tick);
            if (!s || !(s->flags & pf_alive) || !(s->flags & pf_live) || !enemy_seen(m, slot, s->team, f.tick))
                continue;
            if (rifle(f.item) && !(s->flags & pf_ducked)) {
                float top = weapons::max_speed(f.item, (s->flags & pf_scoped) != 0);
                if (top > 0) {
                    out.stops++;
                    out.stopped += std::hypot(s->vel[0], s->vel[1]) <= ACCURATE_SPEED * top;
                }
            }
            if (f.recoil < 0.5f && aimed_gun(f.item) && !sniper(f.item) && first_ticks.insert(f.tick).second)
                out.first++;
        }
        std::set<int> hit_ticks;
        for (const hurt_t& h : m.hurts) {
            if (h.attacker != slot || h.victim < 0 || !first_ticks.count(h.tick))
                continue;
            if (h.weapon.find("grenade") != std::string::npos || h.weapon == "inferno" || h.weapon == "molotov" || h.weapon.find("knife") != std::string::npos ||
                h.weapon == "taser" || h.weapon == "world")
                continue;
            const pstate_t* a = model::at(m, slot, h.tick);
            const pstate_t* v = model::at(m, h.victim, h.tick);
            if (a && v && a->team != v->team && hit_ticks.insert(h.tick).second)
                out.first_hits++;
        }
        return out;
    }

    double flat_speed(const pstate_t* s) {
        return std::hypot(s->vel[0], s->vel[1]);
    }

    swings_t swings(const match_t& m, int slot) {
        swings_t out;
        if (slot < 0 || slot >= (int)m.players.size() || m.tick_interval <= 0)
            return out;
        int rate = (int)std::lround(1.0 / m.tick_interval);
        std::vector<double> walks;
        for (const player_t& pl : m.players)
            for (const pstate_t& s : pl.ticks)
                if ((s.flags & pf_valid) && (s.flags & pf_alive) && (s.flags & pf_walking) && !(s.flags & pf_ducked) && s.vel[2] == 0)
                    walks.push_back(flat_speed(&s));
        double walk_top = 0;
        if (!walks.empty()) {
            size_t at = walks.size() * 999 / 1000;
            std::nth_element(walks.begin(), walks.begin() + at, walks.end());
            walk_top = walks[at];
        }
        std::vector<std::pair<int, float>> guns;
        for (const fire_t& f : m.fires)
            if (f.slot == slot)
                if (float v = weapons::max_speed(f.item, false); v > 0)
                    guns.push_back({f.tick, v});
        std::vector<int> steps;
        for (const sound_t& s : m.sounds)
            if (s.slot == slot && s.kind == sk_step)
                steps.push_back(s.tick);
        std::sort(steps.begin(), steps.end());
        if (guns.empty() || walk_top <= 0)
            return out;
        auto top_at = [&](int t) {
            auto it = std::lower_bound(guns.begin(), guns.end(), std::make_pair(t, 0.f));
            if (it == guns.end())
                return (double)guns.back().second;
            if (it != guns.begin() && t - std::prev(it)->first < it->first - t)
                --it;
            return (double)it->second;
        };
        int t = 1;
        while (t < m.max_tick) {
            const pstate_t* a0 = model::at(m, slot, t - 1);
            const pstate_t* a1 = model::at(m, slot, t);
            t++;
            if (!a0 || !a1 || !(a1->flags & pf_alive) || !(a1->flags & pf_live) || (a1->flags & pf_ducked))
                continue;
            double top = top_at(t - 1);
            if (flat_speed(a0) > ACCURATE_SPEED * top || flat_speed(a1) <= ACCURATE_SPEED * top)
                continue;
            int start = t - 1, stop = -1;
            double peak = 0;
            for (int u = start; u < start + 2 * rate && u < m.max_tick; u++) {
                const pstate_t* s = model::at(m, slot, u);
                const pstate_t* n = model::at(m, slot, u + 1);
                if (!s || !n || !(n->flags & pf_alive))
                    break;
                peak = std::max(peak, flat_speed(n));
                if (flat_speed(n) <= ACCURATE_SPEED * top) {
                    stop = u + 1;
                    break;
                }
            }
            if (stop < 0) {
                t = start + 2 * rate;
                while (t < m.max_tick) {
                    const pstate_t* s = model::at(m, slot, t);
                    if (!s || flat_speed(s) <= ACCURATE_SPEED * top)
                        break;
                    t++;
                }
                continue;
            }
            t = stop + 1;
            if (peak <= walk_top)
                continue;
            auto st = std::lower_bound(steps.begin(), steps.end(), start);
            bool heard = st != steps.end() && *st <= stop;
            bool kill = false;
            for (const death_t& d : m.deaths) {
                if (d.attacker != slot || d.victim < 0 || d.victim == slot || d.tick < start || d.tick > stop + 2 * rate)
                    continue;
                const pstate_t* v = model::at(m, d.victim, d.tick);
                kill |= v && v->team != a1->team;
            }
            out.total++;
            out.good += !heard || kill;
        }
        return out;
    }

    double sens(const match_t& m, int slot) {
        if (slot < 0 || slot >= (int)m.players.size())
            return 0;
        const std::vector<view_t>& views = m.players[slot].views;
        std::vector<move_t> v;
        for (size_t i = 1; i < views.size(); i++) {
            const view_t& a = views[i - 1];
            const view_t& b = views[i];
            if (b.tp - a.tp > 1.0 || b.tp <= a.tp)
                continue;
            const pstate_t* s0 = model::at(m, slot, (int)floor(a.tp));
            const pstate_t* s1 = model::at(m, slot, (int)floor(b.tp));
            if (!s0 || !s1 || !(s0->flags & s1->flags & pf_alive) || !(s0->flags & s1->flags & pf_live))
                continue;
            bool zoom = false;
            for (int t = (int)floor(a.tp) - 1; t <= (int)floor(b.tp) + 1 && !zoom; t++) {
                const pstate_t* s = model::at(m, slot, t);
                zoom = s && (s->flags & (pf_scoped | pf_zoomed));
            }
            if (zoom)
                continue;
            double d = geom::wrap_deg((double)b.yaw - a.yaw);
            if (d != 0.0)
                v.push_back({d, std::max(ulp_of(a.yaw), ulp_of(b.yaw))});
        }
        std::map<long long, size_t> votes;
        for (size_t from = 0; from + MIN_MOVES <= v.size(); from += WINDOW) {
            size_t to = std::min(v.size(), from + WINDOW);
            double share = 0;
            double q = step(v, from, to, share);
            if (q > 0 && share > 0.9)
                votes[std::llround(q / M_YAW * 1000)] += to - from;
        }
        long long best = 0;
        size_t most = 0;
        for (auto& [k, n] : votes)
            if (n > most) {
                most = n;
                best = k;
            }
        return best / 1000.0;
    }
}
