// zstdx - a Zstandard decoder (RFC 8878) written from scratch for cs2voice
//
// MIT License
//
// Copyright (c) 2026 krisoft20
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <atomic>

namespace zstdx {
    const size_t SLACK = 128 * 1024 + 64 + 16;

    template <class T>
    struct raw_alloc_t : std::allocator<T> {
        using value_type = T;
        raw_alloc_t() = default;
        template <class U>
        raw_alloc_t(const raw_alloc_t<U>&) {}
        template <class U>
        struct rebind {
            using other = raw_alloc_t<U>;
        };
        template <class U, class... A>
        void construct(U* p, A&&... a) {
            if constexpr (sizeof...(A) == 0)
                ::new ((void*)p) U;
            else
                ::new ((void*)p) U(std::forward<A>(a)...);
        }
    };

    typedef std::vector<uint8_t, raw_alloc_t<uint8_t>> buffer_t;

    bool is_zstd(const uint8_t* p, size_t n);
    uint64_t content_size(const uint8_t* p, size_t n);
    bool decompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t& len, std::string& err,
                    std::atomic<size_t>* ready = nullptr, const std::atomic<bool>* stop = nullptr);
    bool decompress(const uint8_t* in, size_t n, buffer_t& out, size_t& len, std::string& err, std::atomic<size_t>* ready = nullptr,
                    const std::atomic<bool>* stop = nullptr);
    void set_threads(int n);
    uint64_t xxh64(const uint8_t* p, size_t n, uint64_t seed);
}
