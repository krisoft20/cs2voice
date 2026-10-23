#pragma once

#include <atomic>

#include "parser/parser.h"

namespace track {
    struct state_t;
    state_t* begin(const std::atomic<bool>* cancel);
    void hook(state_t* s, callbacks_t& cb);
    bool finish(state_t* s, parser_t& p, bool ran, std::string& blob, std::string& err);
    bool extract(const std::string& demo, std::string& blob, std::string& err, const std::atomic<bool>* cancel = nullptr);
}
