#pragma once

#include "parser/entities/entities.h"
#include "parser/sendtables/layout.h"
#include "parser/sendtables/serializers.h"

typedef std::vector<std::pair<std::string, std::string>> field_list_t;

namespace inspect {
    void fields(const layouts_t& l, const serializers_t& s, const entity_t& e, field_list_t& out);
    std::string format(const decoder_t& d, const value_t& v, const entity_t& e);
    std::string field_name(const field_def_t& f);
    uint64_t hash(const field_list_t& f);
}
