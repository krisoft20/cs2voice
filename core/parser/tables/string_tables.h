#pragma once

struct table_entry_t {
    std::string key;
    std::vector<uint8_t> data;
};

struct string_table_t {
    std::string name;
    bool fixed_size = false;
    int32_t size_bits = 0;
    int32_t flags = 0;
    bool varint_bits = false;
    std::vector<table_entry_t> entries;
};

struct string_tables_t {
    std::vector<string_table_t> list;
    std::vector<int> changed;
};

namespace string_tables {
    bool create(string_tables_t& st, const uint8_t* msg, size_t n, int& index, std::string& err);
    bool update(string_tables_t& st, const uint8_t* msg, size_t n, int& index, std::string& err);
    bool load(string_tables_t& st, const uint8_t* msg, size_t n, std::string& err);
    void clear(string_tables_t& st);
    int find(const string_tables_t& st, const char* name);
}
