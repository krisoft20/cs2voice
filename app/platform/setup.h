#pragma once

struct setup_t {
    bool installed = false;
    bool here = false;
    bool desktop = false;
    bool autostart = false;
    std::wstring dir;
};

namespace setup {
    std::wstring exe_path();
    setup_t state();
    bool install(bool desktop, std::string& err);
    bool start_installed();
    void uninstall(bool data, const std::wstring& data_dir);
    void set_desktop(bool on);
    void set_autostart(bool on);
    void refresh();
}
