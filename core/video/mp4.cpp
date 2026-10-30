#include "pch.h"

#include "video/mp4.h"

namespace mp4 {
    struct box_t {
        uint32_t type = 0;
        const uint8_t* body = nullptr;
        size_t size = 0;
    };

    uint32_t be32(const uint8_t* p) {
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }

    uint64_t be64(const uint8_t* p) {
        return (uint64_t)be32(p) << 32 | be32(p + 4);
    }

    uint32_t fourcc(const char* s) {
        return (uint32_t)(uint8_t)s[0] << 24 | (uint32_t)(uint8_t)s[1] << 16 | (uint32_t)(uint8_t)s[2] << 8 | (uint8_t)s[3];
    }

    bool next(const uint8_t*& p, const uint8_t* end, box_t& b) {
        if (end - p < 8)
            return false;
        uint64_t size = be32(p);
        b.type = be32(p + 4);
        size_t head = 8;
        if (size == 1) {
            if (end - p < 16)
                return false;
            size = be64(p + 8);
            head = 16;
        } else if (size == 0) {
            size = (uint64_t)(end - p);
        }
        if (size < head || size > (uint64_t)(end - p))
            return false;
        b.body = p + head;
        b.size = (size_t)size - head;
        p += size;
        return true;
    }

    const uint8_t* find(const uint8_t* p, const uint8_t* end, std::initializer_list<const char*> path, size_t& size) {
        for (const char* name : path) {
            box_t b;
            bool found = false;
            while (next(p, end, b))
                if (b.type == fourcc(name)) {
                    found = true;
                    break;
                }
            if (!found)
                return nullptr;
            p = b.body;
            end = b.body + b.size;
            size = b.size;
        }
        return p;
    }

    bool hevc_config(const uint8_t* h, size_t n, track_t& out) {
        if (n < 23)
            return false;
        out.length_size = (h[21] & 3) + 1;
        const uint8_t* a = h + 23;
        const uint8_t* end = h + n;
        for (int i = 0; i < h[22]; i++) {
            if (end - a < 3)
                return false;
            int count = a[1] << 8 | a[2];
            a += 3;
            for (int k = 0; k < count; k++) {
                if (end - a < 2)
                    return false;
                size_t len = (size_t)(a[0] << 8 | a[1]);
                if ((size_t)(end - a - 2) < len)
                    return false;
                out.params.emplace_back(a + 2, a + 2 + len);
                a += 2 + len;
            }
        }
        return true;
    }

    size_t desc_size(const uint8_t*& p, const uint8_t* end) {
        size_t size = 0;
        for (int i = 0; i < 4 && p < end; i++) {
            uint8_t b = *p++;
            size = size << 7 | (b & 0x7f);
            if (!(b & 0x80))
                break;
        }
        return size;
    }

    bool aac_config(const uint8_t* p, size_t n, track_t& out) {
        const uint8_t* end = p + n;
        p += 4;
        if (p >= end || *p++ != 3)
            return false;
        desc_size(p, end);
        if (end - p < 3)
            return false;
        uint8_t flags = p[2];
        p += 3;
        if (flags & 0x80)
            p += 2;
        if (flags & 0x40 && p < end)
            p += 1 + *p;
        if (flags & 0x20)
            p += 2;
        if (p >= end || *p++ != 4)
            return false;
        desc_size(p, end);
        p += 13;
        if (p >= end || *p++ != 5)
            return false;
        size_t len = desc_size(p, end);
        if (len < 2 || (size_t)(end - p) < len)
            return false;
        out.asc.assign(p, p + len);
        return true;
    }

