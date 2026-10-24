#include "pch.h"

#include "demos/stats.h"

namespace stats {
    const float TRADE_SECONDS = 5.f;
    const float FLASHED_SECONDS = 1.f;
    const int HEAD = 1;

    int pawn_of(const game_event_t& e, const char* key) {
        return (int)((uint32_t)game_events::num(e, key, -1) & 0x3fff);
    }

    int slot_of(const game_event_t& e, const char* key) {
        int slot = (int)game_events::num(e, key, -1);
        return slot >= 0 && slot < stats_t::SLOTS ? slot : -1;
    }

    bool own(const stats_t& s, const game_event_t& e, int slot, const char* pawn_key) {
        if (slot < 0)
            return false;
        auto it = s.owner.find(pawn_of(e, pawn_key));
        return it == s.owner.end() || it->second == slot;
    }

    int team_of(stats_t& s, parser_t& p, int slot) {
        const entity_t* c = entities::get(p.ents, slot + 1);
        const field_ref_t* f = c ? access::ref(s.team, p.lay, p.ser, *c) : nullptr;
        return f ? (int)access::get_int(*c, *f) : -1;
    }

    void scores(stats_t& s, parser_t& p, int* out) {
        if (s.team_cls < 0)
            for (size_t i = 0; i < p.ser.classes.size(); i++)
                if (p.ser.classes[i] == "CCSTeam")
                    s.team_cls = (int)i;
        for (const entity_t& e : p.ents.list) {
            if (!e.active || e.cls != s.team_cls)
                continue;
            const field_ref_t* t = access::ref(s.team, p.lay, p.ser, e);
            const field_ref_t* sc = access::ref(s.score, p.lay, p.ser, e);
            int side = t ? (int)access::get_int(e, *t) : -1;
            if (sc && side >= 2 && side <= 3)
                out[side] = (int)access::get_int(e, *sc);
        }
    }

    int round_winner(stats_t& s, parser_t& p) {
        int now[4] = {};
        scores(s, p, now);
        for (int side = 2; side <= 3; side++)
            if (now[side] > s.start_score[side])
                return side;
        return 0;
    }

    const entity_t* rules(stats_t& s, parser_t& p) {
        const entity_t* r = entities::get(p.ents, s.rules);
        if (r && p.ser.classes[r->cls] == "CCSGameRulesProxy")
            return r;
        for (size_t i = 0; i < p.ents.list.size(); i++) {
            const entity_t* e = entities::get(p.ents, (int)i);
            if (e && p.ser.classes[e->cls] == "CCSGameRulesProxy") {
                s.rules = (int)i;
                return e;
            }
        }
        return nullptr;
    }

    bool in_play(stats_t& s, parser_t& p) {
        if (!s.live)
            return false;
        const entity_t* r = rules(s, p);
        if (!r)
            return true;
        const field_ref_t* f = access::ref(s.freeze, p.lay, p.ser, *r);
        const field_ref_t* w = access::ref(s.win, p.lay, p.ser, *r);
        return !(f && access::get_bool(*r, *f)) && !(w && access::get_int(*r, *w) != 0);
    }

    bool is_gun(const std::string& weapon) {
        static const char* NOT_GUNS[] = {"knife", "bayonet", "grenade", "flashbang", "molotov", "decoy", "c4", "taser", "inferno", "world"};
        for (const char* n : NOT_GUNS)
            if (weapon.find(n) != std::string::npos)
                return false;
        return !weapon.empty();
    }

    void end_round(stats_t& s, parser_t& p) {
        if (!s.in_round)
            return;
        int winner = s.winner ? s.winner : round_winner(s, p);
        s.winners.push_back(winner);
        s.info.decided = s.decided;
        s.infos.push_back(s.info);
        for (int i = 0; i < stats_t::SLOTS; i++) {
            const round_t& r = s.round[i];
            player_stats_t& ps = s.players[i];
            ps.lines.push_back(r.in ? round_line_t{true, r.died, r.kills, r.damage, r.side} : round_line_t{});
            if (!r.in)
                continue;
            ps.rounds++;
            if (r.kill || r.assist || (r.alive && !r.died) || r.traded)
                ps.kast++;
            ps.multi[std::min(r.kills, 5)]++;
            if (r.clutch_vs > 0) {
                ps.clutch_rounds++;
                ps.clutches += r.side == winner;
            }
        }
        s.in_round = false;
    }

    void start_round(stats_t& s, parser_t& p) {
        end_round(s, p);
        for (int i = 0; i < stats_t::SLOTS; i++) {
            s.round[i] = round_t{};
            const entity_t* c = entities::get(p.ents, i + 1);
            const field_ref_t* f = c ? access::ref(s.alive, p.lay, p.ser, *c) : nullptr;
            int side = team_of(s, p, i);
            s.round[i].in = side == 2 || side == 3;
            s.round[i].alive = s.round[i].in && f && access::get_bool(*c, *f);
            s.round[i].side = side;
            const field_ref_t* pf = c && s.round[i].in ? access::ref(s.ping, p.lay, p.ser, *c) : nullptr;
            int ms = pf ? (int)access::get_int(*c, *pf) : 0;
            if (ms > 0 && ms < 1000) {
                s.players[i].ping_sum += ms;
                s.players[i].ping_n++;
            }
        }
        s.deaths.clear();
        s.info = round_info_t{};
        s.info.start = p.tick;
        s.first_kill = false;
        s.winner = 0;
        for (int& c : s.clutch_slot)
            c = -1;
        scores(s, p, s.start_score);
        s.in_round = true;
        s.decided = -1;
    }

