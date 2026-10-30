#pragma once

struct demo_file_t {
    std::string path;
    std::string name;
    std::string folder;
    uint64_t size = 0;
    int64_t mtime = 0;
};

namespace finder {
    void default_folders(std::vector<std::string>& out);
    void scan(const std::vector<std::string>& folders, std::vector<demo_file_t>& out);
    bool is_demo(const std::string& name);
    uint64_t current_user();
    std::string steam_root();
    std::string game_dir();
}
