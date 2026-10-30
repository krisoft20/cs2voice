#include "pch.h"

#include <windows.h>

#include "video/chunks.h"

namespace chunks {
    const size_t HEAD = 4096;

    std::wstring name(int stream, int idx) {
        wchar_t buf[64];
        swprintf(buf, 64, L"\\chunk-stream%d-%05d.m4s", stream, idx);
        return buf;
    }

    bool first_time(const std::wstring& path, uint64_t& t) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;
        uint8_t buf[HEAD];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        for (size_t i = 4; i + 16 <= n; i++) {
            if (memcmp(buf + i, "tfdt", 4) != 0)
                continue;
            const uint8_t* b = buf + i + 4;
            if (b[0] == 1) {
                t = 0;
                for (int k = 0; k < 8; k++)
                    t = (t << 8) | b[4 + k];
            } else {
                t = ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | b[7];
            }
            return true;
        }
        return false;
    }

    std::vector<chunk_t> scan(const std::wstring& dir, int stream) {
        std::vector<chunk_t> out;
        WIN32_FIND_DATAW fd;
        wchar_t pattern[32];
        swprintf(pattern, 32, L"\\chunk-stream%d-*.m4s", stream);
        HANDLE h = FindFirstFileW((dir + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return out;
        do {
            chunk_t c;
            c.idx = _wtoi(fd.cFileName + 14);
            c.bytes = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (first_time(dir + L"\\" + fd.cFileName, c.start))
                out.push_back(c);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(out.begin(), out.end(), [](const chunk_t& a, const chunk_t& b) { return a.idx < b.idx; });
        return out;
    }

    int find(const std::vector<chunk_t>& list, uint64_t t) {
        auto it = std::upper_bound(list.begin(), list.end(), t, [](uint64_t v, const chunk_t& c) { return v < c.start; });
        return it == list.begin() ? 0 : (int)(it - list.begin()) - 1;
    }
}
