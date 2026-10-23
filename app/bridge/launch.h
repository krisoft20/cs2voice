#pragma once

#include "demos/finder.h"

namespace launch {
    bool init(const std::wstring& dir);
    void cleanup();
    void watch(const demo_file_t& f, uint32_t low, uint32_t high);
    void clear();
}
