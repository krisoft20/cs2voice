#pragma once

namespace history {
    bool init(const std::wstring& path);
    void note(const std::string& roster, int64_t mtime, bool quiet = false);
    std::string json();
}
