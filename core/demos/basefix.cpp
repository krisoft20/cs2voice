#include "pch.h"

#include "dbg.h"
#include "demos/basefix.h"
#include "parser/demo/snappy.h"
#include "parser/proto/proto.h"
#include "text.h"

namespace basefix {
    struct writer_t {
        std::vector<uint8_t> buf;
        size_t bit = 0;
    };

    struct entry_t {
        int index = -1;
        bool has_key = false;
        std::string key;
        bool has_data = false;
        std::vector<uint8_t> data;
    };

    struct table_t {
        bool fixed = false;
        int32_t size_bits = 0;
        int32_t flags = 0;
        bool varint_bits = false;
    };

    struct pass_t {
        late_t watch;
        std::vector<entry_t> add;
    };

    void put(writer_t& w, uint32_t v, int n) {
        for (int i = 0; i < n; i++, w.bit++) {
            if ((w.bit >> 3) >= w.buf.size())
                w.buf.push_back(0);
            if ((v >> i) & 1)
                w.buf[w.bit >> 3] |= (uint8_t)(1u << (w.bit & 7));
        }
    }

    void put_ubitvar(writer_t& w, uint32_t v) {
        if (v < 16) {
            put(w, v, 6);
        } else if (v < 256) {
            put(w, (v & 15) | 0x10, 6);
            put(w, v >> 4, 4);
        } else if (v < 4096) {
            put(w, (v & 15) | 0x20, 6);
            put(w, v >> 4, 8);
        } else {
            put(w, (v & 15) | 0x30, 6);
            put(w, v >> 4, 28);
        }
    }

    void put_varint_bits(writer_t& w, uint32_t v) {
        do {
            uint32_t byte = v & 0x7f;
            v >>= 7;
            put(w, byte | (v ? 0x80 : 0), 8);
        } while (v);
    }

    void varint(std::vector<uint8_t>& out, uint64_t v) {
        do {
            uint8_t byte = v & 0x7f;
            v >>= 7;
            out.push_back(byte | (v ? 0x80 : 0));
        } while (v);
    }

    void field_bytes(std::vector<uint8_t>& out, uint32_t num, const uint8_t* data, size_t len) {
        varint(out, ((uint64_t)num << 3) | 2);
        varint(out, len);
        out.insert(out.end(), data, data + len);
    }

    void field_varint(std::vector<uint8_t>& out, uint32_t num, uint64_t v) {
        varint(out, (uint64_t)num << 3);
        varint(out, v);
    }

    const char* MUTES[][2] = {
        {"m_uiCommunicationMuteFlags", "m_uiCommunicationMuteFlagX"},
        {"m_bHasCommunicationAbuseMute", "m_bHasCommunicationAbuseMutX"},
    };

    bool rename_mutes(const uint8_t* data, size_t n, std::vector<uint8_t>& out) {
        out.assign(data, data + n);
        int hits = 0;
        for (auto& m : MUTES) {
            size_t len = strlen(m[0]);
            for (size_t i = 0; i + len <= out.size(); i++)
                if (memcmp(out.data() + i, m[0], len) == 0) {
                    memcpy(out.data() + i, m[1], len);
                    hits++;
                }
        }
        return hits > 0;
    }

    const string_table_t* baselines(const parser_t& p) {
        int t = string_tables::find(p.tables, "instancebaseline");
        return t < 0 ? nullptr : &p.tables.list[t];
    }

    void watch_msg(late_t& l, parser_t& p, const msg_t& m) {
        if (m.type == svc_packet_entities && l.first_tick < 0 && p.tick >= 0)
            l.first_tick = p.tick;
        if (m.type != svc_create_string_table && m.type != svc_update_string_table)
            return;
        const string_table_t* t = baselines(p);
        if (!t)
            return;
        for (const table_entry_t& e : t->entries) {
            if (e.key.empty())
                continue;
            if (p.tick < 1)
                l.signon.insert(e.key);
            else if ((l.first_tick < 0 || p.tick <= l.first_tick) && !l.signon.count(e.key))
                l.late.insert(e.key);
        }
    }

