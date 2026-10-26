#include "pch.h"

#include "dbg.h"
#include "demos/basefix.h"
#include "demos/reader.h"
#include "demos/stats.h"
#include "parser/entities/access.h"
#include "parser/parser.h"
#include "parser/proto/proto.h"

namespace reader {
    const int SLOTS = 64;
    const int SAMPLE_TICKS = 32;
    const int STABLE_TICKS = 640;

    struct slot_t {
        uint64_t steamid = 0;
        std::string name;
        std::string crosshair;
        int mute = 0;
        int first = -1;
        int last = -1;
    };

    struct state_t {
        field_key_t name = access::key("m_iszPlayerName");
        field_key_t crosshair = access::key("m_szCrosshairCodes");
        field_key_t mute_flags[2] = {access::key("m_uiCommunicationMuteFlags"), access::key("m_uiCommunicationMuteFlagX")};
        field_key_t mute_abuse[2] = {access::key("m_bHasCommunicationAbuseMute"), access::key("m_bHasCommunicationAbuseMutX")};
        field_key_t steam = access::key("m_steamID");
        field_key_t team = access::key("m_iTeamNum");
        field_key_t clan = access::key("m_szClanTeamname");
        field_key_t score = access::key("m_iScore");
        field_key_t warmup = access::key("m_pGameRules.m_bWarmupPeriod");
        int controller_cls = -1;
        int team_cls = -1;
        int rules_cls = -1;
        int rules = -1;
        std::vector<int> team_ents;
        int sampled = -SAMPLE_TICKS;
        bool live = false;
        slot_t slots[SLOTS];
        std::string clans[4];
        int scores[4] = {};
        int voice = 0;
        std::map<uint64_t, int> speakers;
        early_fn_t early = nullptr;
        void* early_user = nullptr;
        const std::atomic<bool>* cancel = nullptr;
        bool early_sent = false;
        bool quick = false;
        bool tail = false;
        bool scanning = false;
        size_t scan_from = 0;
        size_t scan_to = 0;
        roster_t peeked;
        int stable_since = -1;
        int seen = -1;
        stats_t stats;
        skins_t skins;
        late_t late;
    };

    void build(const parser_t& p, const state_t& s, roster_t& out);

    int class_of(const parser_t& p, const char* name) {
        for (size_t i = 0; i < p.ser.classes.size(); i++)
            if (p.ser.classes[i] == name)
                return (int)i;
        return -1;
    }

    void find_classes(parser_t& p, state_t& s) {
        if (s.controller_cls >= 0)
            return;
        s.controller_cls = class_of(p, "CCSPlayerController");
        s.team_cls = class_of(p, "CCSTeam");
        s.rules_cls = class_of(p, "CCSGameRulesProxy");
    }

    void find_entities(parser_t& p, state_t& s) {
        if (s.rules >= 0 && s.team_ents.size() >= 4)
            return;
        s.team_ents.clear();
        for (const entity_t& e : p.ents.list) {
            if (!e.active)
                continue;
            if (e.cls == s.rules_cls)
                s.rules = e.index;
            else if (e.cls == s.team_cls)
                s.team_ents.push_back(e.index);
        }
    }

    bool is_live(parser_t& p, state_t& s) {
        const entity_t* r = entities::get(p.ents, s.rules);
        if (!r)
            return false;
        const field_ref_t* f = access::ref(s.warmup, p.lay, p.ser, *r);
        return f && !access::get_bool(*r, *f);
    }

    void sample_teams(parser_t& p, state_t& s) {
        for (int idx : s.team_ents) {
            const entity_t* e = entities::get(p.ents, idx);
            if (!e)
                continue;
            const field_ref_t* t = access::ref(s.team, p.lay, p.ser, *e);
            int side = t ? (int)access::get_int(*e, *t) : -1;
            if (side < 2 || side > 3)
                continue;
            const field_ref_t* sc = access::ref(s.score, p.lay, p.ser, *e);
            if (sc)
                s.scores[side] = (int)access::get_int(*e, *sc);
            const field_ref_t* c = access::ref(s.clan, p.lay, p.ser, *e);
            const std::string* name = c ? access::get_str(*e, *c) : nullptr;
            if (name && s.clans[side].empty())
                s.clans[side] = *name;
        }
    }

