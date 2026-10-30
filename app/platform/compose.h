#pragma once

#include <d3d11.h>
#include <dcomp.h>

namespace compose {
    bool init(HWND hwnd);
    void cleanup();
    void commit();
    ID3D11Device* d3d();
    IDCompositionDevice* device();
    IDCompositionVisual* web_visual();
    IDCompositionVisual* video_visual();
}