    void watch_frame(late_t& l, parser_t& p) {
        if (l.first_done || !p.ready || l.first_tick < 0)
            return;
        l.first_done = true;
        for (size_t i = 0; i < p.ents.list.size(); i++)
            if (const entity_t* e = entities::get(p.ents, (int)i))
                l.first.insert(e->cls);
    }

    bool crashes(const late_t& l) {
        for (int cls : l.first)
            if (l.late.count(std::to_string(cls)))
                return true;
        return false;
    }

    bool decode_entries(const table_t& t, const uint8_t* blob, size_t len, int32_t count, std::vector<entry_t>& out) {
        bits_t b = bits::make(blob, len);
        std::string history[32];
        size_t nhist = 0;
        int index = -1;
        std::string tmp;
        std::vector<uint8_t> raw, unpacked;
        for (int32_t n = 0; n < count; n++) {
            entry_t e;
            if (bits::read_bit(b))
                index++;
            else
                index += (int)bits::read_varint(b) + 2;
            e.index = index;
            e.has_key = bits::read_bit(b);
            if (e.has_key) {
                if (bits::read_bit(b)) {
                    uint32_t pos = bits::read(b, 5), size = bits::read(b, 5);
                    if (pos < nhist)
                        e.key = history[pos].substr(0, std::min<size_t>(size, history[pos].size()));
                    bits::read_cstring(b, tmp, 4096);
                    e.key += tmp;
                } else {
                    bits::read_cstring(b, e.key, 4096);
                }
                if (nhist == 32) {
                    for (int i = 1; i < 32; i++)
                        history[i - 1].swap(history[i]);
                    nhist--;
                }
                history[nhist++] = e.key;
            }
            e.has_data = bits::read_bit(b);
            if (e.has_data) {
                bool compressed = false;
                uint32_t nbits;
                if (t.fixed) {
                    nbits = (uint32_t)t.size_bits;
                } else {
                    if (t.flags & 1)
                        compressed = bits::read_bit(b);
                    nbits = t.varint_bits ? bits::read_ubitvar(b) * 8 : bits::read(b, 17) * 8;
                }
                raw.assign((nbits + 7) / 8, 0);
                bits::read_bytes(b, raw.data(), nbits / 8);
                if (nbits & 7)
                    raw[nbits / 8] = (uint8_t)bits::read(b, nbits & 7);
                if (compressed) {
                    size_t out_len = 0;
                    if (!snappy::decompress(raw.data(), raw.size(), unpacked, out_len))
                        return false;
                    e.data.assign(unpacked.begin(), unpacked.begin() + out_len);
                } else {
                    e.data = raw;
                }
            }
            if (b.bad)
                return false;
            out.push_back(std::move(e));
        }
        return true;
    }

    std::vector<uint8_t> encode_entries(const table_t& t, const std::vector<entry_t>& list) {
        writer_t w;
        int index = -1;
        for (const entry_t& e : list) {
            if (e.index == index + 1) {
                put(w, 1, 1);
            } else {
                put(w, 0, 1);
                put_varint_bits(w, (uint32_t)(e.index - index - 2));
            }
            index = e.index;
            put(w, e.has_key, 1);
            if (e.has_key) {
                put(w, 0, 1);
                for (char ch : e.key)
                    put(w, (uint8_t)ch, 8);
                put(w, 0, 8);
            }
            put(w, e.has_data, 1);
            if (e.has_data) {
                if (t.flags & 1)
                    put(w, 0, 1);
                if (t.varint_bits)
                    put_ubitvar(w, (uint32_t)e.data.size());
                else
                    put(w, (uint32_t)e.data.size(), 17);
                for (uint8_t byte : e.data)
                    put(w, byte, 8);
            }
        }
        return w.buf;
    }

