#include "pch.h"

#include "parser/sendtables/layout.h"

namespace layout {

static bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

static bool is_var_container(const std::string& t) {
    return starts_with(t, "CNetworkUtlVectorBase<") || starts_with(t, "CUtlVector<") ||
           starts_with(t, "CUtlVectorEmbeddedNetworkVar<");
}

static bool is_array(const std::string& t) {
    return !t.empty() && t.back() == ']' && !starts_with(t, "char[");
}

static std::string base_type(const std::string& t) {
    size_t end = t.size();
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '<' || t[i] == '[' || t[i] == '*') {
            end = i;
            break;
        }
    }
    std::string b = t.substr(0, end);
    while (!b.empty() && b.back() == ' ')
        b.pop_back();
    return b;
}

static std::string inner_type(const std::string& t) {
    if (!t.empty() && t.back() == ']') {
        size_t br = t.rfind('[');
        return br == std::string::npos ? std::string() : t.substr(0, br);
    }
    size_t lt = t.find('<');
    size_t gt = t.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1)
        return std::string();
    std::string in = t.substr(lt + 1, gt - lt - 1);
    while (!in.empty() && in.front() == ' ')
        in.erase(in.begin());
    while (!in.empty() && in.back() == ' ')
        in.pop_back();
    return in;
}

float quantize(const quant_t& q, float v) {
    if (v < q.low)
        return q.low;
    if (v > q.high)
        return q.high;
    uint32_t i = (uint32_t)((v - q.low) * q.high_low_mul);
    return q.low + (q.high - q.low) * ((float)i * q.dec_mul);
}

static void assign_multipliers(quant_t& q) {
    float range = q.high - q.low;
    uint32_t hi = q.bits == 32 ? 0xfffffffeu : ((1u << q.bits) - 1u);
    float hi_f = (float)hi;
    q.high_low_mul = fabsf(range) <= 0.f ? hi_f : hi_f / range;

    auto too_big = [&] {
        return (q.high_low_mul * range > hi_f) || ((double)q.high_low_mul * (double)range > (double)hi_f);
    };
    if (too_big()) {
        static const float muls[] = {0.9999f, 0.99f, 0.9f, 0.8f, 0.7f};
        for (float m : muls) {
            q.high_low_mul = (hi_f / range) * m;
            if (!too_big())
                break;
        }
    }
    q.dec_mul = 1.f / hi_f;
}

quant_t quant_params(const field_def_t& f, bool& noscale) {
    quant_t q;
    noscale = false;
    if (f.bit_count == 0 || f.bit_count >= 32) {
        noscale = true;
        q.bits = 32;
        return q;
    }
    q.bits = f.bit_count;
    q.low = f.has_low ? f.low : 0.f;
    q.high = f.has_high ? f.high : 1.f;
    q.flags = f.flags;

    if (q.flags) {
        if ((q.low == 0.f && (q.flags & qf_round_down)) || (q.high == 0.f && (q.flags & qf_round_up)))
            q.flags &= ~qf_encode_zero;
        if (q.low == 0.f && (q.flags & qf_encode_zero)) {
            q.flags |= qf_round_down;
            q.flags &= ~qf_encode_zero;
        }
        if (q.high == 0.f && (q.flags & qf_encode_zero)) {
            q.flags |= qf_round_up;
            q.flags &= ~qf_encode_zero;
        }
        if (q.low > 0.f || q.high < 0.f)
            q.flags &= ~qf_encode_zero;
        if (q.flags & qf_encode_integers)
            q.flags &= ~(qf_round_down | qf_round_up | qf_encode_zero);
    }

    float steps = (float)(1u << q.bits);
    if (q.flags & qf_round_down)
        q.high -= (q.high - q.low) / steps;
    else if (q.flags & qf_round_up)
        q.low += (q.high - q.low) / steps;

    if (q.flags & qf_encode_integers) {
        float delta = q.high - q.low;
        if (delta < 1.f)
            delta = 1.f;
        int log2d = (int)ceil(log2((double)delta));
        uint32_t range2 = 1u << (log2d > 0 ? log2d : 0);
        int bc = q.bits;
        while ((1u << bc) < range2)
            bc++;
        if (bc > q.bits) {
            q.bits = bc;
            steps = (float)(1u << bc);
        }
        float offset = (float)range2 / steps;
        q.high = q.low + (float)range2 - offset;
    }

    assign_multipliers(q);

    if ((q.flags & qf_round_down) && quantize(q, q.low) == q.low)
        q.flags &= ~qf_round_down;
    if ((q.flags & qf_round_up) && quantize(q, q.high) == q.high)
        q.flags &= ~qf_round_up;
    if ((q.flags & qf_encode_zero) && quantize(q, 0.f) == 0.f)
        q.flags &= ~qf_encode_zero;
    return q;
}

