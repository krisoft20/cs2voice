#include "pch.h"

#include "parser/demo/snappy.h"
#include "parser/proto/proto.h"

namespace snappy {

bool decompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t& len) {
    const uint8_t* p = in;
    const uint8_t* end = in + n;
    uint64_t total;
    if (!proto::varint(p, end, total) || total > (512u << 20))
        return false;
    if (out.size() < total + 16)
        out.resize(total + 16);

    uint8_t* base = out.data();
    uint8_t* op = base;
    uint8_t* oend = base + total;
    while (p < end) {
        uint8_t tag = *p++;
        if (!(tag & 3)) {
            size_t lit = tag >> 2;
            if (lit >= 60) {
                size_t extra = lit - 59;
                if ((size_t)(end - p) < extra)
                    return false;
                lit = 0;
                for (size_t i = 0; i < extra; i++)
                    lit |= (size_t)p[i] << (8 * i);
                p += extra;
            }
            lit++;
            if ((size_t)(end - p) < lit || (size_t)(oend - op) < lit)
                return false;
            memcpy(op, p, lit);
            op += lit;
            p += lit;
            continue;
        }

        size_t mlen, off;
        if ((tag & 3) == 1) {
            if (p >= end)
                return false;
            mlen = 4 + ((tag >> 2) & 7);
            off = ((size_t)(tag & 0xe0) << 3) | *p++;
        } else if ((tag & 3) == 2) {
            if (end - p < 2)
                return false;
            mlen = 1 + (tag >> 2);
            off = p[0] | ((size_t)p[1] << 8);
            p += 2;
        } else {
            if (end - p < 4)
                return false;
            mlen = 1 + (tag >> 2);
            off = p[0] | ((size_t)p[1] << 8) | ((size_t)p[2] << 16) | ((size_t)p[3] << 24);
            p += 4;
        }
        if (!off || off > (size_t)(op - base) || mlen > (size_t)(oend - op))
            return false;

        const uint8_t* src = op - off;
        if (off >= 8) {
            for (size_t i = 0; i < mlen; i += 8)
                memcpy(op + i, src + i, 8);
        } else {
            for (size_t i = 0; i < mlen; i++)
                op[i] = src[i];
        }
        op += mlen;
    }
    if (op != oend)
        return false;
    len = (size_t)total;
    return true;
}

}
