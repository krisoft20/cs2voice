#pragma once

#include "demos/finder.h"

namespace perfs {
    bool init(const std::wstring& cache_path);
    void cleanup();
    void clear();
    void forget(const std::vector<std::string>& paths);
    void want(const demo_file_t& f, const std::string& map);
    void queue(const demo_file_t& f);
    void set_threads(int n, bool idle);
}
