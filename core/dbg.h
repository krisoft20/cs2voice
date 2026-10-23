#pragma once

namespace dbg {
    bool init(const std::string& path);
    void cleanup();
    void print(const char* fmt, ...);
}
