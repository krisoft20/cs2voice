#include "pch.h"

#include "game/lz4.h"

namespace lz4 {
    bool decompress(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t& len) {
        size_t ip = 0, op = 0;
        while (ip < n) {
            uint8_t token = in[ip++];
            size_t lit = token >> 4;
            if (lit == 15) {
                uint8_t b;
                do {
                    if (ip >= n)
                        return false;
                    b = in[ip++];
                    lit += b;
                } while (b == 255);
            }
            if (ip + lit > n || op + lit > cap)
                return false;
            memcpy(out + op, in + ip, lit);
            ip += lit;
            op += lit;
            if (ip >= n)
                break;
            if (ip + 2 > n)
                return false;
            size_t offset = in[ip] | (in[ip + 1] << 8);
            ip += 2;
            if (offset == 0 || offset > op)
                return false;
            size_t match = (token & 15) + 4;
            if ((token & 15) == 15) {
                uint8_t b;
                do {
                    if (ip >= n)
                        return false;
                    b = in[ip++];
                    match += b;
                } while (b == 255);
            }
            if (op + match > cap)
                return false;
            for (size_t i = 0; i < match; i++, op++)
                out[op] = out[op - offset];
        }
        len = op;
        return true;
    }
}
