#include "pch.h"

#include "parser/demo/demo.h"
#include "parser/demo/snappy.h"
#include "parser/demo/zstdx.h"
#include "parser/proto/proto.h"

#ifdef _WIN32
#include <windows.h>
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

namespace demo {

const size_t ALL = SIZE_MAX;
const size_t FAILED = SIZE_MAX - 1;
const size_t OVERREAD = 16;

#ifdef _WIN32
static std::wstring widen(const char* path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w.data(), n);
    return w;
}
#endif

static FILE* open_utf8(const char* path) {
#ifdef _WIN32
    std::wstring w = widen(path);
    if (w.empty())
        return nullptr;
    return _wfopen(w.c_str(), L"rb");
#else
    return fopen(path, "rb");
#endif
}

static bool read_file(const char* path, zstdx::buffer_t& buf, size_t& size) {
    FILE* f = open_utf8(path);
    if (!f)
        return false;
    fseek64(f, 0, SEEK_END);
    int64_t n = ftell64(f);
    fseek64(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return false;
    }
    buf.resize((size_t)n + 16);
    size_t got = fread(buf.data(), 1, (size_t)n, f);
    fclose(f);
    memset(buf.data() + n, 0, 16);
    size = (size_t)n;
    return got == (size_t)n;
}

static bool parse_header(demo_t& d, const frame_t& f) {
    proto_t r = proto::make(f.data, f.len);
    proto_field_t pf;
    while (proto::next(r, pf)) {
        switch (pf.num) {
        case 1: d.info.stamp = proto::str(pf); break;
        case 2: d.info.protocol = proto::i32(pf); break;
        case 3: d.info.server = proto::str(pf); break;
        case 4: d.info.client = proto::str(pf); break;
        case 5: d.info.map = proto::str(pf); break;
        case 6: d.info.game_dir = proto::str(pf); break;
        case 11: d.info.version = proto::str(pf); break;
        case 13: d.info.build = proto::i32(pf); break;
        }
    }
    return !r.bad && !d.info.map.empty();
}

static bool map_file(demo_t& d, const char* path) {
#ifdef _WIN32
    const size_t PAGE = 4096;
    std::wstring w = widen(path);
    if (w.empty())
        return false;
    HANDLE f = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 16 || PAGE - (size_t)size.QuadPart % PAGE < OVERREAD || (size_t)size.QuadPart % PAGE == 0) {
        CloseHandle(f);
        return false;
    }
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const void* v = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (!v) {
        if (m)
            CloseHandle(m);
        CloseHandle(f);
        return false;
    }
    d.file = f;
    d.mapping = m;
    d.view = v;
    d.data = (const uint8_t*)v;
    d.size = (size_t)size.QuadPart;
    return true;
#else
    return false;
#endif
}

static void unpack(demo_t* d) {
    size_t len = 0;
    std::string err;
    bool ok = zstdx::decompress(d->src, d->src_size, d->buf, len, err, &d->ready, &d->stop);
    if (!ok)
        d->unpack_err = err;
    d->ready.store(ok ? ALL : FAILED);
    d->ready.notify_all();
}

static bool wait_for(demo_t& d, size_t need) {
    for (;;) {
        size_t r = d.ready.load();
        if (r == FAILED) {
            d.err = "zstd: " + d.unpack_err;
            return false;
        }
        if (r >= need)
            return true;
        d.ready.wait(r);
    }
}

void close(demo_t& d) {
    if (d.unpacker) {
        d.stop = true;
        d.unpacker->join();
        delete d.unpacker;
        d.unpacker = nullptr;
    }
#ifdef _WIN32
    if (d.view)
        UnmapViewOfFile(d.view);
    if (d.mapping)
        CloseHandle((HANDLE)d.mapping);
    if (d.file)
        CloseHandle((HANDLE)d.file);
#endif
    d.view = nullptr;
    d.mapping = nullptr;
    d.file = nullptr;
    d.src = nullptr;
    d.data = d.buf.empty() ? nullptr : d.buf.data();
}

