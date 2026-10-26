#include "pch.h"

#include <mutex>

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

struct demo_unpack_t {
    std::string key;
    void* file = nullptr;
    void* mapping = nullptr;
    const void* view = nullptr;
    const uint8_t* src = nullptr;
    size_t src_size = 0;
    zstdx::buffer_t buf;
    size_t size = 0;
    std::atomic<size_t> ready = 0;
    std::atomic<bool> stop = false;
    std::string err;
    std::thread thread;
    int users = 0;
};

namespace demo {

const size_t ALL = SIZE_MAX;
const size_t FAILED = SIZE_MAX - 1;
const size_t OVERREAD = 16;

static std::mutex g_unpack_mu;
static std::vector<demo_unpack_t*> g_unpacks;

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

static void unmap(void*& file, void*& mapping, const void*& view) {
#ifdef _WIN32
    if (view)
        UnmapViewOfFile(view);
    if (mapping)
        CloseHandle((HANDLE)mapping);
    if (file)
        CloseHandle((HANDLE)file);
#endif
    view = nullptr;
    mapping = nullptr;
    file = nullptr;
}

static void unpack(demo_unpack_t* u) {
    size_t len = 0;
    std::string err;
    bool ok = zstdx::decompress(u->src, u->src_size, u->buf, len, err, &u->ready, &u->stop);
    if (!ok)
        u->err = err;
    u->ready.store(ok ? ALL : FAILED);
    u->ready.notify_all();
}

static uint64_t stamp_of(void* file) {
#ifdef _WIN32
    FILETIME t = {};
    if (GetFileTime((HANDLE)file, nullptr, nullptr, &t))
        return ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;
#endif
    return 0;
}

static void share(demo_t& d, const char* path, size_t total) {
    std::string key = std::string(path) + "|" + std::to_string(d.src_size) + "|" + std::to_string(stamp_of(d.file));
    std::lock_guard<std::mutex> lock(g_unpack_mu);
    demo_unpack_t* u = nullptr;
    for (demo_unpack_t* x : g_unpacks)
        if (x->key == key)
            u = x;
    if (u) {
        unmap(d.file, d.mapping, d.view);
#ifdef _WIN32
        int mine = GetThreadPriority(GetCurrentThread());
        if (mine > GetThreadPriority(u->thread.native_handle()))
            SetThreadPriority(u->thread.native_handle(), mine);
#endif
    } else {
        u = new demo_unpack_t();
        u->key = key;
        u->file = d.file;
        u->mapping = d.mapping;
        u->view = d.view;
        u->src = d.src;
        u->src_size = d.src_size;
        d.file = d.mapping = nullptr;
        d.view = nullptr;
        u->buf.resize(total + zstdx::SLACK);
        u->size = total;
        u->thread = std::thread(unpack, u);
#ifdef _WIN32
        SetThreadPriority(u->thread.native_handle(), GetThreadPriority(GetCurrentThread()));
#endif
        g_unpacks.push_back(u);
    }
    u->users++;
    d.unpack = u;
    d.src = u->src;
    d.data = u->buf.data();
    d.size = u->size;
}

static void release(demo_unpack_t* u) {
    {
        std::lock_guard<std::mutex> lock(g_unpack_mu);
        if (--u->users > 0)
            return;
        g_unpacks.erase(std::find(g_unpacks.begin(), g_unpacks.end(), u));
    }
    u->stop = true;
    u->thread.join();
    unmap(u->file, u->mapping, u->view);
    delete u;
}

static bool wait_for(demo_t& d, size_t need) {
    if (!d.unpack)
        return true;
    for (;;) {
        size_t r = d.unpack->ready.load();
        if (r == FAILED) {
            d.err = "zstd: " + d.unpack->err;
            return false;
        }
        if (r >= need)
            return true;
        d.unpack->ready.wait(r);
    }
}

void close(demo_t& d) {
    if (d.unpack) {
        release(d.unpack);
        d.unpack = nullptr;
    }
    unmap(d.file, d.mapping, d.view);
    d.src = nullptr;
    d.data = d.buf.empty() ? nullptr : d.buf.data();
}

bool open(demo_t& d, const char* path) {
    d.err.clear();
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
            share(d, path, (size_t)total);
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
