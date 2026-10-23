#pragma once

namespace files {
    void wipe(const std::wstring& dir, const wchar_t* pattern);
    void clip(HWND owner, const std::string& s);
    void chomp(std::string& s);
}
