#pragma once

namespace profiles {
    bool init(const std::wstring& cache_path, const std::wstring& dir);
    void cleanup();
    void clear();
    void keep(const std::vector<std::string>& ids);
    void want(const std::vector<std::string>& ids, bool urgent);
}
