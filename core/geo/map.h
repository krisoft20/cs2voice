#pragma once

struct fvec_t {
    float x = 0, y = 0, z = 0;
};

struct tri_t {
    fvec_t a, b, c;
    uint16_t attr = 0;
};

struct coll_attr_t {
    std::string group;
    std::vector<std::string> interact_as;
    bool blocks = true;
};

struct bvh_node_t {
    fvec_t lo, hi;
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t count = 0;
};

struct face_t {
    fvec_t a, e1, e2;
};

struct map_geo_t {
    std::string name;
    std::vector<tri_t> tris;
    std::vector<coll_attr_t> attrs;
    std::vector<bvh_node_t> nodes;
    std::vector<uint32_t> order;
    std::vector<face_t> faces;
    std::vector<uint32_t> parent;
    std::vector<uint32_t> leaf;
};

namespace geo {
    bool load(map_geo_t& g, const std::string& vpk_path, const std::string& map, std::string& err);
    bool load_map(map_geo_t& g, const std::string& map, std::string& err);
    bool blocked(const map_geo_t& g, const fvec_t& a, const fvec_t& b, float margin, uint32_t* hints = nullptr);
    bool body_visible(const map_geo_t& g, const fvec_t& eye, const fvec_t& feet, float eye_z, uint32_t* hints = nullptr);
}
