#pragma once

#include <atomic>

struct roster_t;

namespace talk {
    struct opts_t {
        int style = 0;
        int lead = 13;
        int min = 32;
        int refresh = 192;
    };

    struct talker_t {
        uint64_t steamid = 0;
        int slot = -1;
        std::string name;
        std::vector<std::pair<int, int>> sides;
    };

    std::vector<talker_t> talkers(const roster_t& r);
    bool annotate(const std::string& demo, const std::string& out, const std::vector<talker_t>& who, const opts_t& opts, std::string& err,
                  const std::atomic<bool>* cancel = nullptr);
    bool annotate(const std::string& demo, const std::string& out, const opts_t& opts, std::string& err,
                  const std::atomic<bool>* cancel = nullptr);
}