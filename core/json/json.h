#pragma once

enum json_kind_t { jk_null, jk_bool, jk_num, jk_str, jk_arr, jk_obj };

struct json_t {
    json_kind_t kind = jk_null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<json_t> arr;
    std::map<std::string, json_t> obj;
};

namespace json {
    bool parse(const std::string& text, json_t& out, std::string& err);
    const json_t* at(const json_t& v, const std::string& key);
    std::string str(const json_t* v, const std::string& def = "");
    double num(const json_t* v, double def = 0);
    std::string escape(const std::string& s);
}
