#pragma once

#include <d3d11.h>

namespace scaler {
    bool init(ID3D11Device* dev, std::string& err);
    void cleanup();
    void draw(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, int sw, int sh, ID3D11RenderTargetView* dst, int dw, int dh);
}
