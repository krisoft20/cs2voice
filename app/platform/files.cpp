#include "pch.h"

#include "platform/files.h"
#include "text.h"

namespace files {
    void wipe(const std::wstring& dir, const wchar_t* pattern) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    void clip(HWND owner, const std::string& s) {
        std::wstring w = text::wide(s);
        if (!OpenClipboard(owner))
            return;
        EmptyClipboard();
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (mem) {
            memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        }
        CloseClipboard();
    }

    void chomp(std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
    }
}
