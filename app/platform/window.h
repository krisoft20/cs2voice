#pragma once

struct buffer_t {
    std::string bytes;
    std::string json;
};

namespace window {
    const UINT WM_POST_JSON = WM_APP + 1;
    const UINT WM_RESCAN = WM_APP + 2;
    const UINT WM_POST_BUFFER = WM_APP + 3;
    const UINT WM_TRAY = WM_APP + 4;
    const UINT WM_SHOW = WM_APP + 5;
    const UINT WM_QUIT_APP = WM_APP + 6;
    const UINT WM_HIDE = WM_APP + 7;

    bool init(HINSTANCE inst, int w, int h, bool hidden);
    void cleanup();
    HWND handle();
    void post_json(const std::string& json);
    void post_buffer(std::string&& bytes, const std::string& json);
    void post_rescan();
    void command(const std::string& action);
    void set_close(const std::string& mode);
    void start_in_tray();
    void set_menu(const std::wstring& open, const std::wstring& quit);
    bool pump();
}
