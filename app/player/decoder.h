#pragma once

#include <d3d11.h>

#include "video/mp4.h"

struct video_decoder_t;

namespace decoder {
    const int SURFACES = 20;

    struct frame_t {
        int surface = -1;
        int64_t pts = 0;
    };

    video_decoder_t* create(ID3D11Device* dev, const mp4::track_t& track, std::string& err);
    void destroy(video_decoder_t* d);
    bool decode(video_decoder_t* d, const uint8_t* au, size_t n, int64_t pts, std::string& err);
    bool pop(video_decoder_t* d, frame_t& out);
    void drain(video_decoder_t* d);
    void reset(video_decoder_t* d);
    void release(video_decoder_t* d, int surface);
    ID3D11Texture2D* texture(video_decoder_t* d);
    int width(video_decoder_t* d);
    int height(video_decoder_t* d);
    bool full_range(video_decoder_t* d);
    bool bt601(video_decoder_t* d);
}
