#pragma once

namespace replays {
    void init(const std::wstring& dir);
    void cleanup();
    void list();
    void refresh();
    void save(const std::string& id, const std::vector<int>& keep, const std::vector<std::pair<double, double>>& cuts);
    void table(const std::string& id);
    void remove(const std::string& id, bool user);
    void pin(const std::string& id);
    void set_keep(int n);
    std::wstring video_dir(const std::string& folder);
}
