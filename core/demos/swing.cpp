#include "pch.h"

#include "demos/swing.h"
#include "demos/swing_table.h"

namespace swing {
    const int SLOTS = stats_t::SLOTS;
    const float ROUND_SECONDS = 115.f;
    const float BOMB_SECONDS = 40.f;
    const float ECO_FLOOR = 500.f;
    const double FLASH_SHARE = 0.15;
    const double TRADE_SHARE = 0.2;
    const double FINISH_SHARE = 0.5;
    const double PLANT_SHARE = 0.5;
    const int T = 2;
    const int CT = 3;

    struct walk_t {
        const round_info_t* info = nullptr;
        float interval = 0.f;
        bool in[SLOTS] = {};
        bool alive[SLOTS] = {};
        int side[SLOTS] = {};
        int equip[SLOTS] = {};
        int map = -1;
        int plant = -1;
        int planter = -1;
        size_t kill = 0;
        size_t bomb = 0;
        double out[SLOTS] = {};
    };

    int map_index(const std::string& name) {
        for (int i = 0; i < SWING_MAP_COUNT; i++)
            if (name == SWING_MAPS[i])
                return i;
        return -1;
    }

    bool settled(const swing_state_t& st) {
        return !st.ct || (!st.t && !st.planted);
    }

    double chance(const swing_state_t& st) {
        if (!st.ct)
            return 0;
        if (!st.t && !st.planted)
            return 1;
        int b = st.planted ? 1 : 0;
        double z = SWING_CELL[b][std::min(st.ct, 5)][std::min(st.t, 5)] + SWING_ECO[b] * st.eco + SWING_CLOCK[b] * st.clock;
        if (st.map >= 0 && st.map < SWING_MAP_COUNT)
            z += SWING_MAP[st.map][b];
        return 1.0 / (1.0 + exp(-z));
    }

    bool begin(walk_t& w, const swing_view_t& v, size_t k) {
        w = walk_t{};
        w.info = &(*v.infos)[k];
        w.interval = v.interval;
        w.map = map_index(v.map);
        int n[4] = {};
        for (int i = 0; i < SLOTS; i++) {
            const player_stats_t* ps = v.players[i];
            if (!ps || k >= ps->lines.size() || !ps->lines[k].in || !ps->lines[k].alive)
                continue;
            int side = ps->lines[k].side;
            if (side != T && side != CT)
                continue;
            w.in[i] = w.alive[i] = true;
            w.side[i] = side;
            w.equip[i] = k < ps->eco.size() ? ps->eco[k].equipment : 0;
            n[side]++;
        }
        return n[T] > 0 && n[CT] > 0;
    }

    swing_state_t state(const walk_t& w, int tick) {
        swing_state_t st;
        double sum[4] = {};
        int n[4] = {};
        for (int i = 0; i < SLOTS; i++)
            if (w.alive[i]) {
                n[w.side[i]]++;
                sum[w.side[i]] += w.equip[i];
            }
        st.ct = n[CT];
        st.t = n[T];
        st.planted = w.plant >= 0;
        st.map = w.map;
        if (n[CT] && n[T])
            st.eco = (float)log((sum[CT] / n[CT] + ECO_FLOOR) / (sum[T] / n[T] + ECO_FLOOR));
        float sec = (tick - (st.planted ? w.plant : w.info->start)) * w.interval;
        float part = std::clamp(sec / (st.planted ? BOMB_SECONDS : ROUND_SECONDS), 0.f, 1.f);
        st.clock = part * part * part;
        return st;
    }

    int last_tick(const round_info_t& ri) {
        if (ri.decided >= 0)
            return ri.decided;
        int tick = ri.start;
        for (const kill_t& kl : ri.kills)
            tick = std::max(tick, kl.tick);
        return tick;
    }

    int next(walk_t& w, int end, int& tick) {
        const round_info_t& ri = *w.info;
        while (w.bomb < ri.bomb.size() && ri.bomb[w.bomb].kind != 0)
            w.bomb++;
        bool kill = w.kill < ri.kills.size() && ri.kills[w.kill].tick <= end;
        bool plant = w.bomb < ri.bomb.size() && ri.bomb[w.bomb].tick <= end && w.plant < 0;
        if (!kill && !plant)
            return 0;
        if (plant && (!kill || ri.bomb[w.bomb].tick <= ri.kills[w.kill].tick)) {
            tick = ri.bomb[w.bomb].tick;
            return 2;
        }
        tick = ri.kills[w.kill].tick;
        return 1;
    }

    void spread(walk_t& w, int side, double amount) {
        int n = 0, all = 0;
        for (int i = 0; i < SLOTS; i++) {
            n += w.alive[i] && w.side[i] == side;
            all += w.in[i] && w.side[i] == side;
        }
        for (int i = 0; i < SLOTS; i++)
            if (w.side[i] == side && (n ? w.alive[i] : w.in[i]))
                w.out[i] += amount / (n ? n : all);
    }

