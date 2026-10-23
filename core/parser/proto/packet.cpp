#include "pch.h"

#include "parser/proto/packet.h"
#include "parser/proto/proto.h"

namespace packet {

bool begin(packet_t& p, const uint8_t* frame, size_t n) {
    p.b = bits::make(nullptr, 0);
    p.bad = false;
    proto_t r = proto::make(frame, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 3 && f.wire == 2) {
            p.b = bits::make(f.data, f.len);
            break;
        }
    }
    if (r.bad)
        p.bad = true;
    return !p.bad;
}

bool next(packet_t& p, msg_t& m) {
    if (p.bad || bits::left(p.b) < 8)
        return false;
    m.type = bits::read_ubitvar(p.b);
    uint32_t size = bits::read_varint(p.b);
    if (p.b.bad || size > p.b.len) {
        p.bad = true;
        return false;
    }
    if (p.scratch.size() < (size_t)size + 16)
        p.scratch.resize((size_t)size + 16);
    if (!bits::read_bytes(p.b, p.scratch.data(), size)) {
        p.bad = true;
        return false;
    }
    memset(p.scratch.data() + size, 0, 16);
    m.data = p.scratch.data();
    m.len = size;
    return true;
}

}
