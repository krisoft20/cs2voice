// zstdx - a Zstandard decoder (RFC 8878) written from scratch for cs2voice
//
// Decoding only. With threads, one thread reads block headers and tables, helpers decode
// the blocks (Huffman literals, FSE sequences), the calling thread executes the sequences
// in order, another one follows with the xxh64 checksum and another touches the output
// pages ahead. Three helpers is the best setting, so set_threads caps at MAX_THREADS.
//
// Against libzstd 1.5.7 built with the same MSVC (/O2), best of 5, uninitialized output:
//                     10 FACEIT demos (2.6 GB)   Silesia, level 3   Silesia, level 19
//   libzstd           3522 ms                    199 ms             220 ms
//   zstdx, 1 thread   3021 ms   1.17x            185 ms   1.07x     197 ms   1.12x
//   zstdx, 3 threads   884 ms   3.98x             71 ms   2.82x      93 ms   2.38x
// Output is identical byte for byte on those files and on 164 files packed by libzstd
// (levels 1 to 22, with and without checksums, long distance matching, several frames).
// 1300 broken files (flipped bytes, cut off, overwritten chunks) end in a clean error.
//
// MIT License
//
// Copyright (c) 2026 krisoft20
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include <intrin.h>
#include <thread>

#include "parser/demo/zstdx.h"

namespace zstdx {

static const uint32_t frame_magic = 0xfd2fb528;
static const int dict_bytes[4] = {0, 1, 2, 4};
static const int fcs_bytes_of[4] = {0, 2, 4, 8};
static const uint32_t skip_min = 0x184d2a50;
static const uint32_t skip_max = 0x184d2a5f;
static const size_t max_block = 128 * 1024;
static const size_t tail_pad = 64;
static const int huf_log = 11;

static uint32_t le32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint64_t le64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static int high_bit(uint32_t v) {
    return 31 - std::countl_zero(v);
}

struct fwd_bits_t {
    const uint8_t* p;
    size_t n;
    size_t bit;
};

static uint32_t fwd_peek(const fwd_bits_t& b, int nb) {
    size_t at = b.bit >> 3;
    uint64_t v = 0;
    if (at + 8 <= b.n) {
        v = le64(b.p + at);
    } else {
        for (size_t i = at; i < b.n; i++)
            v |= (uint64_t)b.p[i] << (8 * (i - at));
    }
    return (uint32_t)((v >> (b.bit & 7)) & ((1ull << nb) - 1));
}

static uint32_t fwd_read(fwd_bits_t& b, int nb) {
    uint32_t v = fwd_peek(b, nb);
    b.bit += nb;
    return v;
}

struct rbits_t {
    const uint8_t* start;
    const uint8_t* p;
    uint64_t c;
    unsigned used;
};

static bool rb_init(rbits_t& b, const uint8_t* src, size_t n) {
    if (!n || !src[n - 1])
        return false;
    b.start = src;
    if (n >= 8) {
        b.p = src + n - 8;
        b.c = le64(b.p);
        b.used = 0;
    } else {
        b.p = src;
        b.c = 0;
        for (size_t i = 0; i < n; i++)
            b.c |= (uint64_t)src[i] << (8 * i);
        b.used = (unsigned)(8 - n) * 8;
    }
    b.used += 8 - high_bit(src[n - 1]);
    return true;
}

static uint64_t rb_peek(const rbits_t& b, unsigned nb) {
    return ((b.c << (b.used & 63)) >> 1) >> ((63 - nb) & 63);
}

static uint64_t rb_read(rbits_t& b, unsigned nb) {
    uint64_t v = rb_peek(b, nb);
    b.used += nb;
    return v;
}

template <bool BMI2>
static uint64_t rb_take(rbits_t& b, unsigned nb) {
    uint64_t v;
    if constexpr (BMI2)
        v = _shrx_u64(_shlx_u64(b.c, b.used) >> 1, 63 ^ nb);
    else
        v = ((b.c << (b.used & 63)) >> 1) >> ((63 - nb) & 63);
    b.used += nb;
    return v;
}

static bool has_bmi2() {
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7)
        return false;
    __cpuidex(r, 7, 0);
    return (r[1] >> 8) & 1;
}

static const bool g_bmi2 = has_bmi2();

static void rb_reload(rbits_t& b) {
    if (b.used > 64)
        return;
    if (b.p >= b.start + 8) {
        b.p -= b.used >> 3;
        b.used &= 7;
        b.c = le64(b.p);
        return;
    }
    if (b.p == b.start)
        return;
    unsigned nbytes = b.used >> 3;
    if ((size_t)(b.p - b.start) < nbytes)
        nbytes = (unsigned)(b.p - b.start);
    b.p -= nbytes;
    b.used -= nbytes * 8;
    b.c = le64(b.p);
}

static bool rb_done(const rbits_t& b) {
    return b.p == b.start && b.used == 64;
}

struct fse_cell_t {
    uint16_t sym;
    uint8_t bits;
    uint16_t base;
};

struct seq_cell_t {
    uint32_t base;
    uint8_t add;
    uint8_t bits;
    uint16_t next;
};

struct fse_t {
    std::vector<fse_cell_t> cells;
    std::vector<seq_cell_t> seq;
    int log = 0;
    bool valid = false;
};

