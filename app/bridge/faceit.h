#pragma once

namespace faceit {
    void init(const std::wstring& cache_path);
    void want(const std::string& path, const std::string& name);
    void clear();
}