static void float_kind(decoder_t& d, const field_def_t& f) {
    if (f.encoder == "coord") {
        d.fkind = fk_coord;
    } else if (f.encoder == "simtime") {
        d.fkind = fk_simtime;
    } else if (f.encoder == "runetime") {
        d.fkind = fk_runetime;
    } else {
        bool noscale;
        d.q = quant_params(f, noscale);
        d.fkind = noscale ? fk_noscale : fk_quant;
    }
}

static decoder_t pick(const field_def_t& f, const std::string& type) {
    decoder_t d;
    std::string base = base_type(type);

    if (f.name == "m_flSimulationTime" || f.name == "m_flAnimTime") {
        d.kind = dk_float;
        d.fkind = fk_simtime;
        d.vtype = vt_f32;
        return d;
    }
    if (f.name == "m_iClip1" || f.name == "m_iClip2") {
        d.kind = dk_minus_one;
        d.vtype = vt_i64;
        return d;
    }

    if (is_array(type)) {
        d.kind = dk_presence;
        d.vtype = vt_bool;
        return d;
    }
    if (is_var_container(type)) {
        d.kind = dk_count;
        d.vtype = vt_u64;
        return d;
    }

    if (base == "float32") {
        d.kind = dk_float;
        d.vtype = vt_f32;
        float_kind(d, f);
    } else if (base == "CNetworkedQuantizedFloat") {
        d.kind = dk_float;
        d.vtype = vt_f32;
        bool noscale;
        d.q = quant_params(f, noscale);
        d.fkind = noscale ? fk_noscale : fk_quant;
    } else if (base == "QAngle") {
        d.vtype = vt_vec3;
        d.bits = (uint8_t)f.bit_count;
        if (f.encoder == "qangle_precise")
            d.kind = dk_qangle_precise;
        else if (f.encoder == "qangle_pitch_yaw")
            d.kind = dk_qangle_py;
        else if (f.bit_count != 0)
            d.kind = dk_qangle_bits;
        else
            d.kind = dk_qangle_coord;
    } else if (base == "Vector" || base == "VectorWS") {
        d.vtype = vt_vec3;
        if (f.encoder == "normal") {
            d.kind = dk_normal3;
        } else {
            d.kind = dk_vec3;
            float_kind(d, f);
        }
    } else if (base == "Vector2D") {
        d.kind = dk_vec2;
        d.vtype = vt_vec3;
        float_kind(d, f);
    } else if (base == "Vector4D" || base == "Quaternion") {
        d.kind = dk_vec4;
        d.vtype = vt_vec3;
        float_kind(d, f);
    } else if (base == "uint64" || base == "CStrongHandle" || base == "CStrongHandleVoid") {
        d.kind = f.encoder == "fixed64" ? dk_fixed64 : dk_u64;
        d.vtype = vt_u64;
    } else if (base == "CHandle" || base == "CEntityHandle") {
        d.kind = dk_u32;
        d.vtype = vt_u64;
    } else if (base == "bool") {
        d.kind = dk_bool;
        d.vtype = vt_bool;
    } else if (base == "char" || base == "CUtlString" || base == "CUtlSymbolLarge" || base == "CGlobalSymbol") {
        d.kind = dk_string;
        d.vtype = vt_str;
    } else if (base == "int8" || base == "int16" || base == "int32") {
        d.kind = dk_s32;
        d.vtype = vt_i64;
    } else if (base == "int64") {
        d.kind = dk_s64;
        d.vtype = vt_i64;
    } else if (base == "uint8" || base == "uint16" || base == "uint32" || base == "color32" || base == "Color" ||
               base == "CGameSceneNodeHandle" || base == "CUtlStringToken") {
        d.kind = dk_u32;
        d.vtype = vt_u64;
    } else if (base == "GameTime_t") {
        d.kind = dk_gametime;
        d.vtype = vt_f32;
    } else if (base == "ResourceId_t") {
        d.kind = dk_u64;
        d.vtype = vt_u64;
    } else if (base == "HSequence") {
        d.kind = dk_minus_one;
        d.vtype = vt_i64;
    } else if (base == "BloodType") {
        d.kind = dk_bits8;
        d.vtype = vt_u64;
    } else if (base == "CBodyComponent" || base == "CPhysicsComponent" || base == "CRenderComponent") {
        d.kind = dk_bool;
        d.vtype = vt_bool;
    } else if (base == "CUtlBinaryBlock") {
        d.kind = dk_blob;
        d.vtype = vt_str;
    } else {
        d.kind = dk_u32;
        d.vtype = vt_u64;
    }
    return d;
}

