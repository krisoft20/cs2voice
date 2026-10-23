#pragma once

#include "parser/sendtables/serializers.h"

enum lkind_t : uint8_t {
    lk_value,
    lk_struct,
    lk_array,
    lk_vector,
};

enum dkind_t : uint8_t {
    dk_bool,
    dk_u32,
    dk_s32,
    dk_u64,
    dk_s64,
    dk_fixed64,
    dk_minus_one,
    dk_bits8,
    dk_float,
    dk_vec2,
    dk_vec3,
    dk_vec4,
    dk_normal3,
    dk_qangle_precise,
    dk_qangle_py,
    dk_qangle_bits,
    dk_qangle_coord,
    dk_gametime,
    dk_string,
    dk_blob,
    dk_presence,
    dk_count,
};

enum fkind_t : uint8_t {
    fk_noscale,
    fk_coord,
    fk_simtime,
    fk_runetime,
    fk_quant,
};

enum vtype_t : uint8_t {
    vt_bool,
    vt_i64,
    vt_u64,
    vt_f32,
    vt_vec3,
    vt_str,
};

enum qflag_t : uint8_t {
    qf_round_down = 1,
    qf_round_up = 2,
    qf_encode_zero = 4,
    qf_encode_integers = 8,
};

struct quant_t {
    float low = 0.f;
    float high = 1.f;
    float dec_mul = 0.f;
    float high_low_mul = 0.f;
    int bits = 0;
    int flags = 0;
};

struct decoder_t {
    uint8_t kind = dk_u32;
    uint8_t fkind = fk_noscale;
    uint8_t vtype = vt_u64;
    uint8_t bits = 0;
    quant_t q;
};

struct lfield_t {
    uint8_t kind = lk_value;
    uint8_t elem_kind = lk_value;
    bool elem_count = false;
    uint16_t dec = 0;
    int32_t child = -1;
    uint32_t slot = 0;
    uint32_t stride = 1;
    int32_t poly = 0;
    int32_t def = -1;
};

struct layout_t {
    int32_t ser = -1;
    uint32_t size = 0;
    uint8_t state = 0;
    std::vector<lfield_t> fields;
};

struct layouts_t {
    std::vector<layout_t> list;
    std::vector<decoder_t> decoders;
    std::vector<int32_t> class_layout;
};

namespace layout {
    bool build(layouts_t& l, const serializers_t& s, std::string& err);
    quant_t quant_params(const field_def_t& f, bool& noscale);
    float quantize(const quant_t& q, float v);
}
