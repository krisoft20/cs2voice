#pragma once

namespace lz4 {
    bool decompress(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t& len);
}
