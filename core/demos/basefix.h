#pragma once

#include "parser/parser.h"

struct late_t {
    std::set<std::string> signon;
    std::set<std::string> late;
    std::set<int> first;
    int first_tick = -1;
    bool first_done = false;
};

namespace basefix {
    void watch_msg(late_t& l, parser_t& p, const msg_t& m);
    void watch_frame(late_t& l, parser_t& p);
    bool crashes(const late_t& l);
    bool fix(const std::string& in, const std::string& out, std::string& err);
    bool unmute(const uint8_t* dem, size_t len, FILE* out);
}
