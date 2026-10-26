#include "pch.h"

#include "json/json.h"

struct reader_t {
    const char* p;
    const char* end;
    int depth = 0;
};

static void ws(reader_t& r) {
    while (r.p < r.end && (*r.p == ' ' || *r.p == '\t' || *r.p == '\r' || *r.p == '\n'))
        r.p++;
}

static void utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xc0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += (char)(0xe0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    } else {
        out += (char)(0xf0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3f));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    }
}

static bool hex4(reader_t& r, uint32_t& v) {
    if (r.end - r.p < 4)
        return false;
    v = 0;
    for (int i = 0; i < 4; i++) {
        char c = *r.p++;
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= c - '0';
        else if (c >= 'a' && c <= 'f')
            v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v |= c - 'A' + 10;
        else
            return false;
    }
    return true;
}

static bool string(reader_t& r, std::string& out) {
    if (r.p >= r.end || *r.p != '"')
        return false;
    r.p++;
    while (r.p < r.end && *r.p != '"') {
        char c = *r.p++;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (r.p >= r.end)
            return false;
        char e = *r.p++;
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            uint32_t cp;
            if (!hex4(r, cp))
                return false;
            if (cp >= 0xd800 && cp < 0xdc00 && r.end - r.p >= 6 && r.p[0] == '\\' && r.p[1] == 'u') {
                r.p += 2;
                uint32_t lo;
                if (!hex4(r, lo))
                    return false;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            }
            utf8(out, cp);
            break;
        }
        default: out += e; break;
        }
    }
    if (r.p >= r.end)
        return false;
    r.p++;
    return true;
}

static bool value(reader_t& r, json_t& v) {
    ws(r);
    if (r.p >= r.end || r.depth > 256)
        return false;
    char c = *r.p;
    if (c == '{') {
        v.kind = jk_obj;
        r.p++;
        r.depth++;
        ws(r);
        if (r.p < r.end && *r.p == '}') {
            r.p++;
            r.depth--;
            return true;
        }
        for (;;) {
            ws(r);
            std::string key;
            if (!string(r, key))
                return false;
            ws(r);
            if (r.p >= r.end || *r.p++ != ':')
                return false;
            if (!value(r, v.obj.emplace_back(key, json_t{}).second))
                return false;
            ws(r);
            if (r.p < r.end && *r.p == ',') {
                r.p++;
                continue;
            }
            if (r.p < r.end && *r.p == '}') {
                r.p++;
                r.depth--;
                return true;
            }
            return false;
        }
    }
    if (c == '[') {
        v.kind = jk_arr;
        r.p++;
        r.depth++;
        ws(r);
        if (r.p < r.end && *r.p == ']') {
            r.p++;
            r.depth--;
            return true;
        }
        for (;;) {
            v.arr.emplace_back();
            if (!value(r, v.arr.back()))
                return false;
            ws(r);
            if (r.p < r.end && *r.p == ',') {
                r.p++;
                continue;
            }
            if (r.p < r.end && *r.p == ']') {
                r.p++;
                r.depth--;
                return true;
            }
            return false;
        }
    }
    if (c == '"') {
        v.kind = jk_str;
        return string(r, v.str);
    }
    for (const char* word : {"true", "false", "null"}) {
        size_t n = strlen(word);
        if ((size_t)(r.end - r.p) >= n && !memcmp(r.p, word, n)) {
            r.p += n;
            v.kind = word[0] == 'n' ? jk_null : jk_bool;
            v.b = word[0] == 't';
            return true;
        }
    }
    std::string tmp(r.p, std::min<size_t>(64, r.end - r.p));
    char* stop = nullptr;
    v.num = strtod(tmp.c_str(), &stop);
    if (stop == tmp.c_str())
        return false;
    v.kind = jk_num;
    r.p += stop - tmp.c_str();
    return true;
}

namespace json {

bool parse(const std::string& text, json_t& out, std::string& err) {
    reader_t r = {text.data(), text.data() + text.size()};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf)
        r.p += 3;
    out = json_t{};
    if (!value(r, out)) {
        err = "bad json at byte " + std::to_string(r.p - text.data());
        return false;
    }
    ws(r);
    if (r.p != r.end) {
        err = "trailing bytes at " + std::to_string(r.p - text.data());
        return false;
    }
    return true;
}

const json_t* at(const json_t& v, std::string_view key) {
    if (v.kind != jk_obj)
        return nullptr;
    for (const std::pair<std::string, json_t>& kv : v.obj)
        if (kv.first == key)
            return &kv.second;
    return nullptr;
}

std::string_view view(const json_t* v, std::string_view def) {
    return v && v->kind == jk_str ? std::string_view(v->str) : def;
}

std::string str(const json_t* v, std::string_view def) {
    return v && v->kind == jk_str ? v->str : std::string(def);
}

double num(const json_t* v, double def) {
    return v && v->kind == jk_num ? v->num : def;
}

std::string escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

}