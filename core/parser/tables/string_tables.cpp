#include "pch.h"

#include "parser/core/bits.h"
#include "parser/demo/snappy.h"
#include "parser/proto/proto.h"
#include "parser/tables/string_tables.h"

namespace string_tables {

static const size_t history_len = 32;

static bool decode(string_table_t& t, const uint8_t* blob, size_t len, int32_t count,
                   std::vector<int>& changed, std::string& err) {
    bits_t b = bits::make(blob, len);
    std::string history[history_len];
    size_t nhist = 0;
    int32_t index = -1;
    std::string tmp;
    std::vector<uint8_t> raw, unpacked;

    for (int32_t n = 0; n < count; n++) {
        if (bits::read_bit(b))
            index++;
        else
            index += (int32_t)bits::read_varint(b) + 2;
        if (b.bad || index < 0 || index > 65535) {
            err = "bad string table index";
            return false;
        }
        if ((size_t)index >= t.entries.size())
            t.entries.resize(index + 1);
        table_entry_t& e = t.entries[index];

        if (bits::read_bit(b)) {
            std::string key;
            if (bits::read_bit(b)) {
                uint32_t pos = bits::read(b, 5);
                uint32_t size = bits::read(b, 5);
                if (pos < nhist) {
                    const std::string& src = history[pos];
                    key = src.substr(0, size < src.size() ? size : src.size());
                }
                bits::read_cstring(b, tmp, 4096);
                key += tmp;
            } else {
                bits::read_cstring(b, key, 4096);
            }
            if (nhist == history_len) {
                for (size_t i = 1; i < history_len; i++)
                    history[i - 1].swap(history[i]);
                nhist--;
            }
            history[nhist++] = key;
            e.key = key;
        }

        if (bits::read_bit(b)) {
            bool compressed = false;
            uint32_t nbits;
            if (t.fixed_size) {
                nbits = (uint32_t)t.size_bits;
            } else {
                if (t.flags & 1)
                    compressed = bits::read_bit(b);
                if (t.varint_bits)
                    nbits = bits::read_ubitvar(b) * 8;
                else
                    nbits = bits::read(b, 17) * 8;
            }
            if (nbits > (8u << 20)) {
                err = "string table entry too big";
                return false;
            }
            raw.assign((nbits + 7) / 8, 0);
            bits::read_bytes(b, raw.data(), nbits / 8);
            if (nbits & 7)
                raw[nbits / 8] = (uint8_t)bits::read(b, nbits & 7);
            if (b.bad) {
                err = "truncated string table entry";
                return false;
            }
            if (compressed) {
                size_t out_len = 0;
                if (!snappy::decompress(raw.data(), raw.size(), unpacked, out_len)) {
                    err = "bad snappy in string table entry";
                    return false;
                }
                e.data.assign(unpacked.begin(), unpacked.begin() + out_len);
            } else {
                e.data.swap(raw);
            }
        }
        changed.push_back(index);
    }
    return true;
}

bool create(string_tables_t& st, const uint8_t* msg, size_t n, int& index, std::string& err) {
    string_table_t t;
    const uint8_t* data = nullptr;
    size_t len = 0;
    int32_t count = 0;
    bool compressed = false;

    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        switch (f.num) {
        case 1: t.name = proto::str(f); break;
        case 2: count = proto::i32(f); break;
        case 3: t.fixed_size = f.val != 0; break;
        case 5: t.size_bits = proto::i32(f); break;
        case 6: t.flags = proto::i32(f); break;
        case 7: data = f.data; len = f.len; break;
        case 9: compressed = f.val != 0; break;
        case 10: t.varint_bits = f.val != 0; break;
        }
    }
    if (r.bad) {
        err = "bad create string table message";
        return false;
    }

    st.list.push_back(std::move(t));
    index = (int)st.list.size() - 1;
    st.changed.clear();
    if (!data)
        return true;

    std::vector<uint8_t> plain;
    if (compressed) {
        size_t out_len = 0;
        if (!snappy::decompress(data, len, plain, out_len)) {
            err = "bad snappy in string table data";
            return false;
        }
        plain.resize(out_len + 16);
        return decode(st.list[index], plain.data(), out_len, count, st.changed, err);
    }
    return decode(st.list[index], data, len, count, st.changed, err);
}

bool update(string_tables_t& st, const uint8_t* msg, size_t n, int& index, std::string& err) {
    int32_t id = -1, count = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;

    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 1)
            id = proto::i32(f);
        else if (f.num == 2)
            count = proto::i32(f);
        else if (f.num == 3 && f.wire == 2) {
            data = f.data;
            len = f.len;
        }
    }
    if (r.bad || id < 0 || id >= (int32_t)st.list.size()) {
        err = "bad update string table message";
        return false;
    }
    index = id;
    st.changed.clear();
    if (!data)
        return true;
    return decode(st.list[id], data, len, count, st.changed, err);
}

bool load(string_tables_t& st, const uint8_t* msg, size_t n, std::string& err) {
    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num != 1 || f.wire != 2)
            continue;
        std::string name;
        std::vector<table_entry_t> items;
        proto_t tr = proto::make(f.data, f.len);
        proto_field_t tf;
        while (proto::next(tr, tf)) {
            if (tf.num == 1 && tf.wire == 2) {
                name = proto::str(tf);
            } else if (tf.num == 2 && tf.wire == 2) {
                table_entry_t e;
                proto_t ir = proto::make(tf.data, tf.len);
                proto_field_t i;
                while (proto::next(ir, i)) {
                    if (i.num == 1 && i.wire == 2)
                        e.key = proto::str(i);
                    else if (i.num == 2 && i.wire == 2)
                        e.data.assign(i.data, i.data + i.len);
                }
                items.push_back(std::move(e));
            }
        }
        if (tr.bad) {
            err = "bad string table snapshot";
            return false;
        }
        int at = find(st, name.c_str());
        if (at >= 0)
            st.list[at].entries.swap(items);
    }
    if (r.bad) {
        err = "bad string table snapshot";
        return false;
    }
    return true;
}

void clear(string_tables_t& st) {
    st.list.clear();
    st.changed.clear();
}

int find(const string_tables_t& st, const char* name) {
    for (size_t i = 0; i < st.list.size(); i++)
        if (st.list[i].name == name)
            return (int)i;
    return -1;
}

}