    void check_clutch(stats_t& s) {
        int alive[4] = {}, last[4] = {-1, -1, -1, -1};
        for (int i = 0; i < stats_t::SLOTS; i++) {
            const round_t& r = s.round[i];
            if (!r.alive || r.died || r.side < 2 || r.side > 3)
                continue;
            alive[r.side]++;
            last[r.side] = i;
        }
        for (int side = 2; side <= 3; side++) {
            int enemies = alive[5 - side];
            if (alive[side] == 1 && enemies > 0 && s.clutch_slot[side] < 0) {
                s.clutch_slot[side] = last[side];
                s.round[last[side]].clutch_vs = enemies;
            }
        }
    }

    void hurt(stats_t& s, parser_t& p, const game_event_t& e) {
        int victim = slot_of(e, "userid"), attacker = slot_of(e, "attacker");
        int vp = pawn_of(e, "userid_pawn");
        int health = (int)game_events::num(e, "health", 0);
        auto it = s.hp.find(vp);
        int removed = (it == s.hp.end() ? 100 : it->second) - health;
        s.hp[vp] = health;
        if (!s.live || victim < 0 || attacker == victim || !own(s, e, attacker, "attacker_pawn"))
            return;
        if (team_of(s, p, attacker) == team_of(s, p, victim))
            return;
        if (removed > 0) {
            s.players[attacker].damage += removed;
            s.round[attacker].damage += removed;
        }
        if (is_gun(game_events::str(e, "weapon")) && s.hits.insert({attacker, p.tick, victim}).second)
            s.pending.push_back({attacker, true, game_events::num(e, "hitgroup", 0) == HEAD});
    }

    void death(stats_t& s, parser_t& p, const game_event_t& e) {
        int victim = slot_of(e, "userid"), killer = slot_of(e, "attacker"), assister = slot_of(e, "assister");
        if (victim < 0)
            return;
        int side = team_of(s, p, victim);
        bool open = s.decided < 0 || p.tick <= s.decided;
        kill_t kl;
        kl.tick = p.tick;
        kl.victim = victim;
        kl.killer = killer;
        kl.assister = assister;
        kl.weapon = game_events::str(e, "weapon");
        kl.flags = (game_events::num(e, "headshot", 0) ? kf_head : 0) | (game_events::num(e, "penetrated", 0) > 0 ? kf_wall : 0) |
                   (game_events::num(e, "thrusmoke", 0) ? kf_smoke : 0) | (game_events::num(e, "attackerblind", 0) ? kf_blind : 0) |
                   (game_events::num(e, "noscope", 0) ? kf_noscope : 0) | (game_events::num(e, "assistedflash", 0) ? kf_flash : 0) |
                   (!s.first_kill ? kf_opening : 0);
        if (own(s, e, victim, "userid_pawn")) {
            s.players[victim].deaths++;
            s.round[victim].died = s.round[victim].died || open;
        }
        if (killer >= 0 && killer != victim && own(s, e, killer, "attacker_pawn") && team_of(s, p, killer) != side) {
            player_stats_t& k = s.players[killer];
            k.kills++;
            k.hs_kills += game_events::num(e, "headshot", 0) != 0;
            k.wallbang_kills += game_events::num(e, "penetrated", 0) > 0;
            k.smoke_kills += game_events::num(e, "thrusmoke", 0) != 0;
            s.round[killer].kill = s.round[killer].kill || open;
            s.round[killer].kills++;
            if (!s.first_kill) {
                k.entry_kills++;
                if (own(s, e, victim, "userid_pawn"))
                    s.players[victim].entry_deaths++;
            }
            int window = (int)(TRADE_SECONDS / p.tick_interval);
            bool trade = false;
            for (const fall_t& d : s.deaths)
                if (d.killer == victim && p.tick - d.tick <= window && team_of(s, p, d.victim) == team_of(s, p, killer)) {
                    s.round[d.victim].traded = true;
                    trade = true;
                }
            k.trade_kills += trade;
            if (trade)
                kl.flags |= kf_trade;
        }
        s.info.kills.push_back(kl);
        s.first_kill = true;
        if (own(s, e, assister, "assister_pawn") && team_of(s, p, assister) != side) {
            s.players[assister].assists++;
            s.players[assister].flash_assists += game_events::num(e, "assistedflash", 0) != 0;
            s.round[assister].assist = s.round[assister].assist || open;
        }
        s.deaths.push_back({victim, killer, p.tick});
        check_clutch(s);
    }

