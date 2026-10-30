#pragma once

namespace clips {
    void init(const std::wstring& data_dir);
    void cleanup();
    std::wstring dir();
    std::wstring meta_dir();
    bool inside(const std::wstring& path);
    std::wstring file_name(const std::string& name);
    void write_meta(const std::wstring& mp4, const std::string& match, const std::string& map, const std::string& tag, double from, double to);
    void list();
    void remove(const std::vector<std::wstring>& paths);
    void rename(const std::wstring& path, const std::string& name);
    void favourite(const std::wstring& path, bool on);
    void copy(const std::vector<std::wstring>& paths);
}
