#include "pch.h"

#include "game/lz4.h"
#include "geo/kv3.h"
#include "parser/demo/zstdx.h"

static const uint32_t trailer_magic = 0xffeedd00;

enum kv_wire_t : uint8_t {
    w_null = 1,
    w_boolean = 2,
    w_int64 = 3,
    w_uint64 = 4,
    w_double = 5,
    w_string = 6,
    w_blob = 7,
    w_array = 8,
    w_object = 9,
    w_array_typed = 10,
    w_int32 = 11,
    w_uint32 = 12,
    w_true = 13,
    w_false = 14,
    w_int64_zero = 15,
    w_int64_one = 16,
    w_double_zero = 17,
    w_double_one = 18,
    w_float = 19,
    w_int16 = 20,
    w_uint16 = 21,
    w_int32_as_byte = 23,
    w_array_byte_length = 24,
    w_array_aux = 25,
};

struct cursor_t {
    const uint8_t* p = nullptr;
    size_t n = 0;
};

struct bufs_t {
    cursor_t b1, b2, b4, b8;
};

struct kv_ctx_t {
    int version = 0;
    std::vector<std::string> strings;
    bufs_t buf, aux;
    cursor_t types, object_lengths, blob_lengths, blobs;
    bool bad = false;
    int depth = 0;
};

static bool take(cursor_t& c, size_t k, const uint8_t*& out) {
    if (k > c.n)
        return false;
    out = c.p;
    c.p += k;
    c.n -= k;
    return true;
}

template <typename T>
static T rd(kv_ctx_t& c, cursor_t& cur) {
    T v{};
    const uint8_t* q = nullptr;
    if (!take(cur, sizeof(T), q)) {
        c.bad = true;
        return v;
    }
    memcpy(&v, q, sizeof(T));
    return v;
}

static uint8_t read_type(kv_ctx_t& c) {
    uint8_t t = rd<uint8_t>(c, c.types);
    if (c.version >= 3) {
        if (t & 0x80) {
            t &= 0x3f;
            rd<uint8_t>(c, c.types);
        }
    } else if (t & 0x80) {
        t &= 0x7f;
        rd<uint8_t>(c, c.types);
    }
    return t;
}

static void read_value(kv_ctx_t& c, uint8_t type, kv_node_t& out);

static void read_member(kv_ctx_t& c, kv_node_t& parent) {
    uint8_t type = read_type(c);
    if (parent.type == kv_array) {
        parent.items.emplace_back();
        read_value(c, type, parent.items.back());
        return;
    }
    int32_t id = rd<int32_t>(c, c.buf.b4);
    std::string name;
    if (id >= 0 && (size_t)id < c.strings.size())
        name = c.strings[id];
    else if (id != -1)
        c.bad = true;
    parent.keys.push_back(std::move(name));
    parent.items.emplace_back();
    read_value(c, type, parent.items.back());
}

