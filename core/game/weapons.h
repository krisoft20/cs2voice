#pragma once

namespace weapons {
    bool load(const std::string& csgo_dir, std::string& err);
    float max_speed(uint32_t def, bool alt);
}
