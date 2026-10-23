#pragma once

struct econ_item_t {
    std::string weapon;
    std::string skin;
    std::string rarity;
    std::string color;
    std::string icon;
    std::string base_icon;
    std::string slot;
    std::vector<std::string> variants;
    int rank = 0;
    bool star = false;
};

namespace econ {
    bool load(const std::string& csgo_dir, const std::string& lang, std::string& err);
    bool item(int def, int paint, float wear, bool gloves, econ_item_t& out);
    bool sticker(uint32_t id, std::string& name, std::string& icon);
    std::string music(int id);
    std::string wear_name(float wear);
}
