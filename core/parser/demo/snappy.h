#pragma once

namespace snappy {
    bool decompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t& len);
}
