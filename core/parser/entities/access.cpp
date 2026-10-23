#include "pch.h"

#include "parser/debug/inspect.h"
#include "parser/entities/access.h"

namespace access {

static void fill(const layouts_t& l, const lfield_t& lf, field_ref_t& out) {
    switch (lf.kind) {
    case lk_value:
        out.kind = rk_value;
        out.dec = lf.dec;
        out.vtype = l.decoders[lf.dec].vtype;
        break;
    case lk_struct:
        out.kind = rk_presence;
        out.vtype = vt_bool;
        break;
    case lk_array:
    case lk_vector:
        out.kind = lf.kind == lk_vector ? rk_count : rk_presence;
        out.dec = lf.dec;
        if (lf.elem_kind == lk_value)
            out.vtype = l.decoders[lf.dec].vtype;
        else
            out.vtype = lf.elem_count ? vt_u64 : vt_bool;
        break;
    }
}

static bool resolve(const layouts_t& l, const serializers_t& s, const layout_t& lay, uint32_t base,
                    const std::string& name, bool in_elem, field_ref_t& out) {
    for (const lfield_t& lf : lay.fields) {
        std::string fn = inspect::field_name(s.fields[lf.def]);
        if (name == fn) {
            fill(l, lf, out);
            uint32_t at = base + lf.slot;
            if (in_elem) {
                out.sub = (int32_t)at;
                return true;
            }
            out.slot = (int32_t)at;
            if (lf.kind == lk_array || lf.kind == lk_vector) {
                out.sub = 0;
                out.stride = lf.stride;
            }
            return true;
        }
        if (name.size() <= fn.size() || name.compare(0, fn.size(), fn) || name[fn.size()] != '.')
            continue;
        std::string rest = name.substr(fn.size() + 1);
        if (lf.kind == lk_struct)
            return resolve(l, s, l.list[lf.child], base + lf.slot + 1, rest, in_elem, out);
        if ((lf.kind == lk_array || lf.kind == lk_vector) && !in_elem && lf.elem_kind == lk_struct) {
            out.slot = (int32_t)(base + lf.slot);
            out.stride = lf.stride;
            return resolve(l, s, l.list[lf.child], 1, rest, true, out);
        }
        return false;
    }
    return false;
}

field_key_t key(const char* name) {
    field_key_t k;
    k.name = name;
    return k;
}

bool find(const layouts_t& l, const serializers_t& s, int layout, const std::string& name, field_ref_t& out) {
    out = field_ref_t{};
    if (layout < 0 || (size_t)layout >= l.list.size())
        return false;
    if (!resolve(l, s, l.list[layout], 0, name, false, out)) {
        out = field_ref_t{};
        return false;
    }
    out.layout = layout;
    return true;
}

const field_ref_t* ref(field_key_t& k, const layouts_t& l, const serializers_t& s, const entity_t& e) {
    if (e.layout < 0)
        return nullptr;
    if (k.tried.size() < l.list.size()) {
        k.tried.resize(l.list.size(), 0);
        k.refs.resize(l.list.size());
    }
    if (!k.tried[e.layout])
        k.tried[e.layout] = find(l, s, e.layout, k.name, k.refs[e.layout]) ? 2 : 1;
    return k.tried[e.layout] == 2 ? &k.refs[e.layout] : nullptr;
}

static const value_t* at(const entity_t& e, const field_ref_t& r, int elem) {
    if (r.layout != e.layout || r.slot < 0 || (size_t)r.slot >= e.slots.size())
        return nullptr;
    const value_t& head = e.slots[r.slot];
    if (r.sub < 0)
        return &head;
    if (!head.w[1] || head.w[1] > e.dyn.size() || elem < 0)
        return nullptr;
    const std::vector<value_t>& dv = e.dyn[head.w[1] - 1];
    size_t i = (size_t)elem * r.stride + (size_t)r.sub;
    return i < dv.size() ? &dv[i] : nullptr;
}

bool has(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    return v && v->stamp;
}

uint32_t count(const entity_t& e, const field_ref_t& r) {
    if (r.layout != e.layout || r.slot < 0 || (size_t)r.slot >= e.slots.size())
        return 0;
    const value_t& head = e.slots[r.slot];
    if (r.kind == rk_count)
        return head.w[0];
    if (!head.w[1] || head.w[1] > e.dyn.size() || !r.stride)
        return 0;
    return (uint32_t)(e.dyn[head.w[1] - 1].size() / r.stride);
}

uint32_t stamp(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    return v ? v->stamp : 0;
}

float get_float(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    if (!v)
        return 0.f;
    float f;
    switch (r.vtype) {
    case vt_f32:
    case vt_vec3:
        memcpy(&f, &v->w[0], 4);
        return f;
    case vt_i64:
        return (float)get_int(e, r, elem);
    case vt_u64:
        return (float)get_u64(e, r, elem);
    case vt_bool:
        return v->w[0] ? 1.f : 0.f;
    }
    return 0.f;
}

int64_t get_int(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    if (!v)
        return 0;
    int64_t i;
    switch (r.vtype) {
    case vt_i64:
    case vt_u64:
        memcpy(&i, v->w, 8);
        return i;
    case vt_f32:
    case vt_vec3:
        return (int64_t)get_float(e, r, elem);
    case vt_bool:
        return v->w[0] != 0;
    }
    return 0;
}

uint64_t get_u64(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    if (!v)
        return 0;
    if (r.vtype == vt_f32 || r.vtype == vt_vec3)
        return (uint64_t)get_float(e, r, elem);
    if (r.vtype == vt_bool)
        return v->w[0] != 0;
    uint64_t u;
    memcpy(&u, v->w, 8);
    return u;
}

bool get_bool(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    return v && v->w[0];
}

bool get_vec3(const entity_t& e, const field_ref_t& r, float* out, int elem) {
    const value_t* v = at(e, r, elem);
    if (!v || (r.vtype != vt_vec3 && r.vtype != vt_f32))
        return false;
    memcpy(out, v->w, 12);
    if (r.vtype == vt_f32)
        out[1] = out[2] = 0.f;
    return true;
}

const std::string* get_str(const entity_t& e, const field_ref_t& r, int elem) {
    const value_t* v = at(e, r, elem);
    if (!v || r.vtype != vt_str || !v->w[0] || v->w[0] > e.strs.size())
        return nullptr;
    return &e.strs[v->w[0] - 1];
}

}
