#include "pch.h"

#include "demos/talk.h"

#include "dbg.h"
#include "demos/reader.h"
#include "demos/voice.h"
#include "parser/demo/demo.h"
#include "text.h"

namespace talk {
    const int GAP = 128;
    const uint32_t SAYTEXT2 = 118;
    const char* GLYPH = "\xe2\x98\x8a";

    struct line_t {
        int tick = 0;
        std::string msg;
    };

    struct place_t {
        size_t start = 0;
        size_t end = 0;
        uint64_t now = 0;
    };

    struct bitwriter_t {
        std::vector<uint8_t> buf;
        size_t bit = 0;

        void put(uint64_t v, int n) {
            for (int i = 0; i < n; i++) {
                size_t at = bit >> 3;
                if (buf.size() <= at)
                    buf.resize(at + 1, 0);
                if ((v >> i) & 1)
                    buf[at] = (uint8_t)(buf[at] | (1u << (bit & 7)));
                bit++;
            }
        }

        void ubitvar(uint32_t v) {
            if (v < 16) {
                put(v, 6);
            } else if (v < 256) {
                put((v & 15) | 0x10, 6);
                put(v >> 4, 4);
            } else if (v < 4096) {
                put((v & 15) | 0x20, 6);
                put(v >> 4, 8);
            } else {
                put((v & 15) | 0x30, 6);
                put(v >> 4, 28);
            }
        }

        void varint(uint64_t v) {
            while (v >= 0x80) {
                put((v & 0x7f) | 0x80, 8);
                v >>= 7;
            }
            put(v, 8);
        }

        void bytes(const std::string& s) {
            for (unsigned char c : s)
                put(c, 8);
        }
    };

    void put_varint(std::string& out, uint64_t v) {
        while (v >= 0x80) {
            out.push_back((char)((v & 0x7f) | 0x80));
            v >>= 7;
        }
        out.push_back((char)v);
    }

    void put_num(std::string& out, int num, uint64_t v) {
        put_varint(out, (uint64_t)num << 3);
        put_varint(out, v);
    }

    void put_str(std::string& out, int num, const std::string& s) {
        put_varint(out, ((uint64_t)num << 3) | 2);
        put_varint(out, s.size());
        out += s;
    }

    std::string saytext2(int ent, const char* fmt, const std::string& name, const std::string& text) {
        std::string out;
        put_num(out, 1, (uint64_t)ent);
        put_num(out, 2, 1);
        put_str(out, 3, fmt);
        put_str(out, 4, name);
        put_str(out, 5, text);
        put_str(out, 6, "");
        put_str(out, 7, "");
        put_num(out, 8, 1);
        return out;
    }

    int talker_of(const std::vector<talker_t>& who, uint64_t xuid) {
        for (size_t i = 0; i < who.size(); i++)
            if (who[i].steamid == xuid)
                return (int)i;
        return -1;
    }

    int prestart_of(const std::vector<voice::round_mark_t>& marks, int live) {
        for (const voice::round_mark_t& m : marks)
            if (m.live == live)
                return m.prestart;
        return live;
    }

    int side_at(const std::vector<std::pair<int, int>>& sides, int tick) {
        int side = sides.front().second;
        for (const std::pair<int, int>& s : sides) {
            if (s.first > tick)
                break;
            side = s.second;
        }
        return side;
    }

    std::vector<line_t> lines_of(const std::vector<voice::spurt_t>& spurts, const std::vector<talker_t>& who,
                                 const std::vector<voice::round_mark_t>& marks, const opts_t& opts) {
        std::vector<std::vector<std::pair<int, int>>> sides(who.size());
        for (size_t i = 0; i < who.size(); i++)
            for (const std::pair<int, int>& s : who[i].sides)
                sides[i].push_back({prestart_of(marks, s.first), s.second});

        std::vector<line_t> out;
        std::vector<int> last(who.size(), 0);
        std::vector<bool> said(who.size(), false);
        const std::string text = opts.style ? "talking" : GLYPH;
        int step = std::max(opts.refresh, 32);
        for (const voice::spurt_t& s : spurts) {
            if (s.end - s.start < opts.min)
                continue;
            int i = talker_of(who, s.xuid);
            if (i < 0 || sides[i].empty())
                continue;
            const talker_t& t = who[i];
            for (int tick = s.start; tick < s.end; tick += step) {
                if (said[i] && tick - last[i] < step - 32)
                    continue;
                said[i] = true;
                last[i] = tick;
                const char* fmt = side_at(sides[i], tick) == 2 ? "Cstrike_Chat_T" : "Cstrike_Chat_CT";
                out.push_back({tick - opts.lead, saytext2(t.slot + 1, fmt, t.name, text)});
            }
        }
        std::stable_sort(out.begin(), out.end(), [](const line_t& a, const line_t& b) { return a.tick < b.tick; });
        return out;
    }

