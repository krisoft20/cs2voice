#pragma once

namespace maps {
    bool init(const std::wstring& dir);
    void cleanup();
    void clear();
    void ensure(const std::string& map);
    void radar(const std::string& map);
}