    bool on(const walk_t& w, int slot, int side) {
        return slot >= 0 && slot < SLOTS && w.in[slot] && w.side[slot] == side;
    }

    void both(walk_t& w, double ct) {
        spread(w, CT, ct);
        spread(w, T, -ct);
    }

    void credit(walk_t& w, const kill_t& kl, int side, double amount) {
        if (!on(w, kl.killer, side)) {
            spread(w, side, amount);
            return;
        }
        double rest = amount;
        if ((kl.flags & kf_flash) && on(w, kl.assister, side)) {
            w.out[kl.assister] += FLASH_SHARE * amount;
            rest -= FLASH_SHARE * amount;
        }
        if (on(w, kl.traded, side)) {
            w.out[kl.traded] += TRADE_SHARE * amount;
            rest -= TRADE_SHARE * amount;
        }
        int total = 0;
        for (const auto& [slot, dmg] : kl.hurt)
            if (on(w, slot, side))
                total += dmg;
        double finish = total ? FINISH_SHARE * rest : rest;
        w.out[kl.killer] += finish;
        for (const auto& [slot, dmg] : kl.hurt)
            if (total && on(w, slot, side))
                w.out[slot] += (rest - finish) * dmg / total;
    }

    double kill(walk_t& w, kill_t& kl, int tick) {
        int victim = kl.victim;
        if (victim < 0 || victim >= SLOTS || !w.alive[victim])
            return 0;
        int side = w.side[victim];
        double before = chance(state(w, tick));
        w.alive[victim] = false;
        double d = chance(state(w, tick)) - before;
        double lost = side == CT ? d : -d;
        w.out[on(w, kl.killer, side) && kl.killer != victim ? kl.killer : victim] += lost;
        credit(w, kl, 5 - side, -lost);
        kl.swing = (int)lround(-lost * 10000);
        return d;
    }

    void plant(walk_t& w, const bomb_t& b) {
        double before = chance(state(w, b.tick));
        w.plant = b.tick;
        w.planter = b.slot;
        double d = chance(state(w, b.tick)) - before;
        spread(w, CT, d);
        if (on(w, b.slot, T)) {
            w.out[b.slot] += -d * PLANT_SHARE;
            spread(w, T, -d * (1 - PLANT_SHARE));
        } else {
            spread(w, T, -d);
        }
    }

    void finish(walk_t& w, double left) {
        const round_info_t& ri = *w.info;
        const bomb_t* end = !ri.bomb.empty() && ri.bomb.back().kind != 0 ? &ri.bomb.back() : nullptr;
        if (end && end->kind == 1 && on(w, end->slot, CT)) {
            w.out[end->slot] += left;
            spread(w, T, -left);
        } else if (end && end->kind == 2 && on(w, w.planter, T)) {
            w.out[w.planter] += -left * PLANT_SHARE;
            spread(w, T, -left * (1 - PLANT_SHARE));
            spread(w, CT, left);
        } else {
            both(w, left);
        }
    }

    void compute(swing_view_t& v) {
        for (size_t k = 0; k < v.infos->size() && k < v.winners->size(); k++) {
            int winner = (*v.winners)[k];
            walk_t w;
            if ((winner != T && winner != CT) || !begin(w, v, k))
                continue;
            round_info_t& ri = (*v.infos)[k];
            int end = last_tick(ri), tick = 0;
            double p = chance(state(w, ri.start));
            while (int what = next(w, end, tick)) {
                double now = chance(state(w, tick));
                both(w, now - p);
                if (what == 1)
                    kill(w, ri.kills[w.kill++], tick);
                else
                    plant(w, ri.bomb[w.bomb++]);
                p = chance(state(w, tick));
            }
            double now = chance(state(w, end));
            both(w, now - p);
            finish(w, (winner == CT ? 1.0 : 0.0) - now);
            for (int i = 0; i < SLOTS; i++)
                if (w.in[i])
                    v.players[i]->lines[k].swing = (int)lround(w.out[i] * 10000);
        }
    }

    void samples(const swing_view_t& v, std::vector<swing_sample_t>& out) {
        for (size_t k = 0; k < v.infos->size() && k < v.winners->size(); k++) {
            int winner = (*v.winners)[k];
            walk_t w;
            if ((winner != T && winner != CT) || !begin(w, v, k))
                continue;
            swing_state_t first = state(w, w.info->start);
            if (first.ct != 5 || first.t != 5)
                continue;
            round_info_t ri = (*v.infos)[k];
            w.info = &ri;
            int end = last_tick(ri), tick = 0;
            int step = std::max(1, (int)(1.f / v.interval));
            int at = ri.start;
            for (;;) {
                int what = next(w, end, tick);
                int until = what ? tick : end + 1;
                for (; at < until; at += step) {
                    swing_state_t st = state(w, at);
                    if (!settled(st))
                        out.push_back({st, winner == CT});
                }
                if (!what)
                    break;
                if (what == 1)
                    kill(w, ri.kills[w.kill++], tick);
                else
                    plant(w, ri.bomb[w.bomb++]);
            }
        }
    }
}
