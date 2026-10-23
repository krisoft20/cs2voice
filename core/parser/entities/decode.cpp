#include "pch.h"

#include "parser/entities/decode.h"

namespace decode {

static float read_quant(bits_t& b, const quant_t& q) {
    if ((q.flags & qf_round_down) && bits::read_bit(b))
        return q.low;
    if ((q.flags & qf_round_up) && bits::read_bit(b))
        return q.high;
    if ((q.flags & qf_encode_zero) && bits::read_bit(b))
        return 0.f;
    uint32_t v = bits::read(b, q.bits);
    return q.low + (q.high - q.low) * (float)v * q.dec_mul;
}

float read_float(bits_t& b, const decoder_t& d) {
    switch (d.fkind) {
    case fk_coord:
        return bits::read_coord(b);
    case fk_simtime:
        return (float)bits::read_varint(b) * (1.f / 64.f);
    case fk_runetime: {
        uint32_t u = bits::read(b, 4);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    case fk_quant:
        return read_quant(b, d.q);
    }
    return bits::read_float(b);
}

static void put_f(value_t& v, int i, float f) {
    memcpy(&v.w[i], &f, 4);
}

static void put_u64(value_t& v, uint64_t x) {
    memcpy(v.w, &x, 8);
    v.w[2] = 0;
}

static void put_str(value_t& v, entity_t& e, const char* s, size_t n) {
    if (!v.w[0] || v.w[0] > e.strs.size()) {
        e.strs.emplace_back(s, n);
        v.w[0] = (uint32_t)e.strs.size();
    } else {
        e.strs[v.w[0] - 1].assign(s, n);
    }
    v.w[1] = v.w[2] = 0;
}

void value(bits_t& b, const decoder_t& d, value_t& v, entity_t& e) {
    switch (d.kind) {
    case dk_bool:
    case dk_presence:
        v.w[0] = bits::read_bit(b);
        v.w[1] = v.w[2] = 0;
        break;
    case dk_u32:
    case dk_count:
        put_u64(v, bits::read_varint(b));
        break;
    case dk_s32:
        put_u64(v, (uint64_t)(int64_t)bits::read_svarint(b));
        break;
    case dk_u64:
        put_u64(v, bits::read_varint64(b));
        break;
    case dk_s64:
        put_u64(v, (uint64_t)bits::read_svarint64(b));
        break;
    case dk_fixed64: {
        uint64_t lo = bits::read(b, 32);
        uint64_t hi = bits::read(b, 32);
        put_u64(v, lo | (hi << 32));
        break;
    }
    case dk_minus_one:
        put_u64(v, (uint64_t)((int64_t)bits::read_varint(b) - 1));
        break;
    case dk_bits8:
        put_u64(v, bits::read(b, 8));
        break;
    case dk_float:
        put_f(v, 0, read_float(b, d));
        v.w[1] = v.w[2] = 0;
        break;
    case dk_vec2: {
        float x = read_float(b, d);
        float y = read_float(b, d);
        put_f(v, 0, x);
        put_f(v, 1, y);
        v.w[2] = 0;
        break;
    }
    case dk_vec3:
    case dk_vec4: {
        float x = read_float(b, d);
        float y = read_float(b, d);
        float z = read_float(b, d);
        if (d.kind == dk_vec4)
            read_float(b, d);
        put_f(v, 0, x);
        put_f(v, 1, y);
        put_f(v, 2, z);
        break;
    }
    case dk_normal3: {
        bool hx = bits::read_bit(b), hy = bits::read_bit(b);
        float x = hx ? bits::read_normal(b) : 0.f;
        float y = hy ? bits::read_normal(b) : 0.f;
        bool neg_z = bits::read_bit(b);
        float sum = x * x + y * y;
        float z = sum < 1.f ? sqrtf(1.f - sum) : 0.f;
        put_f(v, 0, x);
        put_f(v, 1, y);
        put_f(v, 2, neg_z ? -z : z);
        break;
    }
    case dk_qangle_precise: {
        bool hx = bits::read_bit(b), hy = bits::read_bit(b), hz = bits::read_bit(b);
        put_f(v, 0, hx ? bits::read_angle(b, 20) - 180.f : 0.f);
        put_f(v, 1, hy ? bits::read_angle(b, 20) - 180.f : 0.f);
        put_f(v, 2, hz ? bits::read_angle(b, 20) - 180.f : 0.f);
        break;
    }
    case dk_qangle_py:
        if (d.bits == 0 || d.bits == 32) {
            put_f(v, 0, bits::read_float(b));
            put_f(v, 1, bits::read_float(b));
        } else {
            put_f(v, 0, bits::read_angle(b, d.bits));
            put_f(v, 1, bits::read_angle(b, d.bits));
        }
        v.w[2] = 0;
        break;
    case dk_qangle_bits:
        for (int i = 0; i < 3; i++)
            put_f(v, i, d.bits == 32 ? bits::read_float(b) : bits::read_angle(b, d.bits));
        break;
    case dk_qangle_coord: {
        bool hx = bits::read_bit(b), hy = bits::read_bit(b), hz = bits::read_bit(b);
        put_f(v, 0, hx ? bits::read_coord(b) : 0.f);
        put_f(v, 1, hy ? bits::read_coord(b) : 0.f);
        put_f(v, 2, hz ? bits::read_coord(b) : 0.f);
        break;
    }
    case dk_gametime:
        put_f(v, 0, bits::read_float(b));
        v.w[1] = v.w[2] = 0;
        break;
    case dk_string: {
        char buf[1024];
        size_t n = 0;
        while (n < sizeof(buf)) {
            uint32_t c = bits::read(b, 8);
            if (b.bad || !c)
                break;
            buf[n++] = (char)c;
        }
        put_str(v, e, buf, n);
        break;
    }
    case dk_blob: {
        uint32_t n = bits::read_varint(b);
        if (n > bits::left(b) / 8) {
            b.bad = true;
            break;
        }
        std::string s(n, '\0');
        bits::read_bytes(b, (uint8_t*)s.data(), n);
        put_str(v, e, s.data(), s.size());
        break;
    }
    }
}

}
