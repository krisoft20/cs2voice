#pragma once

namespace mp4 {
    struct track_t {
        uint32_t codec = 0;
        uint32_t timescale = 0;
        int length_size = 4;
        std::vector<std::vector<uint8_t>> params;
        std::vector<uint8_t> asc;
        int channels = 0;
        int rate = 0;
    };

    struct sample_t {
        uint64_t dts = 0;
        int32_t cto = 0;
        uint32_t dur = 0;
        size_t offset = 0;
        uint32_t size = 0;
        bool sync = false;
    };

    uint32_t fourcc(const char* s);
    bool init(const uint8_t* p, size_t n, track_t& out);
    bool fragments(const uint8_t* p, size_t n, std::vector<sample_t>& out);
}
