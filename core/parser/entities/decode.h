#pragma once

#include "parser/core/bits.h"
#include "parser/entities/entities.h"
#include "parser/sendtables/layout.h"

namespace decode {
    void value(bits_t& b, const decoder_t& d, value_t& v, entity_t& e);
    float read_float(bits_t& b, const decoder_t& d);
}