static bool read_fse(const uint8_t* p, size_t n, int max_log, int max_sym,
                     std::vector<int16_t>& norm, int& log, size_t& used) {
    fwd_bits_t br = {p, n, 0};
    int al = (int)fwd_read(br, 4) + 5;
    if (al > max_log)
        return false;
    int remaining = (1 << al) + 1;
    int threshold = 1 << al;
    int nb = al + 1;
    int sym = 0;
    bool prev_zero = false;
    norm.assign(max_sym + 1, 0);

    while (remaining > 1 && sym <= max_sym) {
        if (prev_zero) {
            int n0 = sym;
            for (;;) {
                int r = (int)fwd_read(br, 2);
                n0 += r;
                if (r != 3)
                    break;
                if (br.bit > br.n * 8)
                    return false;
            }
            if (n0 > max_sym)
                return false;
            while (sym < n0)
                norm[sym++] = 0;
            prev_zero = false;
        }
        int max = (2 * threshold - 1) - remaining;
        uint32_t v = fwd_peek(br, nb);
        int count;
        if ((int)(v & (uint32_t)(threshold - 1)) < max) {
            count = (int)(v & (uint32_t)(threshold - 1));
            br.bit += nb - 1;
        } else {
            count = (int)(v & (uint32_t)(2 * threshold - 1));
            if (count >= threshold)
                count -= max;
            br.bit += nb;
        }
        count--;
        remaining -= count < 0 ? -count : count;
        norm[sym++] = (int16_t)count;
        prev_zero = !count;
        while (remaining < threshold) {
            nb--;
            threshold >>= 1;
        }
        if (br.bit > br.n * 8)
            return false;
    }
    if (remaining != 1)
        return false;
    norm.resize(sym);
    log = al;
    used = (br.bit + 7) / 8;
    return used <= n;
}

static bool build_fse(const int16_t* norm, size_t nsym, int al, fse_t& t) {
    int size = 1 << al;
    t.cells.resize(size);
    t.log = al;
    uint32_t next[256];
    int high = size - 1;
    for (size_t s = 0; s < nsym; s++) {
        if (norm[s] == -1) {
            if (high < 0)
                return false;
            t.cells[high--].sym = (uint16_t)s;
            next[s] = 1;
        } else {
            next[s] = norm[s] < 0 ? 0 : (uint32_t)norm[s];
        }
    }
    int step = (size >> 1) + (size >> 3) + 3;
    int mask = size - 1;
    int pos = 0;
    for (size_t s = 0; s < nsym; s++) {
        for (int i = 0; i < norm[s]; i++) {
            t.cells[pos].sym = (uint16_t)s;
            do {
                pos = (pos + step) & mask;
            } while (pos > high);
        }
    }
    if (pos)
        return false;
    for (auto& c : t.cells) {
        uint32_t nx = next[c.sym]++;
        if (!nx)
            return false;
        int nb = al - high_bit(nx);
        c.bits = (uint8_t)nb;
        c.base = (uint16_t)((nx << nb) - (uint32_t)size);
    }
    t.valid = true;
    return true;
}

static void rle_table(uint8_t sym, fse_t& t) {
    t.cells.assign(1, fse_cell_t{sym, 0, 0});
    t.log = 0;
    t.valid = true;
}

struct huf_t {
    std::vector<uint8_t> syms;
    std::vector<uint8_t> lens;
    int max_bits = 0;
    bool valid = false;
};

static bool build_huf(uint8_t* weights, size_t n, huf_t& t) {
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (weights[i] > 11)
            return false;
        if (weights[i])
            total += 1u << (weights[i] - 1);
    }
    if (!total)
        return false;
    int max_bits = high_bit(total) + 1;
    if (max_bits > 11)
        return false;
    uint32_t rest = (1u << max_bits) - total;
    if (!rest || (rest & (rest - 1)))
        return false;
    if (n >= 256)
        return false;
    weights[n++] = (uint8_t)(high_bit(rest) + 1);

    uint32_t count[13] = {};
    for (size_t i = 0; i < n; i++)
        count[weights[i]]++;
    int scale = huf_log - max_bits;
    uint32_t start[13] = {};
    uint32_t acc = 0;
    for (int w = 1; w <= max_bits; w++) {
        start[w] = acc;
        acc += (count[w] << (w - 1)) << scale;
    }
    t.max_bits = max_bits;
    t.syms.resize((size_t)1 << huf_log);
    t.lens.resize((size_t)1 << huf_log);
    for (size_t s = 0; s < n; s++) {
        int w = weights[s];
        if (!w)
            continue;
        uint32_t len = (1u << (w - 1)) << scale;
        memset(t.syms.data() + start[w], (int)s, len);
        memset(t.lens.data() + start[w], max_bits + 1 - w, len);
        start[w] += len;
    }
    t.valid = true;
    return true;
}

static bool read_huf(const uint8_t* p, size_t n, huf_t& t, size_t& used, std::vector<int16_t>& norm, fse_t& ft) {
    if (n < 1)
        return false;
    uint8_t weights[256];
    size_t nw = 0;
    uint8_t header = p[0];
    if (header >= 128) {
        nw = header - 127u;
        size_t bytes = (nw + 1) / 2;
        if (1 + bytes > n)
            return false;
        for (size_t i = 0; i < nw; i++)
            weights[i] = (i & 1) ? (p[1 + i / 2] & 15) : (p[1 + i / 2] >> 4);
        used = 1 + bytes;
        return build_huf(weights, nw, t);
    }

    size_t csize = header;
    if (!csize || 1 + csize > n)
        return false;
    int al = 0;
    size_t desc = 0;
    if (!read_fse(p + 1, csize, 6, 255, norm, al, desc))
        return false;
    if (!build_fse(norm.data(), norm.size(), al, ft))
        return false;
    rbits_t b;
    if (!rb_init(b, p + 1 + desc, csize - desc))
        return false;
    uint32_t s1 = (uint32_t)rb_read(b, al);
    uint32_t s2 = (uint32_t)rb_read(b, al);
    for (;;) {
        if (nw >= 255)
            return false;
        weights[nw++] = (uint8_t)ft.cells[s1].sym;
        s1 = ft.cells[s1].base + (uint32_t)rb_read(b, ft.cells[s1].bits);
        rb_reload(b);
        if (b.used > 64) {
            weights[nw++] = (uint8_t)ft.cells[s2].sym;
            break;
        }
        if (nw >= 255)
            return false;
        weights[nw++] = (uint8_t)ft.cells[s2].sym;
        s2 = ft.cells[s2].base + (uint32_t)rb_read(b, ft.cells[s2].bits);
        rb_reload(b);
        if (b.used > 64) {
            weights[nw++] = (uint8_t)ft.cells[s1].sym;
            break;
        }
    }
    used = 1 + csize;
    return build_huf(weights, nw, t);
}

