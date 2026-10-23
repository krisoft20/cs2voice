#pragma once

namespace web {
    bool init(HWND hwnd, const std::wstring& dev_url, const std::wstring& profile_dir, const std::wstring& data_dir);
    bool runtime();
    void cleanup();
    void resize();
    void post(const std::string& json);
    void post_buffer(const std::string& bytes, const std::string& json);
    void screenshot(const std::wstring& path, const std::wstring& page, UINT delay_ms);
}
