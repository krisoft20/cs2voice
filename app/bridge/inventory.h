#pragma once

#include "demos/finder.h"

namespace inventory {
    bool init(const std::wstring& dir);
    void cleanup();
    void clear();
    void want(const demo_file_t& f, const std::string& lang);
    void icons(const std::vector<std::string>& list);
    void want_icons();
}
