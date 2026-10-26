#pragma once

#include "demos/finder.h"
#include "demos/reader.h"
#include "demos/talk.h"

namespace jobs {
    bool init(const std::wstring& cache_path);
    void cleanup();
    void clear();
    void forget(const std::vector<std::string>& paths);
    void hold(bool on);
    bool idle();
    void want(const demo_file_t& f, bool urgent, bool quick = true);
    bool cached(const demo_file_t& f, std::string& json, bool& stale, bool* full = nullptr);
    bool talkers(const demo_file_t& f, std::vector<talk::talker_t>& out);
    std::string roster_json(const demo_file_t& f, const roster_t& r, bool ok);
}