    bool init(const uint8_t* p, size_t n, track_t& out) {
        size_t size = 0;
        const uint8_t* end = p + n;
        const uint8_t* mdhd = find(p, end, {"moov", "trak", "mdia", "mdhd"}, size);
        if (mdhd && size >= 24)
            out.timescale = mdhd[0] == 1 ? be32(mdhd + 20) : be32(mdhd + 12);
        const uint8_t* stsd = find(p, end, {"moov", "trak", "mdia", "minf", "stbl", "stsd"}, size);
        if (!stsd || size < 8)
            return false;
        const uint8_t* q = stsd + 8;
        box_t entry;
        if (!next(q, stsd + size, entry))
            return false;
        out.codec = entry.type;
        if (entry.type == fourcc("mp4a")) {
            if (entry.size < 28)
                return false;
            out.channels = entry.body[16] << 8 | entry.body[17];
            out.rate = (int)(be32(entry.body + 24) >> 16);
            const uint8_t* c = entry.body + 28;
            box_t b;
            while (next(c, entry.body + entry.size, b))
                if (b.type == fourcc("esds"))
                    return aac_config(b.body, b.size, out);
            return false;
        }
        if (entry.type != fourcc("hev1") && entry.type != fourcc("hvc1"))
            return out.timescale != 0;
        if (entry.size < 78)
            return false;
        const uint8_t* c = entry.body + 78;
        box_t b;
        while (next(c, entry.body + entry.size, b))
            if (b.type == fourcc("hvcC"))
                return hevc_config(b.body, b.size, out);
        return false;
    }

    bool fragments(const uint8_t* p, size_t n, std::vector<sample_t>& out) {
        const uint8_t* end = p + n;
        const uint8_t* at = p;
        box_t moof;
        while (true) {
            const uint8_t* start = at;
            if (!next(at, end, moof))
                break;
            if (moof.type != fourcc("moof"))
                continue;
            const uint8_t* t = moof.body;
            box_t traf;
            while (next(t, moof.body + moof.size, traf)) {
                if (traf.type != fourcc("traf"))
                    continue;
                uint32_t def_dur = 0, def_size = 0, def_flags = 0;
                uint64_t base = 0;
                const uint8_t* r = traf.body;
                box_t b;
                while (next(r, traf.body + traf.size, b)) {
                    if (b.size < 8)
                        continue;
                    uint32_t flags = be32(b.body) & 0xffffff;
                    if (b.type == fourcc("tfhd")) {
                        const uint8_t* f = b.body + 8;
                        if (flags & 0x1)
                            f += 8;
                        if (flags & 0x2)
                            f += 4;
                        if (flags & 0x8) {
                            def_dur = be32(f);
                            f += 4;
                        }
                        if (flags & 0x10) {
                            def_size = be32(f);
                            f += 4;
                        }
                        if (flags & 0x20)
                            def_flags = be32(f);
                    } else if (b.type == fourcc("tfdt")) {
                        base = b.body[0] == 1 ? be64(b.body + 4) : be32(b.body + 4);
                    } else if (b.type == fourcc("trun")) {
                        uint32_t count = be32(b.body + 4);
                        const uint8_t* f = b.body + 8;
                        const uint8_t* stop = b.body + b.size;
                        int32_t data = 0;
                        uint32_t first = def_flags;
                        bool has_first = false;
                        if (flags & 0x1) {
                            data = (int32_t)be32(f);
                            f += 4;
                        }
                        if (flags & 0x4) {
                            first = be32(f);
                            has_first = true;
                            f += 4;
                        }
                        size_t offset = (size_t)(start - p) + data;
                        uint64_t dts = base;
                        for (uint32_t i = 0; i < count; i++) {
                            sample_t s;
                            s.dur = def_dur;
                            s.size = def_size;
                            uint32_t sflags = i == 0 && has_first ? first : def_flags;
                            if (flags & 0x100) {
                                if (stop - f < 4)
                                    return false;
                                s.dur = be32(f);
                                f += 4;
                            }
                            if (flags & 0x200) {
                                if (stop - f < 4)
                                    return false;
                                s.size = be32(f);
                                f += 4;
                            }
                            if (flags & 0x400) {
                                if (stop - f < 4)
                                    return false;
                                sflags = be32(f);
                                f += 4;
                            }
                            if (flags & 0x800) {
                                if (stop - f < 4)
                                    return false;
                                s.cto = (int32_t)be32(f);
                                f += 4;
                            }
                            s.sync = !(sflags & 0x10000);
                            s.dts = dts;
                            s.offset = offset;
                            if (offset + s.size > n)
                                return false;
                            offset += s.size;
                            dts += s.dur;
                            out.push_back(s);
                        }
                    }
                }
            }
        }
        return !out.empty();
    }
}
