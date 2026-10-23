#pragma once

namespace bridge {
    bool init(const std::wstring& data_dir);
    void cleanup();
    void handle(const std::string& json);
    void rescan();
}
