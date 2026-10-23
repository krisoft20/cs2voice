#include "pch.h"

#include "geo/kv3.h"
#include "geo/map.h"
#include "geo/vpk.h"

static fvec_t sub(const fvec_t& a, const fvec_t& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static fvec_t cross(const fvec_t& a, const fvec_t& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float dot(const fvec_t& a, const fvec_t& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float axis(const fvec_t& v, int k) {
    return k == 0 ? v.x : k == 1 ? v.y : v.z;
}

static bool read_vecs(const kv_node_t* n, std::vector<fvec_t>& out) {
    if (!n || n->type != kv_blob || n->blob.size() % 12)
        return false;
    out.resize(n->blob.size() / 12);
    memcpy(out.data(), n->blob.data(), n->blob.size());
    return true;
}

static bool add_hull(const kv_node_t& hull, uint16_t attr, std::vector<tri_t>& tris) {
    std::vector<fvec_t> v;
    bool explicit_idx = kv3::get(hull, "m_VertexPositions") != nullptr;
    if (!read_vecs(kv3::get(hull, explicit_idx ? "m_VertexPositions" : "m_Vertices"), v))
        return false;
    const kv_node_t* edges = kv3::get(hull, "m_Edges");
    const kv_node_t* faces = kv3::get(hull, "m_Faces");
    if (!edges || !faces || edges->type != kv_blob || faces->type != kv_blob || edges->blob.size() % 4)
        return false;
    const std::vector<uint8_t>& e = edges->blob;
    size_t ne = e.size() / 4;
    for (uint8_t start : faces->blob) {
        if (start >= ne)
            return false;
        size_t edge = e[(size_t)start * 4];
        for (int guard = 0; guard < 256; guard++) {
            if (edge >= ne)
                return false;
            size_t next = e[edge * 4];
            if (next == start)
                break;
            size_t ia = e[(size_t)start * 4 + 2], ib = e[edge * 4 + 2], ic = e[next * 4 + 2];
            if (ia >= v.size() || ib >= v.size() || ic >= v.size())
                return false;
            tris.push_back({v[ia], v[ib], v[ic], attr});
            edge = next;
        }
    }
    return true;
}

static bool add_mesh(const kv_node_t& mesh, uint16_t attr, std::vector<tri_t>& tris) {
    std::vector<fvec_t> v;
    if (!read_vecs(kv3::get(mesh, "m_Vertices"), v))
        return false;
    const kv_node_t* t = kv3::get(mesh, "m_Triangles");
    if (!t || t->type != kv_blob || t->blob.size() % 12)
        return false;
    size_t n = t->blob.size() / 12;
    for (size_t k = 0; k < n; k++) {
        int32_t idx[3];
        memcpy(idx, t->blob.data() + k * 12, 12);
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0)
            return false;
        if ((size_t)idx[0] >= v.size() || (size_t)idx[1] >= v.size() || (size_t)idx[2] >= v.size())
            return false;
        tris.push_back({v[idx[0]], v[idx[1]], v[idx[2]], attr});
    }
    return true;
}

static bool has(const std::vector<std::string>& v, const char* s) {
    for (const std::string& x : v)
        if (x == s)
            return true;
    return false;
}

static float hit_tri(const fvec_t& o, const fvec_t& d, const face_t& tr) {
    const fvec_t& e1 = tr.e1;
    const fvec_t& e2 = tr.e2;
    fvec_t p = cross(d, e2);
    float det = dot(e1, p);
    if (fabsf(det) < 1e-9f)
        return -1.f;
    float inv = 1.f / det;
    fvec_t s = sub(o, tr.a);
    float u = dot(s, p) * inv;
    if (u < 0.f || u > 1.f)
        return -1.f;
    fvec_t q = cross(s, e1);
    float w = dot(d, q) * inv;
    if (w < 0.f || u + w > 1.f)
        return -1.f;
    return dot(e2, q) * inv;
}

static bool hit_box(const fvec_t& o, const fvec_t& inv, const bvh_node_t& n, float t0, float t1, float& enter) {
    const fvec_t& lo = n.lo;
    const fvec_t& hi = n.hi;
    for (int k = 0; k < 3; k++) {
        float a = (axis(lo, k) - axis(o, k)) * axis(inv, k);
        float b = (axis(hi, k) - axis(o, k)) * axis(inv, k);
        if (a > b)
            std::swap(a, b);
        if (a > t0)
            t0 = a;
        if (b < t1)
            t1 = b;
        if (t0 > t1)
            return false;
    }
    enter = t0;
    return true;
}

static uint32_t build_node(map_geo_t& g, uint32_t first, uint32_t count, int depth) {
    bvh_node_t n;
    n.lo = {1e30f, 1e30f, 1e30f};
    n.hi = {-1e30f, -1e30f, -1e30f};
    for (uint32_t k = first; k < first + count; k++) {
        const tri_t& t = g.tris[g.order[k]];
        const fvec_t* vs[3] = {&t.a, &t.b, &t.c};
        for (const fvec_t* v : vs) {
            n.lo = {std::min(n.lo.x, v->x), std::min(n.lo.y, v->y), std::min(n.lo.z, v->z)};
            n.hi = {std::max(n.hi.x, v->x), std::max(n.hi.y, v->y), std::max(n.hi.z, v->z)};
        }
    }
    uint32_t index = (uint32_t)g.nodes.size();
    g.nodes.push_back(n);
    if (count <= 4 || depth > 40) {
        g.nodes[index].left = first;
        g.nodes[index].count = count;
        return index;
    }
    fvec_t ext = sub(n.hi, n.lo);
    int ax = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
    auto centre = [&](uint32_t ti) {
        const tri_t& t = g.tris[ti];
        return axis(t.a, ax) + axis(t.b, ax) + axis(t.c, ax);
    };
    uint32_t mid = first + count / 2;
    std::nth_element(g.order.begin() + first, g.order.begin() + mid, g.order.begin() + first + count,
                     [&](uint32_t x, uint32_t y) { return centre(x) < centre(y); });
    uint32_t l = build_node(g, first, mid - first, depth + 1);
    uint32_t r = build_node(g, mid, first + count - mid, depth + 1);
    g.nodes[index].left = l;
    g.nodes[index].right = r;
    g.nodes[index].count = 0;
    return index;
}

static void build(map_geo_t& g) {
    g.order.clear();
    for (uint32_t k = 0; k < g.tris.size(); k++)
        if (g.attrs.empty() || g.attrs[g.tris[k].attr].blocks)
            g.order.push_back(k);
    g.nodes.clear();
    g.nodes.reserve(g.order.size() / 2 + 1);
    if (!g.order.empty())
        build_node(g, 0, (uint32_t)g.order.size(), 0);
    g.faces.resize(g.order.size());
    for (size_t k = 0; k < g.order.size(); k++) {
        const tri_t& t = g.tris[g.order[k]];
        g.faces[k] = {t.a, sub(t.b, t.a), sub(t.c, t.a)};
    }
    g.parent.assign(g.nodes.size(), ~0u);
    g.leaf.assign(g.order.size(), ~0u);
    for (uint32_t i = 0; i < g.nodes.size(); i++) {
        const bvh_node_t& n = g.nodes[i];
        if (n.count) {
            for (uint32_t k = n.left; k < n.left + n.count; k++)
                g.leaf[k] = i;
        } else {
            g.parent[n.left] = i;
            g.parent[n.right] = i;
        }
    }
    std::vector<tri_t>().swap(g.tris);
    std::vector<uint32_t>().swap(g.order);
}

namespace geo {

bool load(map_geo_t& g, const std::string& vpk_path, const std::string& map, std::string& err) {
    std::vector<uint8_t> file;
    if (!vpk::extract(vpk_path, "maps/" + map + "/world_physics.vmdl_c", file, err))
        return false;
    const uint8_t* phys = nullptr;
    size_t phys_len = 0;
    if (!vpk::block(file, "PHYS", phys, phys_len)) {
        err = "no PHYS block in world_physics";
        return false;
    }
    kv_node_t root;
    if (!kv3::read(phys, phys_len, root, err))
        return false;

    g = map_geo_t{};
    g.name = map;
    if (const kv_node_t* ca = kv3::get(root, "m_collisionAttributes")) {
        for (const kv_node_t& a : ca->items) {
            coll_attr_t at;
            if (const kv_node_t* grp = kv3::get(a, "m_CollisionGroupString"))
                at.group = grp->s;
            if (const kv_node_t* as = kv3::get(a, "m_InteractAsStrings"))
                for (const kv_node_t& it : as->items)
                    at.interact_as.push_back(it.s);
            at.blocks = !(has(at.interact_as, "playerclip") || has(at.interact_as, "npcclip") ||
                          has(at.interact_as, "csgo_grenadeclip") || has(at.interact_as, "sky") ||
                          has(at.interact_as, "passbullets"));
            g.attrs.push_back(std::move(at));
        }
    }
    const kv_node_t* parts = kv3::get(root, "m_parts");
    if (!parts || parts->type != kv_array) {
        err = "no parts in map geometry";
        return false;
    }
    for (const kv_node_t& part : parts->items) {
        const kv_node_t* shape = kv3::get(part, "m_rnShape");
        if (!shape)
            continue;
        const char* kinds[2] = {"m_hulls", "m_meshes"};
        for (const char* kind : kinds) {
            const kv_node_t* arr = kv3::get(*shape, kind);
            if (!arr)
                continue;
            bool hull = !strcmp(kind, "m_hulls");
            for (const kv_node_t& desc : arr->items) {
                const kv_node_t* ai = kv3::get(desc, "m_nCollisionAttributeIndex");
                uint16_t attr = (uint16_t)(ai ? kv3::integer(*ai) : 0);
                if (!g.attrs.empty() && attr >= g.attrs.size()) {
                    err = "collision attribute out of range";
                    return false;
                }
                const kv_node_t* body = kv3::get(desc, hull ? "m_Hull" : "m_Mesh");
                if (!body)
                    continue;
                bool ok = hull ? add_hull(*body, attr, g.tris) : add_mesh(*body, attr, g.tris);
                if (!ok) {
                    err = std::string("broken ") + (hull ? "hull" : "mesh") + " in map geometry";
                    return false;
                }
            }
        }
    }
    build(g);
    return true;
}

bool load_map(map_geo_t& g, const std::string& map, std::string& err) {
    std::string dir = vpk::maps_dir();
    if (dir.empty()) {
        err = "cs2 maps dir not found";
        return false;
    }
    return load(g, dir + "/" + map + ".vpk", map, err);
}

bool blocked(const map_geo_t& g, const fvec_t& a, const fvec_t& b, float margin, uint32_t* hints) {
    if (g.nodes.empty())
        return false;
    fvec_t d = sub(b, a);
    float len = sqrtf(dot(d, d));
    if (len <= 2.f * margin)
        return false;
    float t0 = margin / len, t1 = 1.f - margin / len;
    fvec_t inv = {d.x != 0.f ? 1.f / d.x : 1e30f, d.y != 0.f ? 1.f / d.y : 1e30f, d.z != 0.f ? 1.f / d.z : 1e30f};
    float e;
    for (int h = 0; hints && h < 2 && hints[h] != ~0u; h++) {
        uint32_t k = hints[h];
        float t = hit_tri(a, d, g.faces[k]);
        if (t < t0 || t > t1)
            continue;
        uint32_t at = g.leaf[k];
        while (at != ~0u && hit_box(a, inv, g.nodes[at], t0, t1, e))
            at = g.parent[at];
        if (at != ~0u)
            continue;
        if (h) {
            hints[1] = hints[0];
            hints[0] = k;
        }
        return true;
    }
    if (!hit_box(a, inv, g.nodes[0], t0, t1, e))
        return false;
    uint32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const bvh_node_t& n = g.nodes[stack[--sp]];
        if (n.count) {
            for (uint32_t k = n.left; k < n.left + n.count; k++) {
                float t = hit_tri(a, d, g.faces[k]);
                if (t >= t0 && t <= t1) {
                    if (hints) {
                        hints[1] = hints[0];
                        hints[0] = k;
                    }
                    return true;
                }
            }
            continue;
        }
        if (sp + 2 > 128)
            return true;
        float el, er;
        bool hl = hit_box(a, inv, g.nodes[n.left], t0, t1, el);
        bool hr = hit_box(a, inv, g.nodes[n.right], t0, t1, er);
        if (hl && hr) {
            stack[sp++] = el <= er ? n.right : n.left;
            stack[sp++] = el <= er ? n.left : n.right;
        } else if (hl || hr) {
            stack[sp++] = hl ? n.left : n.right;
        }
    }
    return false;
}

bool body_visible(const map_geo_t& g, const fvec_t& eye, const fvec_t& feet, float eye_z, uint32_t* hints) {
    float sx = -(feet.y - eye.y), sy = feet.x - eye.x;
    float len = sqrtf(sx * sx + sy * sy);
    if (len > 1e-3f) {
        sx /= len;
        sy /= len;
    } else {
        sx = 1.f;
        sy = 0.f;
    }
    static const float pts[9][2] = {{2, 0}, {2, -5}, {2, 5}, {-10, -12}, {-10, 12}, {-18, 0}, {-28, 0}, {-28, -10}, {-28, 10}};
    for (const auto& p : pts) {
        fvec_t t = {feet.x + sx * p[1], feet.y + sy * p[1], feet.z + eye_z + p[0]};
        if (!blocked(g, eye, t, 2.f, hints))
            return true;
    }
    static const float legs[2] = {-6, 6};
    for (float side : legs) {
        fvec_t t = {feet.x + sx * side, feet.y + sy * side, feet.z + 18.f};
        if (!blocked(g, eye, t, 2.f, hints))
            return true;
    }
    return false;
}

}
