#include "pch.h"

#include <windows.h>

#include "demos/finder.h"
#include "geo/vpk.h"
#include "text.h"

namespace vpk {

bool block(const std::vector<uint8_t>& file, const char* type, const uint8_t*& data, size_t& len) {
    if (file.size() < 16)
        return false;
    uint32_t block_offset = 0, block_count = 0;
    memcpy(&block_offset, file.data() + 8, 4);
    memcpy(&block_count, file.data() + 12, 4);
    for (uint32_t b = 0; b < block_count; b++) {
        size_t e = 8 + (size_t)block_offset + (size_t)b * 12;
        if (e + 12 > file.size())
            return false;
        uint32_t off = 0, n = 0;
        memcpy(&off, file.data() + e + 4, 4);
        memcpy(&n, file.data() + e + 8, 4);
        if (memcmp(file.data() + e, type, 4))
            continue;
        size_t start = e + 4 + off;
        if (start + n > file.size())
            return false;
        data = file.data() + start;
        len = n;
        return true;
    }
    return false;
}

std::string maps_dir() {
    std::string game = finder::game_dir();
    if (game.empty())
        return {};
    std::string d = game + "\\maps";
    if (GetFileAttributesW(text::wide(d + "\\de_dust2.vpk").c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};
    return d;
}

}
