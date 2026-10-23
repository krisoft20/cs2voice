#include "pch.h"

#include <share.h>
#include <windows.h>

#include "dbg.h"

namespace dbg {
    FILE* g_file = nullptr;
    uint64_t g_start = 0;

    bool init(const std::string& path) {
        g_start = GetTickCount64();
        g_file = _fsopen(path.c_str(), "w", _SH_DENYWR);
        return g_file != nullptr;
    }

    void cleanup() {
        if (g_file)
            fclose(g_file);
        g_file = nullptr;
    }

    void print(const char* fmt, ...) {
        char line[1024];
        int head = snprintf(line, sizeof(line), "%6llu ", (unsigned long long)(GetTickCount64() - g_start));
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(line + head, sizeof(line) - head - 2, fmt, args);
        va_end(args);
        if (n < 0)
            return;
        n += head;
        if (n > (int)sizeof(line) - 2)
            n = (int)sizeof(line) - 2;
        line[n] = '\n';
        line[n + 1] = 0;
        OutputDebugStringA(line);
        if (g_file) {
            fputs(line, g_file);
            fflush(g_file);
        }
    }
}