static void read_value(kv_ctx_t& c, uint8_t type, kv_node_t& out) {
    if (c.bad)
        return;
    if (++c.depth > 200) {
        c.bad = true;
        return;
    }
    bufs_t& b = c.buf;
    switch (type) {
    case w_null: out.type = kv_null; break;
    case w_true: out.type = kv_bool; out.b = true; break;
    case w_false: out.type = kv_bool; out.b = false; break;
    case w_int64_zero: out.type = kv_int; out.i = 0; break;
    case w_int64_one: out.type = kv_int; out.i = 1; break;
    case w_double_zero: out.type = kv_double; out.d = 0.0; break;
    case w_double_one: out.type = kv_double; out.d = 1.0; break;
    case w_boolean: out.type = kv_bool; out.b = rd<uint8_t>(c, b.b1) == 1; break;
    case w_int32_as_byte: out.type = kv_int; out.i = rd<uint8_t>(c, b.b1); break;
    case w_int16: out.type = kv_int; out.i = rd<int16_t>(c, b.b2); break;
    case w_uint16: out.type = kv_uint; out.u = rd<uint16_t>(c, b.b2); break;
    case w_int32: out.type = kv_int; out.i = rd<int32_t>(c, b.b4); break;
    case w_uint32: out.type = kv_uint; out.u = rd<uint32_t>(c, b.b4); break;
    case w_float: out.type = kv_double; out.d = rd<float>(c, b.b4); break;
    case w_int64: out.type = kv_int; out.i = rd<int64_t>(c, b.b8); break;
    case w_uint64: out.type = kv_uint; out.u = rd<uint64_t>(c, b.b8); break;
    case w_double: out.type = kv_double; out.d = rd<double>(c, b.b8); break;
    case w_string: {
        int32_t id = rd<int32_t>(c, b.b4);
        out.type = kv_string;
        if (id >= 0 && (size_t)id < c.strings.size())
            out.s = c.strings[id];
        else if (id != -1)
            c.bad = true;
        break;
    }
    case w_blob: {
        out.type = kv_blob;
        int32_t len = 0;
        const uint8_t* q = nullptr;
        if (c.version < 2) {
            len = rd<int32_t>(c, b.b4);
            if (len > 0 && !take(b.b1, (size_t)len, q)) {
                c.bad = true;
                break;
            }
        } else {
            len = rd<int32_t>(c, c.blob_lengths);
            if (len > 0 && !take(c.blobs, (size_t)len, q)) {
                c.bad = true;
                break;
            }
        }
        if (len > 0)
            out.blob.assign(q, q + len);
        break;
    }
    case w_array: {
        out.type = kv_array;
        int32_t len = rd<int32_t>(c, b.b4);
        if (len < 0) {
            c.bad = true;
            break;
        }
        out.items.reserve(len);
        for (int32_t k = 0; k < len && !c.bad; k++)
            read_member(c, out);
        break;
    }
    case w_array_typed:
    case w_array_byte_length: {
        out.type = kv_array;
        int32_t len = type == w_array_byte_length ? rd<uint8_t>(c, b.b1) : rd<int32_t>(c, b.b4);
        if (len < 0) {
            c.bad = true;
            break;
        }
        uint8_t sub = read_type(c);
        out.items.resize(len);
        for (int32_t k = 0; k < len && !c.bad; k++)
            read_value(c, sub, out.items[k]);
        break;
    }
    case w_array_aux: {
        out.type = kv_array;
        int32_t len = rd<uint8_t>(c, b.b1);
        uint8_t sub = read_type(c);
        std::swap(c.buf, c.aux);
        out.items.resize(len);
        for (int32_t k = 0; k < len && !c.bad; k++)
            read_value(c, sub, out.items[k]);
        std::swap(c.buf, c.aux);
        break;
    }
    case w_object: {
        out.type = kv_object;
        int32_t len = c.version >= 5 ? rd<int32_t>(c, c.object_lengths) : rd<int32_t>(c, b.b4);
        if (len < 0) {
            c.bad = true;
            break;
        }
        out.items.reserve(len);
        out.keys.reserve(len);
        for (int32_t k = 0; k < len && !c.bad; k++)
            read_member(c, out);
        break;
    }
    default:
        c.bad = true;
        break;
    }
    c.depth--;
}

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) / a * a;
}

static bool split(const std::vector<uint8_t>& raw, size_t start, size_t c1, size_t c2, size_t c4, size_t c8,
                  bool align_empty8, bufs_t& out, size_t& end) {
    size_t off = start;
    auto slice = [&](size_t len, cursor_t& cur) {
        if (off + len > raw.size())
            return false;
        cur.p = raw.data() + off;
        cur.n = len;
        off += len;
        return true;
    };
    if (c1 && !slice(c1, out.b1))
        return false;
    if (c2) {
        off = align_up(off, 2);
        if (!slice(c2 * 2, out.b2))
            return false;
    }
    if (c4) {
        off = align_up(off, 4);
        if (!slice(c4 * 4, out.b4))
            return false;
    }
    if (c8) {
        off = align_up(off, 8);
        if (!slice(c8 * 8, out.b8))
            return false;
    } else if (align_empty8) {
        off = align_up(off, 8);
    }
    end = off;
    return true;
}

static bool fetch(const uint8_t* block, size_t n, size_t& pos, size_t comp, size_t unc, uint32_t method, std::vector<uint8_t>& out,
                  std::string& err) {
    if (method == 0) {
        if (pos + unc > n) {
            err = "kv3: truncated buffer";
            return false;
        }
        out.assign(block + pos, block + pos + unc);
        pos += unc;
        return true;
    }
    if (pos + comp > n) {
        err = "kv3: truncated compressed buffer";
        return false;
    }
    if (method == 1) {
        out.resize(unc);
        size_t len = 0;
        if (!lz4::decompress(block + pos, comp, out.data(), unc, len) || len != unc) {
            err = "kv3: bad lz4";
            return false;
        }
        pos += comp;
        return true;
    }
    size_t len = 0;
    if (!zstdx::decompress(block + pos, comp, out, len, err))
        return false;
    if (len != unc) {
        err = "kv3: wrong size after zstd";
        return false;
    }
    out.resize(len);
    pos += comp;
    return true;
}

