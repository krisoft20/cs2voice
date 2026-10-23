#include "pch.h"

#include "demos/voice.h"
#include "parser/parser.h"
#include "parser/proto/proto.h"

namespace voice {
    const uint32_t MAGIC = 0x56325343;
    const uint32_t VERSION = 1;
    const int OPUS = 2;

    struct packet_t {
        int speaker = 0;
        int tick = 0;
        size_t len = 0;
    };

    struct round_t {
        int start = 0;
        int live = -1;
        int end = -1;
    };

    struct kill_t {
        int tick = 0;
        int killer = -1;
        int victim = -1;
        bool head = false;
    };

    struct state_t {
        std::vector<uint64_t> speakers;
        std::vector<packet_t> packets;
        std::vector<uint8_t> bytes;
        std::vector<size_t> offsets;
        std::vector<round_t> rounds;
        std::vector<kill_t> kills;
        bool live = false;
        const std::atomic<bool>* cancel = nullptr;
    };

    int speaker_of(state_t& s, uint64_t xuid) {
        for (size_t i = 0; i < s.speakers.size(); i++)
            if (s.speakers[i] == xuid)
                return (int)i;
        s.speakers.push_back(xuid);
        return (int)s.speakers.size() - 1;
    }

    void on_msg(parser_t& p, const msg_t& m, void* user) {
        state_t& s = *(state_t*)user;
        if (s.cancel && *s.cancel)
            p.stop = true;
        if (m.type != svc_voice_data)
            return;
        proto_t r = proto::make(m.data, m.len);
        proto_field_t f;
        uint64_t xuid = 0;
        int format = -1;
        const uint8_t* data = nullptr;
        size_t len = 0;
        while (proto::next(r, f)) {
            if (f.num == 4 && f.wire == 1)
                xuid = f.val;
            if (f.num != 1 || f.wire != 2)
                continue;
            proto_t a = proto::make(f.data, f.len);
            proto_field_t g;
            while (proto::next(a, g)) {
                if (g.num == 1 && g.wire == 0)
                    format = (int)g.val;
                if (g.num == 2 && g.wire == 2) {
                    data = g.data;
                    len = g.len;
                }
            }
        }
        if (!xuid || format != OPUS || !data || !len || len > 0xffff)
            return;
        s.offsets.push_back(s.bytes.size());
        s.bytes.insert(s.bytes.end(), data, data + len);
        s.packets.push_back({speaker_of(s, xuid), p.tick, len});
    }

    void on_event(parser_t& p, const game_event_t& e, void* user) {
        state_t& s = *(state_t*)user;
        const std::string& name = e.desc->name;
        if (name == "round_prestart") {
            if (!s.rounds.empty() && s.rounds.back().end < 0)
                s.rounds.back().end = p.tick;
            s.rounds.push_back({p.tick});
        } else if (name == "round_freeze_end") {
            if (!s.rounds.empty() && s.rounds.back().live < 0)
                s.rounds.back().live = p.tick;
        } else if (name == "round_officially_ended") {
            if (!s.rounds.empty() && s.rounds.back().end < 0)
                s.rounds.back().end = p.tick;
        } else if (name == "round_announce_match_start") {
            s.live = true;
            s.kills.clear();
            std::vector<round_t> open;
            for (const round_t& r : s.rounds)
                if (r.end < 0)
                    open.push_back(r);
            s.rounds = open;
        } else if (name == "player_death" && s.live) {
            s.kills.push_back({p.tick, (int)game_events::num(e, "attacker", -1), (int)game_events::num(e, "userid", -1),
                               game_events::num(e, "headshot", 0) != 0});
        }
    }

    void put32(std::string& out, uint32_t v) {
        out.append((const char*)&v, 4);
    }

    bool extract(const std::string& demo, std::string& blob, std::string& err, const std::atomic<bool>* cancel) {
        parser_t* p = new parser_t();
        if (!parser::open(*p, demo.c_str())) {
            err = p->err;
            demo::close(p->demo);
            delete p;
            return false;
        }
        p->skip_entities = true;
        state_t* s = new state_t();
        s->cancel = cancel;
        callbacks_t cb;
        cb.user = s;
        cb.on_msg = on_msg;
        cb.on_event = on_event;
        bool ok = parser::run(*p, cb) || !s->packets.empty();
        if (cancel && *cancel)
            ok = false;
        if (!s->rounds.empty() && s->rounds.back().end < 0)
            s->rounds.back().end = p->tick;

        std::string head = "{\"tick_interval\":" + std::to_string(p->tick_interval) + ",\"ticks\":" + std::to_string(p->tick) + ",\"speakers\":[";
        for (size_t i = 0; i < s->speakers.size(); i++)
            head += (i ? ",\"" : "\"") + std::to_string(s->speakers[i]) + "\"";
        head += "],\"rounds\":[";
        for (size_t i = 0; i < s->rounds.size(); i++) {
            const round_t& r = s->rounds[i];
            head += std::string(i ? "," : "") + "[" + std::to_string(r.start) + "," + std::to_string(r.live) + "," + std::to_string(r.end) + "]";
        }
        head += "],\"kills\":[";
        for (size_t i = 0; i < s->kills.size(); i++) {
            const kill_t& k = s->kills[i];
            head += std::string(i ? "," : "") + "[" + std::to_string(k.tick) + "," + std::to_string(k.killer) + "," + std::to_string(k.victim) +
                    "," + (k.head ? "1" : "0") + "]";
        }
        head += "]}";

        blob.clear();
        put32(blob, MAGIC);
        put32(blob, VERSION);
        put32(blob, (uint32_t)head.size());
        blob += head;
        put32(blob, (uint32_t)s->packets.size());
        for (size_t i = 0; i < s->packets.size(); i++) {
            const packet_t& k = s->packets[i];
            uint8_t speaker = (uint8_t)k.speaker;
            uint16_t len = (uint16_t)k.len;
            blob.append((const char*)&speaker, 1);
            blob.append((const char*)&k.tick, 4);
            blob.append((const char*)&len, 2);
            blob.append((const char*)s->bytes.data() + s->offsets[i], k.len);
        }
        if (!ok && err.empty())
            err = p->err.empty() ? "cancelled" : p->err;
        delete s;
        demo::close(p->demo);
        delete p;
        return ok;
    }
}
