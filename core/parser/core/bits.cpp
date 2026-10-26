#include "pch.h"

#include "parser/core/bits.h"

namespace bits {

bits_t make(const uint8_t* buf, size_t len) {
    bits_t b;
    b.buf = buf;
    b.len = len;
    return b;
}

size_t left(const bits_t& b) {
    if (b.bad)
        return 0;
    return b.len * 8 - b.bit;
}

static uint64_t load(const bits_t& b, size_t at) {
    uint64_t word = 0;
    if (at + 8 <= b.len)
        memcpy(&word, b.buf + at, 8);
    else if (at < b.len)
        memcpy(&word, b.buf + at, b.len - at);
    return word;
}

uint32_t peek(const bits_t& b, int n) {
    uint64_t word = load(b, b.bit >> 3);
    return (uint32_t)((word >> (b.bit & 7)) & ((1ull << n) - 1));
}

void skip(bits_t& b, int n) {
    if (b.bit + n > b.len * 8) {
        b.bad = true;
        return;
    }
    b.bit += n;
}

uint32_t read(bits_t& b, int n) {
    if (b.bit + n > b.len * 8) {
        b.bad = true;
        return 0;
    }
    uint64_t word = load(b, b.bit >> 3);
    uint32_t v = (uint32_t)((word >> (b.bit & 7)) & ((1ull << n) - 1));
    b.bit += n;
    return v;
}

bool read_bit(bits_t& b) {
    if (b.bit >= b.len * 8) {
        b.bad = true;
        return false;
    }
    bool v = (b.buf[b.bit >> 3] >> (b.bit & 7)) & 1;
    b.bit++;
    return v;
}

uint32_t read_ubitvar(bits_t& b) {
    uint32_t v = read(b, 6);
    switch (v & 0x30) {
    case 0x10:
        return (v & 15) | (read(b, 4) << 4);
    case 0x20:
        return (v & 15) | (read(b, 8) << 4);
    case 0x30:
        return (v & 15) | (read(b, 28) << 4);
    }
    return v;
}

uint32_t read_ubitvar_fp(bits_t& b) {
    if (read_bit(b)) return read(b, 2);
    if (read_bit(b)) return read(b, 4);
    if (read_bit(b)) return read(b, 10);
    if (read_bit(b)) return read(b, 17);
    return read(b, 31);
}

uint32_t read_varint(bits_t& b) {
    if (b.bit + 40 <= b.len * 8) {
        uint64_t word = load(b, b.bit >> 3) >> (b.bit & 7);
        uint32_t v = 0;
        for (int i = 0; i < 5; i++) {
            uint32_t byte = (uint32_t)(word >> (i * 8)) & 0xff;
            v |= (byte & 0x7f) << (i * 7);
            if (!(byte & 0x80)) {
                b.bit += (i + 1) * 8;
                return v;
            }
        }
        b.bit += 40;
        b.bad = true;
        return 0;
    }
    uint32_t v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        uint32_t byte = read(b, 8);
        v |= (byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return v;
    }
    b.bad = true;
    return 0;
}

uint64_t read_varint64(bits_t& b) {
    uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        uint64_t byte = read(b, 8);
        v |= (byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return v;
    }
    b.bad = true;
    return 0;
}

int32_t read_svarint(bits_t& b) {
    uint32_t u = read_varint(b);
    return (int32_t)((u >> 1) ^ -(int32_t)(u & 1));
}

int64_t read_svarint64(bits_t& b) {
    uint64_t u = read_varint64(b);
    return (int64_t)((u >> 1) ^ -(int64_t)(u & 1));
}

float read_float(bits_t& b) {
    uint32_t u = read(b, 32);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

float read_coord(bits_t& b) {
    bool has_int = read_bit(b);
    bool has_frac = read_bit(b);
    if (!has_int && !has_frac)
        return 0.f;
    bool neg = read_bit(b);
    uint32_t i = has_int ? read(b, 14) + 1 : 0;
    uint32_t frac = has_frac ? read(b, 5) : 0;
    float v = (float)i + (float)frac / 32.f;
    return neg ? -v : v;
}

float read_normal(bits_t& b) {
    bool neg = read_bit(b);
    float v = (float)read(b, 11) / 2047.f;
    return neg ? -v : v;
}

float read_angle(bits_t& b, int n) {
    return (float)read(b, n) * 360.f / (float)(1u << n);
}

bool read_bytes(bits_t& b, uint8_t* dst, size_t n) {
    if (b.bit + n * 8 > b.len * 8) {
        b.bad = true;
        return false;
    }
    const uint8_t* src = b.buf + (b.bit >> 3);
    int shift = b.bit & 7;
    if (!shift) {
        memcpy(dst, src, n);
    } else {
        size_t i = 0;
        for (; i + 8 < n; i += 8) {
            uint64_t lo, hi = src[i + 8];
            memcpy(&lo, src + i, 8);
            uint64_t w = (lo >> shift) | (hi << (64 - shift));
            memcpy(dst + i, &w, 8);
        }
        for (; i < n; i++)
            dst[i] = (uint8_t)((src[i] >> shift) | (src[i + 1] << (8 - shift)));
    }
    b.bit += n * 8;
    return true;
}

void read_cstring(bits_t& b, std::string& out, size_t max) {
    out.clear();
    while (out.size() < max) {
        uint32_t c = read(b, 8);
        if (b.bad || !c)
            break;
        out.push_back((char)c);
    }
}

}