    void sample_players(parser_t& p, state_t& s) {
        for (int idx = 1; idx <= SLOTS; idx++) {
            const entity_t* c = entities::get(p.ents, idx);
            if (!c || c->cls != s.controller_cls)
                continue;
            slot_t& sl = s.slots[idx - 1];
            const field_ref_t* st = access::ref(s.steam, p.lay, p.ser, *c);
            uint64_t id = st ? access::get_u64(*c, *st) : 0;
            if (!id || (s.tail && sl.steamid != id))
                continue;
            if (sl.steamid != id) {
                sl = slot_t{};
                sl.steamid = id;
            }
            const field_ref_t* n = access::ref(s.name, p.lay, p.ser, *c);
            const std::string* name = n ? access::get_str(*c, *n) : nullptr;
            if (name)
                sl.name = *name;
            const field_ref_t* cr = access::ref(s.crosshair, p.lay, p.ser, *c);
            const std::string* code = cr ? access::get_str(*c, *cr) : nullptr;
            if (code && !code->empty())
                sl.crosshair = *code;
            for (int k = 0; k < 2; k++) {
                if (const field_ref_t* m = access::ref(s.mute_flags[k], p.lay, p.ser, *c); m && access::get_int(*c, *m))
                    sl.mute |= 1;
                if (const field_ref_t* m = access::ref(s.mute_abuse[k], p.lay, p.ser, *c); m && access::get_bool(*c, *m))
                    sl.mute |= 2;
            }
            const field_ref_t* t = access::ref(s.team, p.lay, p.ser, *c);
            int side = t ? (int)access::get_int(*c, *t) : -1;
            if (side < 2 || side > 3)
                continue;
            if (sl.first < 0 && s.live)
                sl.first = side;
            sl.last = side;
        }
    }

    void on_frame(parser_t& p, void* user) {
        state_t& s = *(state_t*)user;
        if (s.cancel && *s.cancel)
            p.stop = true;
        if (!s.quick) {
            stats::on_frame(s.stats, p);
            basefix::watch_frame(s.late, p);
        }
        if (!p.ready || p.tick - s.sampled < SAMPLE_TICKS)
            return;
        s.sampled = p.tick;
        find_classes(p, s);
        find_entities(p, s);
        if (!s.live)
            s.live = is_live(p, s);
        sample_players(p, s);
        skins::on_frame(s.skins, p);
        if (s.live)
            sample_teams(p, s);
        if ((s.early || s.quick) && !s.early_sent && s.live) {
            int n = 0;
            for (const slot_t& sl : s.slots)
                n += sl.steamid && sl.first >= 0;
            if (n != s.seen) {
                s.seen = n;
                s.stable_since = p.tick;
            } else if (n >= 2 && p.tick - s.stable_since >= STABLE_TICKS) {
                roster_t r;
                build(p, s, r);
                r.partial = true;
                s.early_sent = true;
                if (s.early)
                    s.early(r, s.early_user);
                if (s.quick) {
                    s.peeked = r;
                    s.scan_from = p.demo.pos;
                    s.tail = parser::seek_last_full(p);
                    s.scan_to = p.demo.pos;
                    if (s.tail) {
                        s.rules = -1;
                        s.team_ents.clear();
                    } else {
                        p.stop = true;
                    }
                }
            }
        }
    }

    void on_msg(parser_t& p, const msg_t& m, void* user) {
        state_t& s = *(state_t*)user;
        if (!s.quick && !s.scanning)
            basefix::watch_msg(s.late, p, m);
        if (m.type != svc_voice_data)
            return;
        s.voice++;
        if (s.scanning)
            p.stop = true;
        proto_t r = proto::make(m.data, m.len);
        proto_field_t f;
        while (proto::next(r, f))
            if (f.num == 4 && f.wire == 1)
                s.speakers[f.val]++;
    }

    void on_scan(parser_t& p, void* user) {
        state_t& s = *(state_t*)user;
        if ((s.cancel && *s.cancel) || p.demo.pos >= s.scan_to)
            p.stop = true;
    }