static uint16_t add_decoder(layouts_t& l, const field_def_t& f, const std::string& type) {
    l.decoders.push_back(pick(f, type));
    return (uint16_t)(l.decoders.size() - 1);
}

static bool build_one(layouts_t& l, const serializers_t& s, int si, int depth, std::string& err) {
    if (l.list[si].state == 2)
        return true;
    if (l.list[si].state == 1 || depth > 32) {
        err = "serializer recursion at " + s.list[si].name;
        return false;
    }
    l.list[si].state = 1;
    l.list[si].ser = si;

    std::vector<lfield_t> fields;
    uint32_t slot = 0;
    for (int32_t fi : s.list[si].fields) {
        const field_def_t& fd = s.fields[fi];
        lfield_t lf;
        lf.def = fi;
        lf.poly = fd.poly;
        lf.slot = slot;
        bool var = is_var_container(fd.type);

        if (!fd.ser_name.empty()) {
            int child = serializers::find_version(s, fd.ser_name, fd.ser_version);
            if (child < 0) {
                err = "missing serializer " + fd.ser_name;
                return false;
            }
            if (!build_one(l, s, child, depth + 1, err))
                return false;
            lf.child = child;
            if (var) {
                lf.kind = lk_vector;
                lf.elem_kind = lk_struct;
                lf.stride = 1 + l.list[child].size;
                slot += 1;
            } else {
                lf.kind = lk_struct;
                slot += 1 + l.list[child].size;
            }
        } else if (is_array(fd.type) || var) {
            lf.kind = var ? lk_vector : lk_array;
            std::string inner = inner_type(fd.type);
            int child = inner.empty() ? -1 : serializers::find(s, inner);
            if (child >= 0) {
                if (!build_one(l, s, child, depth + 1, err))
                    return false;
                lf.elem_kind = lk_struct;
                lf.elem_count = var;
                lf.child = child;
                lf.stride = 1 + l.list[child].size;
            } else {
                lf.elem_kind = lk_value;
                lf.dec = add_decoder(l, fd, inner);
            }
            slot += 1;
        } else {
            lf.kind = lk_value;
            lf.dec = add_decoder(l, fd, fd.type);
            slot += 1;
        }
        fields.push_back(lf);
    }

    l.list[si].fields.swap(fields);
    l.list[si].size = slot;
    l.list[si].state = 2;
    return true;
}

bool build(layouts_t& l, const serializers_t& s, std::string& err) {
    l.list.clear();
    l.decoders.clear();
    l.list.resize(s.list.size());
    for (size_t i = 0; i < s.list.size(); i++)
        if (!build_one(l, s, (int)i, 0, err))
            return false;

    l.class_layout.assign(s.classes.size(), -1);
    for (size_t c = 0; c < s.classes.size(); c++)
        if (!s.classes[c].empty())
            l.class_layout[c] = serializers::find(s, s.classes[c]);
    if (l.decoders.size() > 65535) {
        err = "too many decoders";
        return false;
    }
    return true;
}

}
