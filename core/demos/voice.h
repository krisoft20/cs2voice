#pragma once

#include <atomic>

namespace voice {
    bool extract(const std::string& demo, std::string& blob, std::string& err, const std::atomic<bool>* cancel = nullptr);
}
