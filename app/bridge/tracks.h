#pragma once

#include "demos/finder.h"

namespace tracks {
    bool init(const std::wstring& dir);
    void cleanup();
    void clear();
    void forget(const std::vector<std::string>& paths);
    void want(const demo_file_t& f, bool send);
    bool stored(const demo_file_t& f);
    void expect(const demo_file_t& f);
    void deliver(const demo_file_t& f, const std::string& blob, bool ok, bool open);
}
