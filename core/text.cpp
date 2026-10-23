#include "pch.h"

#include <windows.h>

#include "text.h"

namespace text {
    std::wstring wide(const std::string& s) {
        if (s.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    std::string utf8(const std::wstring& s) {
        if (s.empty())
            return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    std::string lower(const std::string& s) {
        std::string out = s;
        for (char& c : out)
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
        return out;
    }
}
