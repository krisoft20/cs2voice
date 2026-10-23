#pragma once

enum kv_type_t : uint8_t {
    kv_null,
    kv_bool,
    kv_int,
    kv_uint,
    kv_double,
    kv_string,
    kv_blob,
    kv_array,
    kv_object,
};

struct kv_node_t {
    uint8_t type = kv_null;
    bool b = false;
    int64_t i = 0;
    uint64_t u = 0;
    double d = 0.0;
    std::string s;
    std::vector<uint8_t> blob;
    std::vector<kv_node_t> items;
    std::vector<std::string> keys;
};

namespace kv3 {
    bool read(const uint8_t* block, size_t n, kv_node_t& root, std::string& err);
    const kv_node_t* get(const kv_node_t& node, const char* key);
    int64_t integer(const kv_node_t& node);
}