    void collect(parser_t& p, const msg_t& m, void* user) {
        pass_t& s = *(pass_t*)user;
        watch_msg(s.watch, p, m);
        if (m.type != svc_create_string_table && m.type != svc_update_string_table)
            return;
        if (p.tick < 1 || (s.watch.first_tick >= 0 && p.tick > s.watch.first_tick))
            return;
        const string_table_t* t = baselines(p);
        if (!t)
            return;
        for (size_t i = 0; i < t->entries.size(); i++) {
            const table_entry_t& e = t->entries[i];
            if (e.key.empty() || s.watch.signon.count(e.key))
                continue;
            bool known = false;
            for (entry_t& a : s.add)
                if (a.index == (int)i) {
                    a.data = e.data;
                    known = true;
                }
            if (!known)
                s.add.push_back({(int)i, true, e.key, true, e.data});
        }
    }

    void frame_seen(parser_t& p, void* user) {
        watch_frame(((pass_t*)user)->watch, p);
    }

    bool add_to_create(const std::vector<entry_t>& add, const uint8_t* msg, size_t n, std::vector<uint8_t>& out, bool& ok) {
        table_t t;
        std::string name;
        const uint8_t* data = nullptr;
        size_t len = 0;
        int32_t count = 0;
        bool compressed = false;
        proto_t r = proto::make(msg, n);
        proto_field_t f;
        while (proto::next(r, f)) {
            switch (f.num) {
            case 1: name = proto::str(f); break;
            case 2: count = proto::i32(f); break;
            case 3: t.fixed = f.val != 0; break;
            case 5: t.size_bits = proto::i32(f); break;
            case 6: t.flags = proto::i32(f); break;
            case 7: data = f.data; len = f.len; break;
            case 9: compressed = f.val != 0; break;
            case 10: t.varint_bits = f.val != 0; break;
            }
        }
        if (name != "instancebaseline" || !data)
            return false;
        std::vector<uint8_t> plain;
        size_t plain_len = len;
        const uint8_t* src = data;
        if (compressed) {
            if (!snappy::decompress(data, len, plain, plain_len)) {
                ok = false;
                return false;
            }
            plain.resize(plain_len + 16);
            src = plain.data();
        }
        std::vector<entry_t> list;
        if (!decode_entries(t, src, plain_len, count, list)) {
            ok = false;
            return false;
        }
        int last = list.empty() ? -1 : list.back().index;
        for (const entry_t& e : add) {
            if (e.index != last + 1) {
                ok = false;
                return false;
            }
            list.push_back(e);
            last = e.index;
        }
        std::vector<uint8_t> blob = encode_entries(t, list);
        out.clear();
        r = proto::make(msg, n);
        const uint8_t* at = r.p;
        while (proto::next(r, f)) {
            if (f.num == 2)
                field_varint(out, 2, list.size());
            else if (f.num == 7)
                field_bytes(out, 7, blob.data(), blob.size());
            else if (f.num == 8)
                field_varint(out, 8, blob.size());
            else if (f.num == 9)
                field_varint(out, 9, 0);
            else
                out.insert(out.end(), at, r.p);
            at = r.p;
        }
        return true;
    }

    bool add_to_snapshot(const std::vector<entry_t>& add, const uint8_t* msg, size_t n, std::vector<uint8_t>& out, bool& ok) {
        bool changed = false;
        out.clear();
        proto_t r = proto::make(msg, n);
        proto_field_t f;
        const uint8_t* at = r.p;
        while (proto::next(r, f)) {
            std::string name;
            int items = 0;
            if (f.num == 1 && f.wire == 2) {
                proto_t tr = proto::make(f.data, f.len);
                proto_field_t tf;
                while (proto::next(tr, tf)) {
                    if (tf.num == 1 && tf.wire == 2)
                        name = proto::str(tf);
                    else if (tf.num == 2 && tf.wire == 2)
                        items++;
                }
            }
            if (name != "instancebaseline" || add.empty() || items > add.front().index) {
                out.insert(out.end(), at, r.p);
                at = r.p;
                continue;
            }
            if (items != add.front().index) {
                ok = false;
                return false;
            }
            std::vector<uint8_t> table(f.data, f.data + f.len);
            for (const entry_t& e : add) {
                std::vector<uint8_t> item;
                field_bytes(item, 1, (const uint8_t*)e.key.data(), e.key.size());
                field_bytes(item, 2, e.data.data(), e.data.size());
                field_bytes(table, 2, item.data(), item.size());
            }
            field_bytes(out, 1, table.data(), table.size());
            at = r.p;
            changed = true;
        }
        return changed;
    }

