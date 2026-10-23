#pragma once

#include "game/vpk.h"

namespace vpk {
    bool block(const std::vector<uint8_t>& file, const char* type, const uint8_t*& data, size_t& len);
    std::string maps_dir();
}