static bool huf_tail(rbits_t& b, const huf_t& t, uint8_t* o, uint8_t* oend) {
    const uint8_t* syms = t.syms.data();
    const uint8_t* lens = t.lens.data();
    while (o < oend) {
        rb_reload(b);
        size_t i = (size_t)rb_peek(b, huf_log);
        *o++ = syms[i];
        b.used += lens[i];
    }
    return rb_done(b);
}

static bool huf_stream(const uint8_t* src, size_t n, const huf_t& t, uint8_t* out, size_t count) {
    rbits_t b;
    if (!rb_init(b, src, n))
        return false;
    return huf_tail(b, t, out, out + count);
}

template <bool BMI2>
static uint64_t shl(uint64_t v, unsigned n) {
    if constexpr (BMI2)
        return _shlx_u64(v, n);
    else
        return v << n;
}

template <bool BMI2>
static void huf_reload(const uint8_t*& p, uint64_t& v) {
    unsigned used = (unsigned)std::countr_zero(v);
    p -= used >> 3;
    v = shl<BMI2>(le64(p) | 1, used & 7);
}

template <bool BMI2>
static bool huf_streams4(const uint8_t* const* src, const size_t* n, const huf_t& t, uint8_t* out,
                         size_t part, size_t regen) {
    rbits_t b[4];
    for (int s = 0; s < 4; s++)
        if (!rb_init(b[s], src[s], n[s]))
            return false;

    const uint8_t* syms = t.syms.data();
    const uint8_t* lens = t.lens.data();
    const uint8_t *s0 = b[0].start + 8, *s1 = b[1].start + 8, *s2 = b[2].start + 8, *s3 = b[3].start + 8;
    const uint8_t *p0 = b[0].p, *p1 = b[1].p, *p2 = b[2].p, *p3 = b[3].p;
    uint8_t *o0 = out, *o1 = out + part, *o2 = out + 2 * part, *o3 = out + 3 * part;
    uint8_t* end3 = out + regen;

    bool fast = b[0].used < 64 && b[1].used < 64 && b[2].used < 64 && b[3].used < 64;
    uint64_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;
    if (fast) {
        v0 = 1ull << b[0].used;
        v1 = 1ull << b[1].used;
        v2 = 1ull << b[2].used;
        v3 = 1ull << b[3].used;
    }
    bool ran = false;
    while (fast && p0 >= s0 && p1 >= s1 && p2 >= s2 && p3 >= s3 && o0 + 8 <= out + part && o3 + 8 <= end3) {
        ran = true;
        huf_reload<BMI2>(p0, v0);
        huf_reload<BMI2>(p1, v1);
        huf_reload<BMI2>(p2, v2);
        huf_reload<BMI2>(p3, v3);
        uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
        for (int k = 0; k < 5; k++) {
            size_t i0 = v0 >> (64 - huf_log), i1 = v1 >> (64 - huf_log);
            size_t i2 = v2 >> (64 - huf_log), i3 = v3 >> (64 - huf_log);
            w0 |= (uint64_t)syms[i0] << (8 * k);
            w1 |= (uint64_t)syms[i1] << (8 * k);
            w2 |= (uint64_t)syms[i2] << (8 * k);
            w3 |= (uint64_t)syms[i3] << (8 * k);
            v0 = shl<BMI2>(v0, lens[i0]);
            v1 = shl<BMI2>(v1, lens[i1]);
            v2 = shl<BMI2>(v2, lens[i2]);
            v3 = shl<BMI2>(v3, lens[i3]);
        }
        memcpy(o0, &w0, 8);
        memcpy(o1, &w1, 8);
        memcpy(o2, &w2, 8);
        memcpy(o3, &w3, 8);
        o0 += 5;
        o1 += 5;
        o2 += 5;
        o3 += 5;
    }

    if (ran) {
        b[0].p = p0, b[1].p = p1, b[2].p = p2, b[3].p = p3;
        b[0].c = le64(p0), b[1].c = le64(p1), b[2].c = le64(p2), b[3].c = le64(p3);
        b[0].used = (unsigned)std::countr_zero(v0), b[1].used = (unsigned)std::countr_zero(v1);
        b[2].used = (unsigned)std::countr_zero(v2), b[3].used = (unsigned)std::countr_zero(v3);
    }
    if (!huf_tail(b[0], t, o0, out + part))
        return false;
    if (!huf_tail(b[1], t, o1, out + 2 * part))
        return false;
    if (!huf_tail(b[2], t, o2, out + 3 * part))
        return false;
    return huf_tail(b[3], t, o3, end3);
}

static const uint32_t ll_base[36] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096,
    8192, 16384, 32768, 65536};
static const uint8_t ll_bits[36] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16};
static const uint32_t ml_base[53] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
    35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051,
    4099, 8195, 16387, 32771, 65539};
static const uint8_t ml_bits[53] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16};

static const int16_t ll_default[36] = {
    4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
    -1, -1, -1, -1};
static const int16_t ml_default[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1,
    -1, -1, -1, -1, -1};
