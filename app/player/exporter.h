#pragma once

namespace exporter {
    struct job_t {
        std::string id;
        std::wstring dir;
        std::wstring out;
        double from = 0;
        double to = 0;
        int width = 0;
        int height = 0;
        int fps = 60;
        int bitrate = 0;
        uint64_t max_bytes = 0;
    };

    void add(const job_t& job);
    void cancel(const std::string& id);
    void cleanup();
}
