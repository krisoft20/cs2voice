#pragma once

struct chunk_t {
    int idx = 0;
    uint64_t start = 0;
    uint64_t bytes = 0;
};

namespace chunks {
    std::wstring name(int stream, int idx);
    std::vector<chunk_t> scan(const std::wstring& dir, int stream);
    int find(const std::vector<chunk_t>& list, uint64_t t);
}
