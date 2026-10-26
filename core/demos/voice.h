#pragma once

#include <atomic>

namespace voice {
    const uint32_t MAGIC = 0x56325343;
    const uint32_t VERSION = 1;

    struct spurt_t {
        uint64_t xuid = 0;
        int start = 0;
        int end = 0;
    };

    bool extract(const std::string& demo, std::string& blob, std::string& err, const std::atomic<bool>* cancel = nullptr);
    struct round_mark_t {
        int prestart = 0;
        int live = -1;
    };

    bool spurts(const std::string& demo, int gap, std::vector<spurt_t>& out, std::vector<round_mark_t>& rounds, std::string& err,
                const std::atomic<bool>* cancel = nullptr);
}