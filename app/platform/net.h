#pragma once

namespace net {
    typedef void (*progress_fn_t)(uint64_t done, uint64_t total, void* user);

    bool get(const std::wstring& url, std::string& body, const wchar_t* headers = nullptr, progress_fn_t progress = nullptr, void* user = nullptr);
    std::string sha256(const std::string& bytes);
}
