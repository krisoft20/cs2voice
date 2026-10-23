#pragma once

struct proto_t {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool bad = false;
    bool deltas = false;
};

struct proto_field_t {
    uint32_t num = 0;
    uint32_t wire = 0;
    uint64_t val = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

namespace proto {
    proto_t make(const uint8_t* p, size_t n);
    bool next(proto_t& r, proto_field_t& f);
    bool varint(const uint8_t*& p, const uint8_t* end, uint64_t& v);

    float f32(const proto_field_t& f);
    int32_t i32(const proto_field_t& f);
    std::string str(const proto_field_t& f);
}
