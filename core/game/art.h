#pragma once

struct overview_t {
    float pos_x = 0;
    float pos_y = 0;
    float scale = 0;
    float split_z = 0;
    bool lower = false;
};

namespace art {
    bool map_image(const std::string& csgo_dir, const std::string& map, const std::string& out_png, std::string& err);
    bool texture_png(const std::string& pak, const std::string& path, const std::string& out_png, std::string& err);
    bool radar_image(const std::string& csgo_dir, const std::string& name, const std::string& out_png, std::string& err);
    bool overview(const std::string& csgo_dir, const std::string& map, overview_t& out, std::string& err);
}
