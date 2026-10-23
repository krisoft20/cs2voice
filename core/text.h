#pragma once

namespace text {
    std::wstring wide(const std::string& s);
    std::string utf8(const std::wstring& s);
    std::string lower(const std::string& s);
}