    void on_event(parser_t& p, const game_event_t& e, void* user) {
        state_t& s = *(state_t*)user;
        if (e.desc->kind == ek_round_announce_match_start) {
            for (slot_t& sl : s.slots)
                sl.first = -1;
            for (std::string& c : s.clans)
                c.clear();
        }
        stats::on_event(s.stats, p, e);
    }

    void build(const parser_t& p, const state_t& s, roster_t& out) {
        out.map = p.demo.info.map;
        out.winners = s.stats.winners;
        out.rounds_info = s.stats.infos;
        out.tick_interval = p.tick_interval;
        out.server = p.demo.info.server;
        out.voice = s.voice;
        out.duration = (int)(p.tick * p.tick_interval);
        for (int side = 2; side <= 3; side++) {
            roster_team_t& t = out.teams[side - 2];
            t.side = side;
            t.name = s.clans[side];
        }
        bool swapped = false;
        for (int i = 0; i < SLOTS; i++) {
            const slot_t& sl = s.slots[i];
            if (!sl.steamid || (sl.first < 0 && sl.last < 0))
                continue;
            roster_player_t pl;
            pl.slot = i;
            pl.name = sl.name;
            pl.crosshair = sl.crosshair;
            pl.mute = sl.mute;
            pl.steamid = sl.steamid;
            pl.team = (sl.first >= 0 ? sl.first : sl.last) - 2;
            auto sp = s.speakers.find(sl.steamid);
            pl.voice = sp == s.speakers.end() ? 0 : sp->second;
            pl.stats = s.stats.players[i];
            skins::of(s.skins, sl.steamid, pl.skins, pl.music);
            if (sl.first >= 0 && sl.last >= 0 && sl.first != sl.last)
                swapped = true;
            out.players.push_back(pl);
        }
        for (int side = 2; side <= 3; side++)
            out.teams[side - 2].score = s.scores[swapped ? 5 - side : side];
        out.rounds = out.teams[0].score + out.teams[1].score;
    }

    bool run(const std::string& path, roster_t& out, early_fn_t early, void* user, const std::atomic<bool>* cancel, bool quick) {
        out = roster_t{};
        parser_t* p = new parser_t();
        if (!parser::open(*p, path.c_str())) {
            out.err = p->err;
            demo::close(p->demo);
            delete p;
            return false;
        }
        state_t* s = new state_t();
        skins::init(s->skins);
        s->early = early;
        s->early_user = user;
        s->cancel = cancel;
        s->quick = quick;
        callbacks_t cb;
        cb.user = s;
        cb.on_frame = on_frame;
        cb.on_msg = on_msg;
        if (!quick)
            cb.on_event = on_event;
        bool ok = parser::run(*p, cb);
        if (quick && s->early_sent && !s->tail) {
            out = s->peeked;
        } else if (quick && s->tail) {
            build(*p, *s, out);
            out.partial = true;
            out.ended = true;
            if (ok && !s->voice && s->scan_to > s->scan_from) {
                callbacks_t scan;
                scan.user = s;
                scan.on_frame = on_scan;
                scan.on_msg = on_msg;
                s->scanning = true;
                p->stop = false;
                p->use_full = false;
                p->skip_entities = true;
                p->demo.pos = s->scan_from;
                parser::resume(*p, scan);
                out.voice = s->voice;
            }
            out.voice_known = s->voice > 0 || !(cancel && *cancel);
        } else {
            stats::finish(s->stats, *p);
            build(*p, *s, out);
            out.voice_known = !quick;
            out.pet_crash = basefix::crashes(s->late);
        }
        if (!ok) {
            out.err = p->err;
            dbg::print("[reader] %s: %s", path.c_str(), p->err.c_str());
        }
        delete s;
        demo::close(p->demo);
        delete p;
        return ok || !out.players.empty();
    }

    bool read(const std::string& path, roster_t& out, early_fn_t early, void* user, const std::atomic<bool>* cancel) {
        return run(path, out, early, user, cancel, false);
    }

    bool peek(const std::string& path, roster_t& out, const std::atomic<bool>* cancel) {
        return run(path, out, nullptr, nullptr, cancel, true);
    }
}