    std::string chat_packet(int tick, const std::vector<line_t>& lines, size_t from, size_t to) {
        bitwriter_t w;
        for (size_t i = from; i < to; i++) {
            w.ubitvar(SAYTEXT2);
            w.varint(lines[i].msg.size());
            w.bytes(lines[i].msg);
        }
        std::string packet;
        put_varint(packet, (3u << 3) | 2);
        put_varint(packet, w.buf.size());
        packet.append((const char*)w.buf.data(), w.buf.size());

        std::string frame;
        put_varint(frame, (uint64_t)dem_packet);
        put_varint(frame, (uint64_t)(uint32_t)tick);
        put_varint(frame, packet.size());
        frame += packet;
        return frame;
    }

    void patch_offsets(FILE* f, const std::vector<place_t>& places, const uint8_t* head) {
        for (int slot = 0; slot < 2; slot++) {
            uint32_t off = 0;
            memcpy(&off, head + 8 + slot * 4, 4);
            if (!off)
                continue;
            uint64_t now = 0;
            bool found = false;
            for (const place_t& pl : places)
                if (off >= pl.start && off < pl.end) {
                    now = pl.now + (off - pl.start);
                    found = true;
                    break;
                }
            if (!found && !places.empty() && off >= places.back().end)
                now = places.back().now + (off - places.back().start);
            if (!found && !now)
                continue;
            uint32_t v = (uint32_t)now;
            fseek(f, 8 + slot * 4, SEEK_SET);
            fwrite(&v, 4, 1, f);
        }
    }

    std::vector<talker_t> talkers(const roster_t& r) {
        std::vector<talker_t> out;
        for (const roster_player_t& p : r.players) {
            talker_t t;
            t.steamid = p.steamid;
            t.slot = p.slot;
            t.name = p.name;
            const std::vector<round_line_t>& lines = p.stats.lines;
            for (size_t k = 0; k < lines.size() && k < r.rounds_info.size(); k++)
                if (lines[k].in && (lines[k].side == 2 || lines[k].side == 3))
                    t.sides.push_back({r.rounds_info[k].start, lines[k].side});
            if (t.sides.empty() && (p.team == 0 || p.team == 1))
                t.sides.push_back({0, p.team + 2});
            out.push_back(t);
        }
        return out;
    }

    bool annotate(const std::string& demo_path, const std::string& out_path, const opts_t& opts, std::string& err,
                  const std::atomic<bool>* cancel) {
        roster_t r;
        if (!reader::read(demo_path, r, nullptr, nullptr, cancel)) {
            err = r.err.empty() ? "cant read the demo" : r.err;
            return false;
        }
        return annotate(demo_path, out_path, talkers(r), opts, err, cancel);
    }

    bool annotate(const std::string& demo_path, const std::string& out_path, const std::vector<talker_t>& who, const opts_t& opts,
                  std::string& err, const std::atomic<bool>* cancel) {
        std::vector<voice::spurt_t> spurts;
        std::vector<voice::round_mark_t> marks;
        if (!voice::spurts(demo_path, GAP, spurts, marks, err, cancel)) {
            if (err.empty())
                err = "no voice in this demo";
            return false;
        }
        std::vector<line_t> lines = lines_of(spurts, who, marks, opts);
        dbg::print("[talk] %zu spurts, %zu lines", spurts.size(), lines.size());
        if (lines.empty()) {
            err = "no speech in this demo";
            return false;
        }

        demo_t d;
        if (!demo::open(d, demo_path.c_str())) {
            err = d.err;
            return false;
        }
        FILE* out = _wfopen(text::wide(out_path).c_str(), L"wb");
        if (!out) {
            demo::close(d);
            err = "cant write " + out_path;
            return false;
        }
        setvbuf(out, nullptr, _IOFBF, 1 << 20);

        bool ok = fwrite(d.data, 1, 16, out) == 16;
        std::vector<place_t> places;
        uint64_t written = 16;
        size_t next = 0;
        frame_t f;
        while (ok) {
            size_t start = d.pos;
            if (!demo::next(d, f))
                break;
            size_t end = d.pos;

            size_t upto = next;
            while (upto < lines.size() && lines[upto].tick <= f.tick)
                upto++;
            if (upto > next && f.cmd == dem_packet) {
                std::string chat = chat_packet(f.tick, lines, next, upto);
                ok = fwrite(chat.data(), 1, chat.size(), out) == chat.size();
                written += chat.size();
                next = upto;
            }
            if (ok) {
                ok = fwrite(d.data + start, 1, end - start, out) == end - start;
                places.push_back({start, end, written});
                written += end - start;
            }
        }
        if (ok)
            patch_offsets(out, places, d.data);
        fclose(out);
        demo::close(d);
        if (!ok)
            err = "cant write " + out_path;
        return ok;
    }
}