bool open(demo_t& d, const char* path) {
    d.err.clear();
    d.ready = ALL;
    if (!map_file(d, path)) {
        if (!read_file(path, d.buf, d.size)) {
            d.err = std::string("cant read ") + path;
            return false;
        }
        d.data = d.buf.data();
    }
    d.src = d.data;
    d.src_size = d.size;
    d.packed_size = d.size;
    if (zstdx::is_zstd(d.src, d.src_size)) {
        uint64_t total = d.view ? zstdx::content_size(d.src, d.src_size) : 0;
        if (total && total < (1ull << 34)) {
            d.buf.resize((size_t)total + zstdx::SLACK);
            d.data = d.buf.data();
            d.size = (size_t)total;
            d.ready = 0;
            d.unpacker = new std::thread(unpack, &d);
        } else {
            zstdx::buffer_t out;
            size_t len = 0;
            std::string zerr;
            if (!zstdx::decompress(d.src, d.src_size, out, len, zerr)) {
                d.err = "zstd: " + zerr;
                return false;
            }
            d.buf.swap(out);
            close(d);
            d.data = d.buf.data();
            d.size = len;
        }
    }
    if (!wait_for(d, std::min<size_t>(16, d.size)))
        return false;
    if (d.size < 16 || memcmp(d.data, "PBDEMS2\0", 8)) {
        d.err = "not a cs2 demo";
        return false;
    }
    d.first = 16;
    d.pos = d.first;

    frame_t f;
    if (!next(d, f) || f.cmd != dem_file_header || !parse_header(d, f)) {
        if (d.err.empty())
            d.err = "first frame is not a file header";
        return false;
    }
    rewind(d);
    return true;
}

bool last_full(demo_t& d, size_t& at) {
    bool found = false;
    size_t pos = d.pos;
    while (pos < d.size) {
        if (!wait_for(d, std::min(d.size, pos + 32)))
            return false;
        const uint8_t* p = d.data + pos;
        const uint8_t* end = d.data + d.size;
        uint64_t cmd, tick, len;
        if (!proto::varint(p, end, cmd) || !proto::varint(p, end, tick) || !proto::varint(p, end, len) ||
            len > (uint64_t)(end - p))
            break;
        cmd &= ~64ull;
        if (cmd == dem_full_packet) {
            at = pos;
            found = true;
        } else if (cmd == dem_stop) {
            break;
        }
        pos = (size_t)(p - d.data) + (size_t)len;
    }
    return found;
}

bool next(demo_t& d, frame_t& f) {
    if (d.pos >= d.size)
        return false;
    if (!wait_for(d, std::min(d.size, d.pos + 32))) {
        d.pos = d.size;
        return false;
    }
    const uint8_t* p = d.data + d.pos;
    const uint8_t* end = d.data + d.size;
    uint64_t cmd, tick, len;
    if (!proto::varint(p, end, cmd) || !proto::varint(p, end, tick) || !proto::varint(p, end, len) ||
        len > (uint64_t)(end - p)) {
        d.err = "truncated frame at " + std::to_string(d.pos);
        d.pos = d.size;
        return false;
    }
    if (!wait_for(d, std::min(d.size, (size_t)(p - d.data) + (size_t)len + OVERREAD))) {
        d.pos = d.size;
        return false;
    }
    f.compressed = cmd & 64;
    f.cmd = (int)(cmd & ~64ull);
    f.tick = (int)(uint32_t)tick;
    if (f.compressed) {
        if (!snappy::decompress(p, (size_t)len, d.unpacked, f.len)) {
            d.err = "bad snappy frame at " + std::to_string(d.pos);
            d.pos = d.size;
            return false;
        }
        f.data = d.unpacked.data();
    } else {
        f.data = p;
        f.len = (size_t)len;
    }
    d.pos = (size_t)(p - d.data) + (size_t)len;
    if (f.cmd == dem_stop)
        d.pos = d.size;
    return true;
}

void rewind(demo_t& d) {
    d.pos = d.first;
    d.err.clear();
}

}
