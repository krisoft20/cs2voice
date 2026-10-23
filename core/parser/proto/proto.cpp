#include "pch.h"

#include "parser/proto/proto.h"

namespace proto {

proto_t make(const uint8_t* p, size_t n) {
    proto_t r;
    r.p = p;
    r.end = p + n;
    return r;
}

bool varint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int shift = 0; shift < 70 && p < end; shift += 7) {
        uint8_t byte = *p++;
        v |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

bool next(proto_t& r, proto_field_t& f) {
    if (r.bad || r.p >= r.end)
        return false;
    uint64_t key;
    if (!varint(r.p, r.end, key)) {
        r.bad = true;
        return false;
    }
    f.num = (uint32_t)(key >> 3);
    f.wire = (uint32_t)(key & 7);
    f.val = 0;
    f.data = nullptr;
    f.len = 0;

    switch (f.wire) {
    case 0:
        if (!varint(r.p, r.end, f.val))
            r.bad = true;
        break;
    case 1:
        if (r.end - r.p < 8) {
            r.bad = true;
            break;
        }
        memcpy(&f.val, r.p, 8);
        r.p += 8;
        break;
    case 2: {
        uint64_t n;
        if (!varint(r.p, r.end, n) || n > (uint64_t)(r.end - r.p)) {
            r.bad = true;
            break;
        }
        f.data = r.p;
        f.len = (size_t)n;
        r.p += n;
        break;
    }
    case 5: {
        if (r.end - r.p < 4) {
            r.bad = true;
            break;
        }
        uint32_t u;
        memcpy(&u, r.p, 4);
        f.val = u;
        r.p += 4;
        break;
    }
    case 7:
        if (!r.deltas)
            r.bad = true;
        break;
    default:
        r.bad = true;
    }

    if (!f.num && !r.deltas)
        r.bad = true;
    return !r.bad;
}

float f32(const proto_field_t& f) {
    uint32_t u = (uint32_t)f.val;
    float v;
    memcpy(&v, &u, 4);
    return v;
}

int32_t i32(const proto_field_t& f) {
    return (int32_t)(uint32_t)f.val;
}

std::string str(const proto_field_t& f) {
    return std::string((const char*)f.data, f.len);
}

}
