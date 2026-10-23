#include "pch.h"

#include "parser/proto/proto.h"
#include "parser/sendtables/serializers.h"

namespace serializers {

static bool parse_field(field_def_t& fd, const uint8_t* data, size_t n, const std::vector<std::string>& syms) {
    int64_t type = -1, name = -1, ser = -1, enc = -1, node = -1;
    proto_t r = proto::make(data, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        switch (f.num) {
        case 1: type = (int64_t)f.val; break;
        case 2: name = (int64_t)f.val; break;
        case 3: fd.bit_count = proto::i32(f); break;
        case 4: fd.low = proto::f32(f); fd.has_low = true; break;
        case 5: fd.high = proto::f32(f); fd.has_high = true; break;
        case 6: fd.flags = proto::i32(f); break;
        case 7: ser = (int64_t)f.val; break;
        case 8: fd.ser_version = proto::i32(f); break;
        case 9: node = (int64_t)f.val; break;
        case 10: enc = (int64_t)f.val; break;
        case 11: fd.poly++; break;
        }
    }
    if (r.bad)
        return false;
    auto sym = [&](int64_t i) { return i >= 0 && (size_t)i < syms.size() ? syms[i] : std::string(); };
    fd.type = sym(type);
    fd.name = sym(name);
    fd.ser_name = sym(ser);
    fd.encoder = sym(enc);
    fd.send_node = sym(node);
    return true;
}

bool parse(serializers_t& s, const uint8_t* data, size_t n, std::string& err) {
    const uint8_t* blob = nullptr;
    size_t blob_len = 0;
    proto_t r = proto::make(data, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 1 && f.wire == 2) {
            blob = f.data;
            blob_len = f.len;
            break;
        }
    }
    if (r.bad || !blob) {
        err = "send tables without data";
        return false;
    }

    const uint8_t* p = blob;
    uint64_t inner;
    if (!proto::varint(p, blob + blob_len, inner) || inner > (uint64_t)(blob + blob_len - p)) {
        err = "bad flattened serializer length";
        return false;
    }

    std::vector<std::string> syms;
    std::vector<proto_field_t> raw_sers, raw_fields;
    r = proto::make(p, (size_t)inner);
    while (proto::next(r, f)) {
        if (f.wire != 2)
            continue;
        if (f.num == 1)
            raw_sers.push_back(f);
        else if (f.num == 2)
            syms.push_back(proto::str(f));
        else if (f.num == 3)
            raw_fields.push_back(f);
    }
    if (r.bad || syms.empty()) {
        err = "bad flattened serializer";
        return false;
    }

    s.fields.resize(raw_fields.size());
    for (size_t i = 0; i < raw_fields.size(); i++) {
        if (!parse_field(s.fields[i], raw_fields[i].data, raw_fields[i].len, syms)) {
            err = "bad serializer field";
            return false;
        }
    }

    s.list.resize(raw_sers.size());
    for (size_t i = 0; i < raw_sers.size(); i++) {
        serializer_t& ser = s.list[i];
        int64_t name = -1;
        proto_t sr = proto::make(raw_sers[i].data, raw_sers[i].len);
        proto_field_t g;
        while (proto::next(sr, g)) {
            if (g.num == 1) {
                name = (int64_t)g.val;
            } else if (g.num == 2) {
                ser.version = proto::i32(g);
            } else if (g.num == 3 && g.wire == 0) {
                ser.fields.push_back(proto::i32(g));
            } else if (g.num == 3 && g.wire == 2) {
                const uint8_t* q = g.data;
                const uint8_t* end = g.data + g.len;
                uint64_t v;
                while (q < end && proto::varint(q, end, v))
                    ser.fields.push_back((int32_t)v);
            }
        }
        if (sr.bad) {
            err = "bad serializer";
            return false;
        }
        if (name >= 0 && (size_t)name < syms.size())
            ser.name = syms[name];
        for (int32_t idx : ser.fields) {
            if (idx < 0 || (size_t)idx >= s.fields.size()) {
                err = "serializer " + ser.name + " points at a missing field";
                return false;
            }
        }
    }
    if (s.list.empty()) {
        err = "no serializers";
        return false;
    }
    return true;
}

bool parse_classes(serializers_t& s, const uint8_t* data, size_t n, std::string& err) {
    s.classes.clear();
    proto_t r = proto::make(data, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num != 1 || f.wire != 2)
            continue;
        int32_t id = -1;
        std::string name;
        proto_t cr = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(cr, g)) {
            if (g.num == 1 && g.wire == 0)
                id = proto::i32(g);
            else if (g.num == 2 && g.wire == 2)
                name = proto::str(g);
        }
        if (id < 0 || id > 65535 || name.empty())
            continue;
        if ((size_t)id >= s.classes.size())
            s.classes.resize(id + 1);
        s.classes[id] = name;
    }
    if (r.bad || s.classes.empty()) {
        err = "bad class info";
        return false;
    }
    return true;
}

int find(const serializers_t& s, const std::string& name) {
    for (size_t i = 0; i < s.list.size(); i++)
        if (s.list[i].name == name)
            return (int)i;
    return -1;
}

int find_version(const serializers_t& s, const std::string& name, int32_t version) {
    int fallback = -1;
    for (size_t i = 0; i < s.list.size(); i++) {
        if (s.list[i].name != name)
            continue;
        if (s.list[i].version == version)
            return (int)i;
        if (fallback < 0)
            fallback = (int)i;
    }
    return fallback;
}

}