namespace kv3 {

const kv_node_t* get(const kv_node_t& node, const char* key) {
    if (node.type != kv_object)
        return nullptr;
    for (size_t k = 0; k < node.keys.size(); k++)
        if (node.keys[k] == key)
            return &node.items[k];
    return nullptr;
}

int64_t integer(const kv_node_t& node) {
    switch (node.type) {
    case kv_int: return node.i;
    case kv_uint: return (int64_t)node.u;
    case kv_double: return (int64_t)node.d;
    case kv_bool: return node.b ? 1 : 0;
    }
    return 0;
}

bool read(const uint8_t* block, size_t n, kv_node_t& root, std::string& err) {
    size_t pos = 0;
    auto u32 = [&](uint32_t& v) {
        if (pos + 4 > n)
            return false;
        memcpy(&v, block + pos, 4);
        pos += 4;
        return true;
    };
    auto i32 = [&]() {
        uint32_t v = 0;
        if (!u32(v))
            return (int32_t)-1;
        return (int32_t)v;
    };
    auto u16 = [&]() {
        uint16_t v = 0;
        if (pos + 2 > n)
            return (uint16_t)0;
        memcpy(&v, block + pos, 2);
        pos += 2;
        return v;
    };

    uint32_t magic = 0;
    if (!u32(magic)) {
        err = "kv3: empty block";
        return false;
    }
    int version = (int)(magic & 0xff);
    if ((magic & 0xffffff00u) != 0x4b563300u || version < 1 || version > 5) {
        err = "kv3: unknown signature or version";
        return false;
    }
    pos += 16;
    uint32_t method = 0;
    if (!u32(method)) {
        err = "kv3: truncated header";
        return false;
    }
    if (method > 2) {
        err = "kv3: unknown compression";
        return false;
    }

    int32_t c1 = 0, c4 = 0, c8 = 0, count_types = 0, unc_total = 0, comp_total = 0;
    int32_t count_blocks = 0, blob_bytes = 0, c2 = 0;
    uint16_t frame = 0;
    if (version == 1) {
        c1 = i32();
        c4 = i32();
        c8 = i32();
        unc_total = i32();
        comp_total = (int32_t)(n - pos);
    } else {
        uint16_t dict = u16();
        frame = u16();
        if (dict != 0 || (method == 2 && frame != 0)) {
            err = "kv3: compression dictionary not supported";
            return false;
        }
        c1 = i32();
        c4 = i32();
        c8 = i32();
        count_types = i32();
        u16();
        u16();
        unc_total = i32();
        comp_total = i32();
        count_blocks = i32();
        blob_bytes = i32();
    }
    if (version >= 4) {
        c2 = i32();
        i32();
    }
    int32_t unc1 = unc_total, comp1 = comp_total, unc2 = 0, comp2 = 0;
    int32_t b1_2 = 0, b2_2 = 0, b4_2 = 0, b8_2 = 0, obj2 = 0;
    if (version >= 5) {
        unc1 = i32();
        comp1 = i32();
        unc2 = i32();
        comp2 = i32();
        b1_2 = i32();
        b2_2 = i32();
        b4_2 = i32();
        b8_2 = i32();
        i32();
        obj2 = i32();
        i32();
        i32();
    }
    if (c1 < 0 || c4 <= 0 || c8 < 0 || c2 < 0 || unc1 < 0 || unc2 < 0 || blob_bytes < 0 || count_blocks < 0) {
        err = "kv3: bad header counts";
        return false;
    }
    if (version < 5 && method == 2) {
        err = "kv3: zstd below version 5 not supported";
        return false;
    }

    kv_ctx_t c;
    c.version = version;
    std::vector<uint8_t> raw1, raw2, raw_blobs;
    if (!fetch(block, n, pos, comp1, unc1, method, raw1, err))
        return false;
    bufs_t b1;
    size_t end1 = 0;
    if (!split(raw1, 0, c1, c2, c4, c8, version < 5, b1, end1)) {
        err = "kv3: buffer 1 too short";
        return false;
    }
    int32_t count_strings = rd<int32_t>(c, b1.b4);
    if (c.bad || count_strings < 0) {
        err = "kv3: no string count";
        return false;
    }

    cursor_t strings;
    if (version >= 5) {
        strings = b1.b1;
    } else {
        strings.p = raw1.data() + end1;
        strings.n = raw1.size() - end1;
    }
    c.strings.reserve(count_strings);
    for (int32_t k = 0; k < count_strings; k++) {
        const void* z = memchr(strings.p, 0, strings.n);
        if (!z) {
            err = "kv3: unterminated string";
            return false;
        }
        size_t len = (size_t)((const uint8_t*)z - strings.p);
        c.strings.emplace_back((const char*)strings.p, len);
        strings.p += len + 1;
        strings.n -= len + 1;
    }

    cursor_t after_types;
    if (version >= 5) {
        b1.b1 = strings;
        c.aux = b1;
        if (!fetch(block, n, pos, comp2, unc2, method, raw2, err))
            return false;
        size_t obj_bytes = (size_t)obj2 * 4;
        if (obj_bytes > raw2.size()) {
            err = "kv3: buffer 2 too short";
            return false;
        }
        c.object_lengths.p = raw2.data();
        c.object_lengths.n = obj_bytes;
        size_t end2 = 0;
        if (!split(raw2, obj_bytes, b1_2, b2_2, b4_2, b8_2, false, c.buf, end2)) {
            err = "kv3: buffer 2 too short";
            return false;
        }
        if (end2 + (size_t)count_types > raw2.size()) {
            err = "kv3: no types";
            return false;
        }
        c.types.p = raw2.data() + end2;
        c.types.n = count_types;
        after_types.p = c.types.p + count_types;
        after_types.n = raw2.size() - end2 - count_types;
    } else {
        c.buf = b1;
        size_t types_len = version == 1 ? (size_t)unc_total - (strings.p - raw1.data()) - 4
                                        : (size_t)count_types - (strings.p - (raw1.data() + end1));
        if (types_len > strings.n) {
            err = "kv3: no types";
            return false;
        }
        c.types.p = strings.p;
        c.types.n = types_len;
        after_types.p = strings.p + types_len;
        after_types.n = strings.n - types_len;
    }

    uint32_t trailer = 0;
    if (count_blocks == 0) {
        if (after_types.n < 4) {
            err = "kv3: no end marker";
            return false;
        }
        memcpy(&trailer, after_types.p, 4);
        if (trailer != trailer_magic) {
            err = "kv3: bad end marker";
            return false;
        }
    } else {
        size_t lens = (size_t)count_blocks * 4;
        if (after_types.n < lens + 4) {
            err = "kv3: no blob lengths";
            return false;
        }
        c.blob_lengths.p = after_types.p;
        c.blob_lengths.n = lens;
        memcpy(&trailer, after_types.p + lens, 4);
        if (trailer != trailer_magic) {
            err = "kv3: bad marker after blob lengths";
            return false;
        }
        size_t comp_blobs = (size_t)comp_total - (size_t)comp1 - (size_t)comp2;
        if (method == 1) {
            err = "kv3: lz4 blobs not supported";
            return false;
        }
        if (!fetch(block, n, pos, comp_blobs, blob_bytes, method, raw_blobs, err))
            return false;
        c.blobs.p = raw_blobs.data();
        c.blobs.n = raw_blobs.size();
        uint32_t t2 = 0;
        if (!u32(t2) || t2 != trailer_magic) {
            err = "kv3: bad blob end marker";
            return false;
        }
    }

    uint8_t root_type = read_type(c);
    read_value(c, root_type, root);
    if (c.bad) {
        err = "kv3: inconsistent data while reading the tree";
        return false;
    }
    if (c.types.n || c.object_lengths.n || c.blob_lengths.n || c.blobs.n || c.buf.b1.n || c.buf.b2.n ||
        c.buf.b4.n || c.buf.b8.n || (version >= 5 && (c.aux.b1.n || c.aux.b2.n || c.aux.b4.n || c.aux.b8.n))) {
        err = "kv3: leftover data, read went out of step";
        return false;
    }
    return true;
}

}
