#pragma once

#include "parser/core/bits.h"

struct fpath_t {
    int32_t p[7];
    int32_t last;
};

namespace field_path {
    void init();
    int read(bits_t& b, fpath_t* out, int cap, std::string& err);
    int code_length(int op);
}