    bool add_to_packet(const std::vector<entry_t>& add, const uint8_t* pkt, size_t n, std::vector<uint8_t>& out, bool& ok) {
        const uint8_t* data = nullptr;
        size_t len = 0;
        proto_t r = proto::make(pkt, n);
        proto_field_t f;
        while (proto::next(r, f))
            if (f.num == 3 && f.wire == 2) {
                data = f.data;
                len = f.len;
            }
        if (!data)
            return false;
        struct item_t {
            uint32_t type;
            std::vector<uint8_t> bytes;
        };
        std::vector<item_t> msgs;
        bool changed = false;
        bits_t b = bits::make(data, len);
        std::vector<uint8_t> repl;
        while (bits::left(b) >= 8) {
            item_t m;
            m.type = bits::read_ubitvar(b);
            uint32_t size = bits::read_varint(b);
            if (b.bad || size > len)
                break;
            m.bytes.resize(size);
            if (!bits::read_bytes(b, m.bytes.data(), size))
                break;
            if (m.type == svc_create_string_table && add_to_create(add, m.bytes.data(), m.bytes.size(), repl, ok)) {
                m.bytes = repl;
                changed = true;
            }
            msgs.push_back(std::move(m));
        }
        if (!changed)
            return false;
        writer_t w;
        for (const item_t& m : msgs) {
            put_ubitvar(w, m.type);
            put_varint_bits(w, (uint32_t)m.bytes.size());
            for (uint8_t byte : m.bytes)
                put(w, byte, 8);
        }
        out.clear();
        r = proto::make(pkt, n);
        const uint8_t* at = r.p;
        while (proto::next(r, f)) {
            if (f.num == 3 && f.wire == 2)
                field_bytes(out, 3, w.buf.data(), w.buf.size());
            else
                out.insert(out.end(), at, r.p);
            at = r.p;
        }
        return true;
    }