    void on_event(stats_t& s, parser_t& p, const game_event_t& e) {
        const std::string& name = e.desc->name;
        if (name == "player_hurt") {
            hurt(s, p, e);
        } else if (name == "weapon_fire") {
            int slot = slot_of(e, "userid");
            if (s.live && own(s, e, slot, "userid_pawn") && is_gun(game_events::str(e, "weapon")))
                s.pending.push_back({slot, false, false});
        } else if (name == "player_death") {
            if (s.live)
                death(s, p, e);
        } else if (name == "flashbang_detonate") {
            int slot = slot_of(e, "userid");
            if (s.live && s.in_round && slot >= 0)
                s.players[slot].flashes++;
        } else if (name == "player_blind") {
            int victim = slot_of(e, "userid"), attacker = slot_of(e, "attacker");
            double sec = game_events::flt(e, "blind_duration", 0.f);
            if (s.live && s.in_round && victim >= 0 && attacker >= 0 && victim != attacker && sec > 0) {
                int va = team_of(s, p, victim), aa = team_of(s, p, attacker);
                if ((va == 2 || va == 3) && (aa == 2 || aa == 3)) {
                    if (va != aa) {
                        s.players[attacker].enemy_blinds += sec >= FLASHED_SECONDS;
                        s.players[attacker].enemy_blind += sec;
                    } else {
                        s.players[attacker].team_blind += sec;
                    }
                }
            }
        } else if (name == "bomb_planted" || name == "bomb_defused" || name == "bomb_exploded") {
            if (s.live && s.in_round)
                s.info.bomb.push_back({p.tick, slot_of(e, "userid"), name == "bomb_planted" ? 0 : name == "bomb_defused" ? 1 : 2});
        } else if (name == "round_officially_ended") {
            if (s.live && s.in_round)
                s.winner = round_winner(s, p);
        } else if (name == "player_spawn") {
            int slot = slot_of(e, "userid");
            int pawn = pawn_of(e, "userid_pawn");
            if (slot >= 0)
                s.owner[pawn] = slot;
            s.hp[pawn] = 100;
        } else if (name == "round_freeze_end") {
            s.freeze_tick = p.tick;
            if (s.live)
                start_round(s, p);
        } else if (name == "round_announce_match_start") {
            for (player_stats_t& ps : s.players)
                ps = player_stats_t{};
            s.hits.clear();
            s.winners.clear();
            s.infos.clear();
            s.in_round = false;
            s.live = true;
            if (s.freeze_tick == p.tick)
                start_round(s, p);
        }
    }

    void on_frame(stats_t& s, parser_t& p) {
        if (!s.in_round) {
            s.pending.clear();
            return;
        }
        if (!s.info.reason && s.live) {
            const entity_t* r = rules(s, p);
            const field_ref_t* w = r ? access::ref(s.win, p.lay, p.ser, *r) : nullptr;
            const field_ref_t* why = r ? access::ref(s.reason, p.lay, p.ser, *r) : nullptr;
            if (w && why && access::get_int(*r, *w) != 0)
                s.info.reason = (int)access::get_int(*r, *why);
        }
        if (s.decided >= 0 && s.pending.empty())
            return;
        bool play = in_play(s, p);
        if (!play && s.decided < 0 && s.live)
            s.decided = p.tick;
        if (play)
            for (const aim_t& a : s.pending) {
                player_stats_t& ps = s.players[a.slot];
                if (!a.hit) {
                    ps.shots++;
                } else {
                    ps.hits++;
                    ps.head_hits += a.head;
                }
            }
        s.pending.clear();
    }

    void finish(stats_t& s, parser_t& p) {
        end_round(s, p);
        for (int i = 0; i < stats_t::SLOTS; i++) {
            const entity_t* c = entities::get(p.ents, i + 1);
            if (!c)
                continue;
            const field_ref_t* u = access::ref(s.utility, p.lay, p.ser, *c);
            const field_ref_t* f = access::ref(s.flashed, p.lay, p.ser, *c);
            if (u)
                s.players[i].utility_damage = (int)access::get_int(*c, *u);
            if (f)
                s.players[i].enemies_flashed = (int)access::get_int(*c, *f);
            const field_ref_t* pr = access::ref(s.per_round, p.lay, p.ser, *c);
            const field_ref_t* eq = access::ref(s.pr_equipment, p.lay, p.ser, *c);
            const field_ref_t* sv = access::ref(s.pr_saved, p.lay, p.ser, *c);
            const field_ref_t* er = access::ref(s.pr_earned, p.lay, p.ser, *c);
            const field_ref_t* lv = access::ref(s.pr_live, p.lay, p.ser, *c);
            const field_ref_t* kl = access::ref(s.pr_kills, p.lay, p.ser, *c);
            uint32_t n = pr ? access::count(*c, *pr) : 0;
            for (uint32_t k = 0; eq && sv && er && lv && kl && k < n && k < s.winners.size(); k++)
                s.players[i].eco.push_back({(int)access::get_int(*c, *eq, (int)k), (int)access::get_int(*c, *sv, (int)k), (int)access::get_int(*c, *er, (int)k),
                                            (int)access::get_int(*c, *lv, (int)k), (int)access::get_int(*c, *kl, (int)k)});
        }
    }
}
