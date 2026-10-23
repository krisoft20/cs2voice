#pragma once

#include "parser/entities/entities.h"
#include "parser/sendtables/layout.h"
#include "parser/sendtables/serializers.h"

enum ref_kind_t : uint8_t {
    rk_value,
    rk_presence,
    rk_count,
};

struct field_ref_t {
    int32_t layout = -1;
    int32_t slot = -1;
    int32_t sub = -1;
    uint32_t stride = 0;
    uint16_t dec = 0;
    uint8_t kind = rk_value;
    uint8_t vtype = vt_u64;
};

struct field_key_t {
    std::string name;
    std::vector<field_ref_t> refs;
    std::vector<uint8_t> tried;
};

namespace access {
    field_key_t key(const char* name);
    bool find(const layouts_t& l, const serializers_t& s, int layout, const std::string& name, field_ref_t& out);
    const field_ref_t* ref(field_key_t& k, const layouts_t& l, const serializers_t& s, const entity_t& e);

    bool has(const entity_t& e, const field_ref_t& r, int elem = 0);
    uint32_t count(const entity_t& e, const field_ref_t& r);
    uint32_t stamp(const entity_t& e, const field_ref_t& r, int elem = 0);
    float get_float(const entity_t& e, const field_ref_t& r, int elem = 0);
    int64_t get_int(const entity_t& e, const field_ref_t& r, int elem = 0);
    uint64_t get_u64(const entity_t& e, const field_ref_t& r, int elem = 0);
    bool get_bool(const entity_t& e, const field_ref_t& r, int elem = 0);
    bool get_vec3(const entity_t& e, const field_ref_t& r, float* out, int elem = 0);
    const std::string* get_str(const entity_t& e, const field_ref_t& r, int elem = 0);
}
