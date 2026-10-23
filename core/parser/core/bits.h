#pragma once

struct bits_t {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t bit = 0;
    bool bad = false;
};

namespace bits {
    bits_t make(const uint8_t* buf, size_t len);
    size_t left(const bits_t& b);

    uint32_t peek(const bits_t& b, int n);
    void skip(bits_t& b, int n);
    uint32_t read(bits_t& b, int n);
    bool read_bit(bits_t& b);
    uint32_t read_ubitvar(bits_t& b);
    uint32_t read_ubitvar_fp(bits_t& b);
    uint32_t read_varint(bits_t& b);
    uint64_t read_varint64(bits_t& b);
    int32_t read_svarint(bits_t& b);
    int64_t read_svarint64(bits_t& b);

    float read_float(bits_t& b);
    float read_coord(bits_t& b);
    float read_normal(bits_t& b);
    float read_angle(bits_t& b, int n);

    bool read_bytes(bits_t& b, uint8_t* dst, size_t n);
    void read_cstring(bits_t& b, std::string& out, size_t max);
}
