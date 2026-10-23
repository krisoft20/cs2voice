#pragma once

struct field_def_t {
    std::string name;
    std::string type;
    std::string encoder;
    std::string ser_name;
    std::string send_node;
    int32_t ser_version = -1;
    int32_t bit_count = 0;
    float low = 0.f;
    float high = 0.f;
    bool has_low = false;
    bool has_high = false;
    int32_t flags = 0;
    int32_t poly = 0;
};

struct serializer_t {
    std::string name;
    int32_t version = 0;
    std::vector<int32_t> fields;
};

struct serializers_t {
    std::vector<field_def_t> fields;
    std::vector<serializer_t> list;
    std::vector<std::string> classes;
    int32_t max_classes = 0;
};

namespace serializers {
    bool parse(serializers_t& s, const uint8_t* data, size_t n, std::string& err);
    bool parse_classes(serializers_t& s, const uint8_t* data, size_t n, std::string& err);
    int find(const serializers_t& s, const std::string& name);
    int find_version(const serializers_t& s, const std::string& name, int32_t version);
}
