#pragma once

#include "demos/finder.h"

namespace voices {
    bool init();
    void cleanup();
    void want(const demo_file_t& f, bool send);
}