    bool fix(const std::string& in, const std::string& out_path, std::string& err) {
        pass_t* s = new pass_t();
        parser_t* p = new parser_t();
        if (!parser::open(*p, in.c_str())) {
            err = p->err;
            demo::close(p->demo);
            delete p;
            delete s;
            return false;
        }
        callbacks_t cb;
        cb.user = s;
        cb.on_msg = collect;
        cb.on_frame = frame_seen;
        bool parsed = parser::run(*p, cb);
        err = p->err;
        demo::close(p->demo);
        delete p;
        std::sort(s->add.begin(), s->add.end(), [](const entry_t& a, const entry_t& b) { return a.index < b.index; });
        if (!parsed || !crashes(s->watch) || s->add.empty()) {
            if (err.empty())
                err = "nothing to fix in this demo";
            delete s;
            return false;
        }
        demo_t* d = new demo_t();
        if (!demo::open(*d, in.c_str())) {
            err = d->err;
            delete d;
            delete s;
            return false;
        }
        FILE* out = _wfopen(text::wide(out_path).c_str(), L"wb");
        if (!out) {
            err = "cant write " + out_path;
            demo::close(*d);
            delete d;
            delete s;
            return false;
        }
        std::vector<uint8_t> head(d->data, d->data + d->first);
        fwrite(head.data(), 1, head.size(), out);
        struct place_t {
            uint64_t start, end, now;
            bool changed;
        };
        std::vector<place_t> places;
        uint64_t written = head.size();
        bool ok = true;
        int changed_frames = 0;
        frame_t f;
        std::vector<uint8_t> body;
        while (demo::next(*d, f)) {
            size_t start = places.empty() ? d->first : (size_t)places.back().end;
            size_t end = d->pos;
            bool changed = false;
            if (f.cmd == dem_signon_packet)
                changed = add_to_packet(s->add, f.data, f.len, body, ok);
            else if (f.cmd == dem_string_tables)
                changed = add_to_snapshot(s->add, f.data, f.len, body, ok);
            bool placed = changed;
            if (f.cmd == dem_send_tables)
                changed = rename_mutes(f.data, f.len, body);
            uint64_t now = written;
            if (changed) {
                std::vector<uint8_t> frame;
                varint(frame, (uint64_t)f.cmd);
                varint(frame, (uint32_t)f.tick);
                varint(frame, body.size());
                frame.insert(frame.end(), body.begin(), body.end());
                fwrite(frame.data(), 1, frame.size(), out);
                written += frame.size();
                changed_frames += placed;
            } else {
                fwrite(d->data + start, 1, end - start, out);
                written += end - start;
            }
            places.push_back({start, end, now, changed});
            if (f.cmd == dem_stop)
                break;
        }
        for (int slot = 0; slot < 2 && head.size() >= 16; slot++) {
            uint32_t off;
            memcpy(&off, head.data() + 8 + slot * 4, 4);
            for (const place_t& pl : places)
                if (off >= pl.start && off < pl.end && (!pl.changed || off == pl.start)) {
                    uint32_t now = (uint32_t)(pl.now + (off - pl.start));
                    fseek(out, 8 + slot * 4, SEEK_SET);
                    fwrite(&now, 4, 1, out);
                }
        }
        fclose(out);
        demo::close(*d);
        delete d;
        if (!ok || changed_frames == 0) {
            err = "could not place the missing baselines";
            _wremove(text::wide(out_path).c_str());
            delete s;
            return false;
        }
        dbg::print("[basefix] %s: %zu late baselines moved into %d signon frames", in.c_str(), s->add.size(), changed_frames);
        delete s;
        return true;
    }

    bool unmute(const uint8_t* dem, size_t len, FILE* out) {
        const size_t HEAD = 16;
        if (len < HEAD)
            return false;
        const uint8_t* p = dem + HEAD;
        const uint8_t* end = dem + len;
        while (p < end) {
            const uint8_t* start = p;
            uint64_t cmd, tick, size;
            if (!proto::varint(p, end, cmd) || !proto::varint(p, end, tick) || !proto::varint(p, end, size) || size > (uint64_t)(end - p))
                return false;
            const uint8_t* data = p;
            p += size;
            int type = (int)(cmd & ~64ull);
            if (type == dem_stop)
                return false;
            if (type != dem_send_tables)
                continue;
            std::vector<uint8_t> plain, body;
            size_t n = (size_t)size;
            if (cmd & 64) {
                if (!snappy::decompress(data, (size_t)size, plain, n))
                    return false;
                data = plain.data();
            }
            if (!rename_mutes(data, n, body))
                return false;
            std::vector<uint8_t> frame;
            varint(frame, (uint64_t)type);
            varint(frame, tick);
            varint(frame, body.size());
            frame.insert(frame.end(), body.begin(), body.end());
            size_t from = start - dem, to = p - dem;
            int64_t delta = (int64_t)frame.size() - (int64_t)(to - from);
            uint8_t head[HEAD];
            memcpy(head, dem, HEAD);
            for (int slot = 0; slot < 2; slot++) {
                uint32_t off;
                memcpy(&off, head + 8 + slot * 4, 4);
                if (off >= to) {
                    off = (uint32_t)(off + delta);
                    memcpy(head + 8 + slot * 4, &off, 4);
                }
            }
            return fwrite(head, 1, HEAD, out) == HEAD && fwrite(dem + HEAD, 1, from - HEAD, out) == from - HEAD &&
                   fwrite(frame.data(), 1, frame.size(), out) == frame.size() && fwrite(dem + to, 1, len - to, out) == len - to;
        }
        return false;
    }
}
