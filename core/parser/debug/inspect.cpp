#include "pch.h"

#include "parser/debug/inspect.h"

namespace inspect {

static std::string fmt_bool(uint32_t w) {
    return w ? "true" : "false";
}

static std::string fmt_u64(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    return buf;
}

std::string format(const decoder_t& d, const value_t& v, const entity_t& e) {
    char buf[96];
    uint64_t u;
    memcpy(&u, v.w, 8);
    float f[3];
    memcpy(f, v.w, 12);
    switch (d.vtype) {
    case vt_bool:
        return fmt_bool(v.w[0]);
    case vt_i64:
        snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)u);
        return buf;
    case vt_u64:
        return fmt_u64(u);
    case vt_f32:
        snprintf(buf, sizeof(buf), "%.4f", (double)f[0]);
        return buf;
    case vt_vec3:
        snprintf(buf, sizeof(buf), "(%.4f, %.4f, %.4f)", (double)f[0], (double)f[1], (double)f[2]);
        return buf;
    case vt_str:
        if (v.w[0] && v.w[0] <= e.strs.size())
            return "\"" + e.strs[v.w[0] - 1] + "\"";
        return "\"\"";
    }
    return "-";
}

std::string field_name(const field_def_t& f) {
    if (f.send_node.empty())
        return f.name;
    return f.send_node + "." + f.name;
}

static void walk(const layouts_t& l, const serializers_t& s, const entity_t& e, const layout_t& lay,
                 const value_t* blk, size_t base, const std::string& prefix, field_list_t& out);

static void elements(const layouts_t& l, const serializers_t& s, const entity_t& e, const lfield_t& lf,
                     const value_t& head, const std::string& name, field_list_t& out) {
    if (!head.w[1] || head.w[1] > e.dyn.size())
        return;
    const std::vector<value_t>& dv = e.dyn[head.w[1] - 1];
    size_t n = dv.size() / lf.stride;
    for (size_t i = 0; i < n; i++) {
        const value_t& ev = dv[i * lf.stride];
        std::string en = name + "." + std::to_string(i);
        if (lf.elem_kind == lk_value) {
            if (ev.stamp)
                out.push_back({en, format(l.decoders[lf.dec], ev, e)});
            continue;
        }
        if (ev.stamp)
            out.push_back({en, lf.elem_count ? fmt_u64(ev.w[0]) : fmt_bool(ev.w[0])});
        walk(l, s, e, l.list[lf.child], dv.data(), i * lf.stride + 1, en + ".", out);
    }
}

static void walk(const layouts_t& l, const serializers_t& s, const entity_t& e, const layout_t& lay,
                 const value_t* blk, size_t base, const std::string& prefix, field_list_t& out) {
    for (const lfield_t& lf : lay.fields) {
        std::string name = prefix + field_name(s.fields[lf.def]);
        const value_t& v = blk[base + lf.slot];
        switch (lf.kind) {
        case lk_value:
            if (v.stamp)
                out.push_back({name, format(l.decoders[lf.dec], v, e)});
            break;
        case lk_struct:
            if (v.stamp)
                out.push_back({name, fmt_bool(v.w[0])});
            walk(l, s, e, l.list[lf.child], blk, base + lf.slot + 1, name + ".", out);
            break;
        case lk_array:
            if (v.stamp)
                out.push_back({name, fmt_bool(v.w[0])});
            elements(l, s, e, lf, v, name, out);
            break;
        case lk_vector:
            if (v.stamp)
                out.push_back({name, fmt_u64(v.w[0])});
            elements(l, s, e, lf, v, name, out);
            break;
        }
    }
}

void fields(const layouts_t& l, const serializers_t& s, const entity_t& e, field_list_t& out) {
    out.clear();
    if (e.layout < 0)
        return;
    walk(l, s, e, l.list[e.layout], e.slots.data(), 0, std::string(), out);
    std::sort(out.begin(), out.end());
}

uint64_t hash(const field_list_t& f) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    for (auto& kv : f) {
        mix(kv.first);
        mix("=");
        mix(kv.second);
        mix("\n");
    }
    return h;
}

}
