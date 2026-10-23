#include "pch.h"

#include "parser/entities/decode.h"
#include "parser/entities/entities.h"
#include "parser/proto/proto.h"

static const int max_entities = 16384;
static const int max_elems = 1 << 16;

namespace entities {

void init(entities_t& es, const serializers_t& s, const layouts_t& l, int cls_bits) {
    es.ser = &s;
    es.lay = &l;
    es.list.assign(max_entities + 1, entity_t{});
    es.baselines.assign(s.classes.size(), entity_t{});
    es.baseline_state.assign(s.classes.size(), 0);
    es.baseline_data.assign(s.classes.size(), {});
    es.paths.resize(4096);
    es.touched.clear();
    es.cls_bits = cls_bits;
    es.stamp = 2;
    es.stats = ent_stats_t{};
    es.err.clear();
}

void set_baseline(entities_t& es, int cls, const uint8_t* data, size_t n) {
    if (cls < 0 || (size_t)cls >= es.baseline_data.size())
        return;
    es.baseline_data[cls].assign(data, data + n);
    es.baseline_data[cls].resize(n + 16);
    es.baseline_state[cls] = 0;
}

static value_t* block(entity_t& e, int blk) {
    return blk < 0 ? e.slots.data() : e.dyn[blk].data();
}

static int dyn_of(entity_t& e, int blk, size_t at) {
    uint32_t d = block(e, blk)[at].w[1];
    if (!d) {
        e.dyn.emplace_back();
        d = (uint32_t)e.dyn.size();
        block(e, blk)[at].w[1] = d;
    }
    return (int)d - 1;
}

static bool apply(entities_t& es, bits_t& b, entity_t& e, const fpath_t& fp) {
    const layouts_t& l = *es.lay;
    const layout_t* lay = &l.list[e.layout];
    int blk = -1;
    size_t base = 0;

    for (int lvl = 0; lvl <= fp.last; lvl++) {
        int32_t idx = fp.p[lvl];
        if (idx < 0 || (size_t)idx >= lay->fields.size()) {
            es.err = "field index out of range";
            return false;
        }
        const lfield_t& lf = lay->fields[idx];
        size_t at = base + lf.slot;

        if (lvl == fp.last) {
            value_t& v = block(e, blk)[at];
            switch (lf.kind) {
            case lk_value:
                decode::value(b, l.decoders[lf.dec], v, e);
                break;
            case lk_struct: {
                bool present = bits::read_bit(b);
                v.w[0] = present;
                v.w[1] = present && lf.poly ? bits::read_ubitvar(b) : 0;
                break;
            }
            case lk_array:
                v.w[0] = bits::read_bit(b);
                break;
            case lk_vector: {
                uint32_t count = bits::read_varint(b);
                if (count > (uint32_t)max_elems) {
                    es.err = "vector too long";
                    return false;
                }
                v.w[0] = count;
                int d = dyn_of(e, blk, at);
                e.dyn[d].resize((size_t)count * lf.stride, value_t{});
                break;
            }
            }
            block(e, blk)[at].stamp = es.stamp;
            return !b.bad;
        }

        if (lf.kind == lk_value) {
            es.err = "field path goes below a value";
            return false;
        }
        if (lf.kind == lk_struct) {
            base = at + 1;
            lay = &l.list[lf.child];
            continue;
        }

        lvl++;
        int32_t ei = fp.p[lvl];
        if (ei < 0 || ei >= max_elems) {
            es.err = "element index out of range";
            return false;
        }
        int d = dyn_of(e, blk, at);
        std::vector<value_t>& dv = e.dyn[d];
        size_t eat = (size_t)ei * lf.stride;
        if (dv.size() < eat + lf.stride)
            dv.resize(eat + lf.stride, value_t{});

        if (lf.elem_kind == lk_value) {
            if (lvl != fp.last) {
                es.err = "field path goes below an element";
                return false;
            }
            value_t& v = dv[eat];
            decode::value(b, l.decoders[lf.dec], v, e);
            v.stamp = es.stamp;
            return !b.bad;
        }
        if (lvl == fp.last) {
            value_t& v = dv[eat];
            if (lf.elem_count) {
                v.w[0] = bits::read_varint(b);
            } else {
                bool present = bits::read_bit(b);
                v.w[0] = present;
                v.w[1] = present && lf.poly ? bits::read_ubitvar(b) : 0;
            }
            v.stamp = es.stamp;
            return !b.bad;
        }
        blk = d;
        base = eat + 1;
        lay = &l.list[lf.child];
    }
    es.err = "empty field path";
    return false;
}

static bool read_fields(entities_t& es, bits_t& b, entity_t& e) {
    int n = field_path::read(b, es.paths.data(), (int)es.paths.size(), es.err);
    if (n < 0)
        return false;
    for (int i = 0; i < n; i++)
        if (!apply(es, b, e, es.paths[i]))
            return false;
    es.stats.fields += n;
    return true;
}

static void fresh(entities_t& es, entity_t& e, int cls) {
    int li = es.lay->class_layout[cls];
    e.layout = li;
    e.slots.assign(es.lay->list[li].size, value_t{});
    e.dyn.clear();
    e.strs.clear();
}

static const entity_t* baseline(entities_t& es, int cls) {
    if ((size_t)cls >= es.baseline_data.size() || es.baseline_data[cls].empty())
        return nullptr;
    entity_t& t = es.baselines[cls];
    if (es.baseline_state[cls])
        return &t;
    fresh(es, t, cls);
    t.cls = cls;
    std::vector<uint8_t>& data = es.baseline_data[cls];
    bits_t b = bits::make(data.data(), data.size() - 16);
    std::string keep = es.err;
    uint32_t now = es.stamp;
    es.stamp = now - 1;
    if (!read_fields(es, b, t))
        es.stats.baseline_failed++;
    es.stamp = now;
    es.err = keep;
    es.baseline_state[cls] = 1;
    return &t;
}

bool packet(entities_t& es, const uint8_t* msg, size_t n) {
    const uint8_t* data = nullptr;
    size_t len = 0;
    uint64_t updated = 0, vis_bits = 0;
    proto_t r = proto::make(msg, n);
    proto_field_t f;
    while (proto::next(r, f)) {
        if (f.num == 2 && f.wire == 0)
            updated = f.val;
        else if (f.num == 7 && f.wire == 2) {
            data = f.data;
            len = f.len;
        } else if (f.num == 17 && f.wire == 0)
            vis_bits = f.val;
    }
    es.touched.clear();
    if (r.bad) {
        es.err = "bad packet entities message";
        es.stats.failed++;
        return false;
    }
    if (!data)
        return true;

    bits_t b = bits::make(data, len);
    int32_t index = -1;
    for (uint64_t i = 0; i < updated; i++) {
        index += (int32_t)bits::read_ubitvar(b) + 1;
        if (b.bad || index < 0 || index > max_entities) {
            es.err = "bad entity index";
            es.stats.failed++;
            return false;
        }
        uint32_t cmd = bits::read(b, 2);
        if (cmd & 1) {
            if (cmd & 2) {
                es.list[index].active = false;
                es.stats.deleted++;
            } else {
                es.stats.left++;
            }
            es.touched.push_back(index);
            continue;
        }

        if (cmd & 2) {
            uint32_t cls = bits::read(b, es.cls_bits);
            uint32_t serial = bits::read(b, 17);
            bits::read_varint(b);
            if (b.bad || cls >= es.lay->class_layout.size() || es.lay->class_layout[cls] < 0) {
                es.err = "unknown class id " + std::to_string(cls);
                es.stats.failed++;
                return false;
            }
            entity_t& e = es.list[index];
            const entity_t* base = baseline(es, (int)cls);
            if (base)
                e = *base;
            else
                fresh(es, e, (int)cls);
            e.index = index;
            e.cls = (int32_t)cls;
            e.serial = (int32_t)serial;
            e.active = true;
            if (!read_fields(es, b, e)) {
                es.err = es.ser->classes[cls] + ": " + es.err;
                es.stats.failed++;
                return false;
            }
            es.stats.created++;
        } else {
            if (vis_bits && (bits::read(b, 2) & 1))
                continue;
            entity_t& e = es.list[index];
            if (!e.active) {
                es.err = "update of a missing entity";
                es.stats.failed++;
                return false;
            }
            if (!read_fields(es, b, e)) {
                es.err = es.ser->classes[e.cls] + ": " + es.err;
                es.stats.failed++;
                return false;
            }
            es.stats.updated++;
        }
        es.touched.push_back(index);
    }
    return true;
}

void reset(entities_t& es) {
    for (entity_t& e : es.list)
        e.active = false;
    es.touched.clear();
}

const entity_t* get(const entities_t& es, int index) {
    if (index < 0 || (size_t)index >= es.list.size() || !es.list[index].active)
        return nullptr;
    return &es.list[index];
}

}
