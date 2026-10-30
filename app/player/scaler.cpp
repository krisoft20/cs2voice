#include "pch.h"

#include <d3dcompiler.h>

#include "dbg.h"
#include "player/scaler.h"

namespace scaler {
    const char* SOURCE = R"(
Texture2D src : register(t0);

cbuffer params : register(b0) {
    float2 src_size;
    float2 scale;
};

struct vs_out {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

vs_out vs(uint id : SV_VertexID) {
    vs_out o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

float lanczos(float x) {
    x = abs(x);
    if (x < 1e-5)
        return 1;
    if (x >= 3)
        return 0;
    float px = 3.14159265 * x;
    return 3 * sin(px) * sin(px / 3) / (px * px);
}

float4 ps(vs_out i) : SV_Target {
    float2 c = i.uv * src_size - 0.5;
    float2 r = 3 / scale;
    int2 lo = int2(ceil(c - r));
    int2 hi = int2(floor(c + r));
    int2 last = int2(src_size) - 1;
    float3 acc = 0;
    float sum = 0;
    for (int y = lo.y; y <= hi.y; y++) {
        float wy = lanczos((y - c.y) * scale.y);
        for (int x = lo.x; x <= hi.x; x++) {
            float w = wy * lanczos((x - c.x) * scale.x);
            acc += w * src.Load(int3(clamp(int2(x, y), 0, last), 0)).rgb;
            sum += w;
        }
    }
    return float4(saturate(acc / sum), 1);
}
)";

    struct params_t {
        float src_w, src_h;
        float scale_x, scale_y;
    };

    ID3D11VertexShader* g_vs = nullptr;
    ID3D11PixelShader* g_ps = nullptr;
    ID3D11Buffer* g_cb = nullptr;

    ID3DBlob* compile(const char* entry, const char* target, std::string& err) {
        ID3DBlob* code = nullptr;
        ID3DBlob* msg = nullptr;
        HRESULT hr = D3DCompile(SOURCE, strlen(SOURCE), "scaler", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &msg);
        if (FAILED(hr)) {
            err = msg ? std::string((const char*)msg->GetBufferPointer(), msg->GetBufferSize()) : "cant compile the scaler";
            if (code)
                code->Release();
            code = nullptr;
        }
        if (msg)
            msg->Release();
        return code;
    }

    bool init(ID3D11Device* dev, std::string& err) {
        if (g_ps)
            return true;
        ID3DBlob* vs = compile("vs", "vs_5_0", err);
        ID3DBlob* ps = vs ? compile("ps", "ps_5_0", err) : nullptr;
        bool ok = vs && ps && SUCCEEDED(dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
                  SUCCEEDED(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps));
        if (vs)
            vs->Release();
        if (ps)
            ps->Release();
        D3D11_BUFFER_DESC bd = {sizeof(params_t), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
        ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &g_cb));
        if (!ok) {
            dbg::print("[scaler] %s", err.c_str());
            cleanup();
        }
        return ok;
    }

    void cleanup() {
        IUnknown* all[] = {g_cb, g_ps, g_vs};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        g_cb = nullptr;
        g_ps = nullptr;
        g_vs = nullptr;
    }

    void draw(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, int sw, int sh, ID3D11RenderTargetView* dst, int dw, int dh) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (!g_ps || FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            return;
        params_t* p = (params_t*)m.pData;
        p->src_w = (float)sw;
        p->src_h = (float)sh;
        p->scale_x = std::min(1.0f, (float)dw / sw);
        p->scale_y = std::min(1.0f, (float)dh / sh);
        ctx->Unmap(g_cb, 0);
        D3D11_VIEWPORT vp = {0, 0, (float)dw, (float)dh, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, &dst, nullptr);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(g_vs, nullptr, 0);
        ctx->PSSetShader(g_ps, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &g_cb);
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);
        ID3D11RenderTargetView* no_rt = nullptr;
        ctx->OMSetRenderTargets(1, &no_rt, nullptr);
    }
}
