#pragma once

#include "demos/finder.h"
#include "demos/talk.h"

namespace launch {
    void set_talk(bool on, int style, int lead, int min, int refresh);
    bool init(const std::wstring& dir);
    void cleanup();
    void watch(const demo_file_t& f, uint32_t low, uint32_t high, bool fix, const std::vector<talk::talker_t>& who);
    void prepare_fix(const demo_file_t& f);
    std::vector<std::string> fixed_sources();
    void clear();
}