static const int16_t of_default[29] = {
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

template <class B>
static void ensure(B& out, size_t need) {
    if (out.size() >= need)
        return;
    size_t grow = out.size() + out.size() / 2;
    out.resize(need > grow ? need : grow);
}

static const uint64_t xp1 = 0x9e3779b185ebca87ull, xp2 = 0xc2b2ae3d27d4eb4full, xp3 = 0x165667b19e3779f9ull,
                      xp4 = 0x85ebca77c2b2ae63ull, xp5 = 0x27d4eb2f165667c5ull;

static uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static uint64_t xround(uint64_t acc, uint64_t in) {
    acc += in * xp2;
    acc = rotl64(acc, 31);
    return acc * xp1;
}

static uint64_t xmerge(uint64_t acc, uint64_t v) {
    acc ^= xround(0, v);
    return acc * xp1 + xp4;
}

struct xxh_t {
    uint64_t v[4];
    uint64_t seed;
    size_t total;
};

static void xxh_begin(xxh_t& x, uint64_t seed) {
    x.v[0] = seed + xp1 + xp2;
    x.v[1] = seed + xp2;
    x.v[2] = seed;
    x.v[3] = seed - xp1;
    x.seed = seed;
    x.total = 0;
}

static void xxh_stripes(xxh_t& x, const uint8_t* p, size_t n) {
    uint64_t v1 = x.v[0], v2 = x.v[1], v3 = x.v[2], v4 = x.v[3];
    for (const uint8_t* end = p + n; p < end; p += 32) {
        v1 = xround(v1, le64(p));
        v2 = xround(v2, le64(p + 8));
        v3 = xround(v3, le64(p + 16));
        v4 = xround(v4, le64(p + 24));
    }
    x.v[0] = v1, x.v[1] = v2, x.v[2] = v3, x.v[3] = v4;
    x.total += n;
}

static uint64_t xxh_end(const xxh_t& x, const uint8_t* p, size_t n) {
    uint64_t h;
    if (x.total) {
        h = rotl64(x.v[0], 1) + rotl64(x.v[1], 7) + rotl64(x.v[2], 12) + rotl64(x.v[3], 18);
        for (int i = 0; i < 4; i++)
            h = xmerge(h, x.v[i]);
    } else {
        h = x.seed + xp5;
    }
    h += (uint64_t)(x.total + n);
    const uint8_t* end = p + n;
    while (p + 8 <= end) {
        h ^= xround(0, le64(p));
        h = rotl64(h, 27) * xp1 + xp4;
        p += 8;
    }
    if (p + 4 <= end) {
        h ^= (uint64_t)le32(p) * xp1;
        h = rotl64(h, 23) * xp2 + xp3;
        p += 4;
    }
    while (p < end) {
        h ^= (*p) * xp5;
        h = rotl64(h, 11) * xp1;
        p++;
    }
    h ^= h >> 33;
    h *= xp2;
    h ^= h >> 29;
    h *= xp3;
    h ^= h >> 32;
    return h;
}

template <class B>
static void xxh_follow(xxh_t* x, const B& out, size_t frame_start, size_t op) {
    if (!x)
        return;
    size_t upto = (op - frame_start) & ~(size_t)31;
    if (upto > x->total)
        xxh_stripes(*x, out.data() + frame_start + x->total, upto - x->total);
}

struct seq_t {
    uint32_t ll;
    uint32_t ml;
    uint32_t of;
};

struct job_t {
    int type = 0;
    const uint8_t* src = nullptr;
    size_t size = 0;
    bool last = false;
    int lit_type = 0;
    const uint8_t* lit_src = nullptr;
    size_t lit_csize = 0;
    size_t lit_len = 0;
    bool four = false;
    const huf_t* huf = nullptr;
    const fse_t* ll = nullptr;
    const fse_t* of = nullptr;
    const fse_t* ml = nullptr;
    const uint8_t* seq_src = nullptr;
    size_t seq_size = 0;
    size_t nseq = 0;
    huf_t own_huf;
    fse_t own_ll, own_of, own_ml;
    std::vector<uint8_t> lit;
    std::vector<seq_t> seqs;
    std::atomic<size_t> done = 0;
    bool ok = false;
    bool failed = false;
};

struct frame_state_t {
    fse_t ll, of, ml;
    huf_t huf;
    fse_t weights;
    std::vector<int16_t> norm;
    uint32_t rep[3] = {1, 4, 8};
    job_t job;
};

static bool scan_literals(const uint8_t* p, size_t n, frame_state_t& fs, job_t& j, size_t& used) {
    if (n < 1)
        return false;
    int type = p[0] & 3;
    int sf = (p[0] >> 2) & 3;
    j.lit_type = type;
    if (type <= 1) {
        size_t regen = 0, hdr = 0;
        if (sf == 0 || sf == 2) {
            regen = p[0] >> 3;
            hdr = 1;
        } else if (sf == 1) {
            if (n < 2)
                return false;
            regen = (p[0] >> 4) + ((size_t)p[1] << 4);
            hdr = 2;
        } else {
            if (n < 3)
                return false;
            regen = (p[0] >> 4) + ((size_t)p[1] << 4) + ((size_t)p[2] << 12);
            hdr = 3;
        }
        if (regen > max_block)
            return false;
        size_t body = type == 0 ? regen : 1;
        if (hdr + body > n)
            return false;
        j.lit_src = p + hdr;
        j.lit_csize = body;
        j.lit_len = regen;
        used = hdr + body;
        return true;
    }

    size_t hdr = 0, regen = 0, csize = 0;
    bool four = true;
    if (sf <= 1) {
        if (n < 3)
            return false;
        uint32_t h = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
        regen = (h >> 4) & 0x3ff;
        csize = (h >> 14) & 0x3ff;
        hdr = 3;
        four = sf == 1;
    } else if (sf == 2) {
        if (n < 4)
            return false;
        uint32_t h = le32(p);
        regen = (h >> 4) & 0x3fff;
        csize = (h >> 18) & 0x3fff;
        hdr = 4;
    } else {
        if (n < 5)
            return false;
        uint64_t h = le32(p) | ((uint64_t)p[4] << 32);
        regen = (size_t)((h >> 4) & 0x3ffff);
        csize = (size_t)((h >> 22) & 0x3ffff);
        hdr = 5;
    }
    if (regen > max_block || hdr + csize > n)
        return false;

    const uint8_t* q = p + hdr;
    size_t left = csize;
    if (type == 2) {
        size_t tree = 0;
        if (!read_huf(q, left, fs.huf, tree, fs.norm, fs.weights))
            return false;
        q += tree;
        left -= tree;
    } else if (!fs.huf.valid) {
        return false;
    }
    j.huf = &fs.huf;
    j.lit_src = q;
    j.lit_csize = left;
    j.lit_len = regen;
    j.four = four;
    used = hdr + csize;
    return true;
}

static void seq_table(fse_t& t, const uint32_t* base, const uint8_t* add) {
    t.seq.resize(t.cells.size());
    for (size_t i = 0; i < t.cells.size(); i++) {
        const fse_cell_t& c = t.cells[i];
        t.seq[i].base = base ? base[c.sym] : 1u << c.sym;
        t.seq[i].add = add ? add[c.sym] : (uint8_t)c.sym;
        t.seq[i].bits = c.bits;
        t.seq[i].next = c.base;
    }
}

static bool table_for(int mode, const uint8_t* p, size_t n, size_t& used, const int16_t* def,
                      size_t ndef, int def_log, int max_log, int max_sym, const uint32_t* base,
                      const uint8_t* add, fse_t& t, std::vector<int16_t>& norm) {
    used = 0;
    bool ok = false;
    switch (mode) {
    case 0:
        ok = build_fse(def, ndef, def_log, t);
        break;
    case 1:
        if (n < 1 || p[0] > max_sym)
            return false;
        rle_table(p[0], t);
        used = 1;
        ok = true;
        break;
    case 2: {
        int al = 0;
        if (!read_fse(p, n, max_log, max_sym, norm, al, used))
            return false;
        ok = build_fse(norm.data(), norm.size(), al, t);
        break;
    }
    default:
        return t.valid;
    }
    if (ok)
        seq_table(t, base, add);
    return ok;
}

static bool scan_block(const uint8_t* p, size_t n, frame_state_t& fs, job_t& j) {
    size_t lit_used = 0;
    if (!scan_literals(p, n, fs, j, lit_used))
        return false;
    const uint8_t* q = p + lit_used;
    size_t left = n - lit_used;

    if (left < 1)
        return false;
    size_t nseq = q[0];
    size_t hdr = 1;
    if (nseq >= 128) {
        if (nseq < 255) {
            if (left < 2)
                return false;
            nseq = ((nseq - 128) << 8) + q[1];
            hdr = 2;
        } else {
            if (left < 3)
                return false;
            nseq = q[1] + ((size_t)q[2] << 8) + 0x7f00;
            hdr = 3;
        }
    }
    q += hdr;
    left -= hdr;
    j.nseq = nseq;
    if (!nseq)
        return left == 0;

    if (left < 1)
        return false;
    uint8_t modes = q[0];
    if (modes & 3)
        return false;
    q++;
    left--;
    size_t u = 0;
    if (!table_for((modes >> 6) & 3, q, left, u, ll_default, 36, 6, 9, 35, ll_base, ll_bits, fs.ll, fs.norm))
        return false;
    q += u;
    left -= u;
    if (!table_for((modes >> 4) & 3, q, left, u, of_default, 29, 5, 8, 31, nullptr, nullptr, fs.of, fs.norm))
        return false;
    q += u;
    left -= u;
    if (!table_for((modes >> 2) & 3, q, left, u, ml_default, 53, 6, 9, 52, ml_base, ml_bits, fs.ml, fs.norm))
        return false;
    q += u;
    left -= u;
    j.ll = &fs.ll;
    j.of = &fs.of;
    j.ml = &fs.ml;
    j.seq_src = q;
    j.seq_size = left;
    return true;
}

static void own_tables(job_t& j) {
    if (j.lit_type >= 2) {
        j.own_huf = *j.huf;
        j.huf = &j.own_huf;
    }
    if (j.nseq) {
        j.own_ll.seq = j.ll->seq;
        j.own_ll.log = j.ll->log;
        j.own_of.seq = j.of->seq;
        j.own_of.log = j.of->log;
        j.own_ml.seq = j.ml->seq;
        j.own_ml.log = j.ml->log;
        j.ll = &j.own_ll;
        j.of = &j.own_of;
        j.ml = &j.own_ml;
    }
}

static bool decode_literals(job_t& j) {
    if (j.lit.size() < max_block + 32)
        j.lit.resize(max_block + 32);
    uint8_t* o = j.lit.data();
    if (j.lit_type == 0) {
        memcpy(o, j.lit_src, j.lit_len);
        return true;
    }
    if (j.lit_type == 1) {
        memset(o, j.lit_src[0], j.lit_len);
        return true;
    }
    const uint8_t* q = j.lit_src;
    size_t left = j.lit_csize;
    size_t regen = j.lit_len;
    if (!j.four)
        return huf_stream(q, left, *j.huf, o, regen);
    if (left < 6)
        return false;
    size_t s1 = q[0] | ((size_t)q[1] << 8);
    size_t s2 = q[2] | ((size_t)q[3] << 8);
    size_t s3 = q[4] | ((size_t)q[5] << 8);
    if (6 + s1 + s2 + s3 > left)
        return false;
    size_t s4 = left - 6 - s1 - s2 - s3;
    size_t part = (regen + 3) / 4;
    if (3 * part > regen)
        return false;
    const uint8_t* d = q + 6;
    const uint8_t* srcs[4] = {d, d + s1, d + s1 + s2, d + s1 + s2 + s3};
    size_t sizes[4] = {s1, s2, s3, s4};
    return g_bmi2 ? huf_streams4<true>(srcs, sizes, *j.huf, o, part, regen)
                  : huf_streams4<false>(srcs, sizes, *j.huf, o, part, regen);
}

template <bool BMI2>
static bool decode_seqs(job_t& j, rbits_t& b) {
    size_t nseq = j.nseq;
    uint32_t s_ll = (uint32_t)rb_read(b, j.ll->log);
    uint32_t s_of = (uint32_t)rb_read(b, j.of->log);
    uint32_t s_ml = (uint32_t)rb_read(b, j.ml->log);
    rb_reload(b);

    const seq_cell_t* ll_cells = j.ll->seq.data();
    const seq_cell_t* of_cells = j.of->seq.data();
    const seq_cell_t* ml_cells = j.ml->seq.data();
    if (j.seqs.size() < nseq)
        j.seqs.resize(nseq);
    seq_t* sq = j.seqs.data();
    for (size_t i = 0;; i++) {
        const seq_cell_t& cll = ll_cells[s_ll];
        const seq_cell_t& cof = of_cells[s_of];
        const seq_cell_t& cml = ml_cells[s_ml];
        sq[i].of = cof.base + (uint32_t)rb_take<BMI2>(b, cof.add);
        sq[i].ml = cml.base + (uint32_t)rb_take<BMI2>(b, cml.add);
        if (cof.add + cml.add + cll.add >= 31)
            rb_reload(b);
        sq[i].ll = cll.base + (uint32_t)rb_take<BMI2>(b, cll.add);
        if (i + 1 == nseq) {
            rb_reload(b);
            break;
        }
        s_ll = cll.next + (uint32_t)rb_take<BMI2>(b, cll.bits);
        s_ml = cml.next + (uint32_t)rb_take<BMI2>(b, cml.bits);
        s_of = cof.next + (uint32_t)rb_take<BMI2>(b, cof.bits);
        rb_reload(b);
    }
    return rb_done(b);
}

static bool decode_job(job_t& j) {
    if (!decode_literals(j))
        return false;
    if (!j.nseq)
        return true;
    rbits_t b;
    if (!rb_init(b, j.seq_src, j.seq_size))
        return false;
    return g_bmi2 ? decode_seqs<true>(j, b) : decode_seqs<false>(j, b);
}

static bool exec_job(const job_t& j, uint32_t* rep, uint8_t* base, size_t& op, size_t cap, size_t frame_start) {
    uint32_t r0 = rep[0], r1 = rep[1], r2 = rep[2];
    uint8_t* o = base + op;
    uint8_t* oend = base + cap;
    const uint8_t* ostart = base + frame_start;
    const uint8_t* lp = j.lit.data();
    const uint8_t* lend = lp + j.lit_len;
    const seq_t* sq = j.seqs.data();
    for (const seq_t* e = sq; e != sq + j.nseq; e++) {
        size_t ll = e->ll, ml = e->ml;
        uint32_t of_val = e->of;
        uint32_t offset;
        if (of_val > 3) {
            offset = of_val - 3;
            r2 = r1;
            r1 = r0;
            r0 = offset;
        } else {
            uint32_t idx = of_val - 1 + (ll == 0);
            if (idx == 0) {
                offset = r0;
            } else {
                offset = idx == 1 ? r1 : idx == 2 ? r2 : r0 - 1;
                if (idx != 1)
                    r2 = r1;
                r1 = r0;
                r0 = offset;
            }
        }

        if (ll > (size_t)(lend - lp) || ll + ml > (size_t)(oend - o))
            return false;
        memcpy(o, lp, 16);
        memcpy(o + 16, lp + 16, 16);
        for (size_t k = 32; k < ll; k += 16)
            memcpy(o + k, lp + k, 16);
        lp += ll;
        o += ll;
        if (!offset || offset > (size_t)(o - ostart))
            return false;
        const uint8_t* src = o - offset;
        if (offset >= 16) {
            memcpy(o, src, 16);
            for (size_t k = 16; k < ml; k += 16)
                memcpy(o + k, src + k, 16);
        } else if (offset >= 8) {
            for (size_t k = 0; k < ml; k += 8)
                memcpy(o + k, src + k, 8);
        } else {
            for (size_t k = 0; k < ml; k++)
                o[k] = src[k];
        }
        o += ml;
    }
    rep[0] = r0;
    rep[1] = r1;
    rep[2] = r2;
    size_t rest = (size_t)(lend - lp);
    if (rest > (size_t)(oend - o))
        return false;
    memcpy(o, lp, rest);
    op = (size_t)(o + rest - base);
    return true;
}

static const int MAX_THREADS = 3;
static std::atomic<int> g_threads = 1;

void set_threads(int n) {
    g_threads = std::clamp(n, 1, MAX_THREADS);
}

static const size_t SLOTS = 32;

static const uint64_t MADE_LAST = 1ull << 63;

struct pool_t {
    job_t slots[SLOTS];
    std::atomic<size_t> posted = 0;
    std::atomic<size_t> taken = 0;
    std::atomic<size_t> executed = 0;
    std::atomic<uint64_t> made = 0;
    std::atomic<bool> quit = false;
    std::string scan_err;
};

static void worker(pool_t* pl) {
    for (;;) {
        size_t id = pl->taken.fetch_add(1);
        for (;;) {
            size_t have = pl->posted.load();
            if (have > id || pl->quit)
                break;
            pl->posted.wait(have);
        }
        if (pl->quit)
            return;
        job_t& j = pl->slots[id % SLOTS];
        j.ok = !j.failed && (j.type != 2 || decode_job(j));
        j.done.store(id + 1);
        j.done.notify_all();
    }
}

static bool block_header(const uint8_t* in, size_t n, size_t& pos, job_t& j, std::string& err) {
    if (n - pos < 3) {
        err = "truncated block header";
        return false;
    }
    uint32_t bh = in[pos] | ((uint32_t)in[pos + 1] << 8) | ((uint32_t)in[pos + 2] << 16);
    pos += 3;
    j.last = bh & 1;
    j.type = (bh >> 1) & 3;
    j.size = bh >> 3;
    j.src = in + pos;
    if (j.type == 3) {
        err = "reserved block type";
        return false;
    }
    if (j.type == 1) {
        if (pos >= n || j.size > max_block) {
            err = "bad rle block";
            return false;
        }
        pos++;
        return true;
    }
    if (n - pos < j.size || j.size > max_block) {
        err = "truncated block";
        return false;
    }
    pos += j.size;
    return true;
}

static std::string bad_block(const uint8_t* in, const job_t& j) {
    return "bad compressed block at byte " + std::to_string(j.src - in);
}

template <class B>
static bool run_block(const uint8_t* in, const job_t& j, frame_state_t& fs, B& out, size_t& op, size_t frame_start,
                      bool fixed, std::string& err) {
    if (fixed && op + max_block + tail_pad > out.size()) {
        err = "frame bigger than its header says";
        return false;
    }
    ensure(out, op + max_block + tail_pad);
    if (j.type == 1) {
        memset(out.data() + op, j.src[0], j.size);
        op += j.size;
    } else if (j.type == 0) {
        memcpy(out.data() + op, j.src, j.size);
        op += j.size;
    } else if (!exec_job(j, fs.rep, out.data(), op, out.size() - tail_pad, frame_start)) {
        err = bad_block(in, j);
        return false;
    }
    return true;
}

template <class B>
static bool blocks_one(const uint8_t* in, size_t n, size_t& pos, frame_state_t& fs, B& out,
                       size_t& op, size_t frame_start, xxh_t* sum, std::atomic<size_t>* ready,
                       const std::atomic<bool>* stop, std::string& err) {
    job_t& j = fs.job;
    for (;;) {
        if (!block_header(in, n, pos, j, err))
            return false;
        if (j.type == 2 && (!scan_block(j.src, j.size, fs, j) || !decode_job(j))) {
            err = bad_block(in, j);
            return false;
        }
        if (!run_block(in, j, fs, out, op, frame_start, ready != nullptr, err))
            return false;
        xxh_follow(sum, out, frame_start, op);
        if (ready) {
            ready->store(op);
            ready->notify_all();
        }
        if (stop && *stop) {
            err = "stopped";
            return false;
        }
        if (j.last)
            return true;
    }
}

static void scanner(pool_t* pl, const uint8_t* in, size_t n, size_t* pos, frame_state_t* fs) {
    for (size_t scanned = 0;; scanned++) {
        for (;;) {
            size_t e = pl->executed.load();
            if (scanned - e < SLOTS || pl->quit)
                break;
            pl->executed.wait(e);
        }
        if (pl->quit)
            return;
        job_t& j = pl->slots[scanned % SLOTS];
        j.failed = !block_header(in, n, *pos, j, pl->scan_err);
        if (!j.failed && j.type == 2) {
            j.failed = !scan_block(j.src, j.size, *fs, j);
            if (j.failed)
                pl->scan_err = bad_block(in, j);
            else
                own_tables(j);
        }
        pl->posted.store(scanned + 1);
        pl->posted.notify_all();
        if (j.failed || j.last)
            return;
    }
}

static void toucher(pool_t* pl, uint8_t* base, size_t from, size_t to) {
    for (size_t at = (from + 4095) & ~(size_t)4095; at < to && !pl->quit; at += 4096)
        _InterlockedOr8((volatile char*)(base + at), 0);
}

static void hasher(pool_t* pl, xxh_t* sum, const uint8_t* base, size_t frame_start) {
    for (;;) {
        uint64_t made = pl->made.load();
        size_t upto = ((size_t)(made & ~MADE_LAST) - frame_start) & ~(size_t)31;
        if (upto > sum->total)
            xxh_stripes(*sum, base + frame_start + sum->total, upto - sum->total);
        if (made & MADE_LAST)
            return;
        pl->made.wait(made);
    }
}

template <class B>
static bool blocks_many(const uint8_t* in, size_t n, size_t& pos, frame_state_t& fs, B& out,
                        size_t& op, size_t frame_start, bool stable, xxh_t* sum, std::atomic<size_t>* ready,
                        const std::atomic<bool>* stop, std::string& err, int threads) {
    pool_t* pl = new pool_t();
    pl->made = op;
    std::vector<std::thread> crew;
    for (int i = 0; i < threads; i++)
        crew.emplace_back(worker, pl);
    std::thread scan(scanner, pl, in, n, &pos, &fs);
    std::thread hash, touch;
    if (sum && stable)
        hash = std::thread(hasher, pl, sum, out.data(), frame_start);
    if (stable)
        touch = std::thread(toucher, pl, out.data(), op, out.size());

    bool ok = true;
    for (size_t executed = 0;; executed++) {
        job_t& j = pl->slots[executed % SLOTS];
        for (;;) {
            size_t d = j.done.load();
            if (d == executed + 1)
                break;
            j.done.wait(d);
        }
        if (!j.ok) {
            err = j.failed ? pl->scan_err : bad_block(in, j);
            ok = false;
            break;
        }
        bool last = j.last;
        if (!run_block(in, j, fs, out, op, frame_start, ready != nullptr, err)) {
            ok = false;
            break;
        }
        pl->executed.store(executed + 1);
        pl->executed.notify_all();
        if (hash.joinable()) {
            pl->made.store(op);
            pl->made.notify_all();
        } else {
            xxh_follow(sum, out, frame_start, op);
        }
        if (ready) {
            ready->store(op);
            ready->notify_all();
        }
        if (stop && *stop) {
            err = "stopped";
            ok = false;
            break;
        }
        if (last)
            break;
    }

    pl->quit = true;
    pl->executed.store(SIZE_MAX);
    pl->executed.notify_all();
    scan.join();
    pl->posted.store(SIZE_MAX);
    pl->posted.notify_all();
    if (touch.joinable())
        touch.join();
    for (std::thread& t : crew)
        t.join();
    if (hash.joinable()) {
        pl->made.store(op | MADE_LAST);
        pl->made.notify_all();
        hash.join();
    }
    delete pl;
    return ok;
}

bool is_zstd(const uint8_t* p, size_t n) {
    return n >= 4 && le32(p) == frame_magic;
}

uint64_t xxh64(const uint8_t* p, size_t len, uint64_t seed) {
    xxh_t x;
    xxh_begin(x, seed);
    size_t body = len & ~(size_t)31;
    xxh_stripes(x, p, body);
    return xxh_end(x, p + body, len - body);
}

uint64_t content_size(const uint8_t* in, size_t n) {
    size_t pos = 0;
    uint64_t total = 0;
    while (pos < n) {
        if (n - pos < 5 || le32(in + pos) != frame_magic)
            return 0;
        pos += 4;
        uint8_t fhd = in[pos++];
        bool single = (fhd >> 5) & 1;
        int fcs_bytes = ((fhd >> 6) == 0 && single) ? 1 : fcs_bytes_of[fhd >> 6];
        if (!fcs_bytes)
            return 0;
        size_t head = (single ? 0 : 1) + dict_bytes[fhd & 3];
        if (n - pos < head + fcs_bytes)
            return 0;
        pos += head;
        uint64_t fcs = 0;
        for (int i = 0; i < fcs_bytes; i++)
            fcs |= (uint64_t)in[pos++] << (8 * i);
        if (fcs_bytes == 2)
            fcs += 256;
        total += fcs;
        for (;;) {
            if (n - pos < 3)
                return 0;
            uint32_t bh = in[pos] | ((uint32_t)in[pos + 1] << 8) | ((uint32_t)in[pos + 2] << 16);
            pos += 3;
            size_t size = ((bh >> 1) & 3) == 1 ? 1 : bh >> 3;
            if (n - pos < size)
                return 0;
            pos += size;
            if (bh & 1)
                break;
        }
        if ((fhd >> 2) & 1)
            pos += 4;
    }
    return total;
}

template <class B>
static bool unpack(const uint8_t* in, size_t n, B& out, size_t& len, std::string& err, std::atomic<size_t>* ready,
                   const std::atomic<bool>* stop) {
    size_t pos = 0, op = 0;
    int frames = 0;
    frame_state_t fs;
    while (pos < n) {
        if (n - pos < 4) {
            err = "truncated frame header";
            return false;
        }
        uint32_t magic = le32(in + pos);
        if (magic >= skip_min && magic <= skip_max) {
            if (n - pos < 8) {
                err = "truncated skippable frame";
                return false;
            }
            size_t skip = le32(in + pos + 4);
            if (n - pos - 8 < skip) {
                err = "truncated skippable frame";
                return false;
            }
            pos += 8 + skip;
            continue;
        }
        if (magic != frame_magic) {
            err = "not a zstd frame";
            return false;
        }
        pos += 4;
        if (pos >= n) {
            err = "truncated frame header";
            return false;
        }

        uint8_t fhd = in[pos++];
        int fcs_flag = fhd >> 6;
        bool single = (fhd >> 5) & 1;
        bool checksum = (fhd >> 2) & 1;
        int dict_flag = fhd & 3;
        if (fhd & 8) {
            err = "reserved bit set in frame header";
            return false;
        }
        int fcs_bytes = (fcs_flag == 0 && single) ? 1 : fcs_bytes_of[fcs_flag];
        size_t need = (single ? 0 : 1) + dict_bytes[dict_flag] + fcs_bytes;
        if (n - pos < need) {
            err = "truncated frame header";
            return false;
        }
        if (!single)
            pos++;
        uint32_t dict = 0;
        for (int i = 0; i < dict_bytes[dict_flag]; i++)
            dict |= (uint32_t)in[pos++] << (8 * i);
        if (dict) {
            err = "dictionary frames not supported";
            return false;
        }
        uint64_t fcs = 0;
        for (int i = 0; i < fcs_bytes; i++)
            fcs |= (uint64_t)in[pos++] << (8 * i);
        if (fcs_bytes == 2)
            fcs += 256;

        size_t frame_start = op;
        bool stable = fcs_bytes && fcs < (1ull << 34);
        if (stable)
            ensure(out, op + (size_t)fcs + max_block + tail_pad);

        fs.ll.valid = fs.of.valid = fs.ml.valid = false;
        fs.huf.valid = false;
        fs.rep[0] = 1;
        fs.rep[1] = 4;
        fs.rep[2] = 8;
        xxh_t sum;
        xxh_begin(sum, 0);
        xxh_t* follow = checksum ? &sum : nullptr;
        int threads = g_threads;
        bool ok = threads > 1 ? blocks_many(in, n, pos, fs, out, op, frame_start, stable, follow, ready, stop, err, threads)
                              : blocks_one(in, n, pos, fs, out, op, frame_start, follow, ready, stop, err);
        if (!ok)
            return false;

        size_t produced = op - frame_start;
        if (fcs_bytes && produced != fcs) {
            err = "frame size does not match header";
            return false;
        }
        if (checksum) {
            if (n - pos < 4) {
                err = "missing frame checksum";
                return false;
            }
            uint32_t want = le32(in + pos);
            uint32_t got = (uint32_t)xxh_end(sum, out.data() + frame_start + sum.total, produced - sum.total);
            if (got != want) {
                err = "frame checksum mismatch";
                return false;
            }
            pos += 4;
        }
        frames++;
    }
    if (!frames) {
        err = "no zstd frames";
        return false;
    }
    ensure(out, op + 16);
    memset(out.data() + op, 0, 16);
    len = op;
    return true;
}

bool decompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t& len, std::string& err, std::atomic<size_t>* ready,
                const std::atomic<bool>* stop) {
    return unpack(in, n, out, len, err, ready, stop);
}

bool decompress(const uint8_t* in, size_t n, buffer_t& out, size_t& len, std::string& err, std::atomic<size_t>* ready,
                const std::atomic<bool>* stop) {
    return unpack(in, n, out, len, err, ready, stop);
}

}
