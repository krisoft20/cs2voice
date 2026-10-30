#include "pch.h"

#include <d3d11_4.h>
#include <dxgi.h>

#include "dbg.h"
#include "platform/compose.h"

namespace compose {
    ID3D11Device* g_d3d = nullptr;
    IDCompositionDevice* g_dev = nullptr;
    IDCompositionTarget* g_target = nullptr;
    IDCompositionVisual* g_root = nullptr;
    IDCompositionVisual* g_web = nullptr;
    IDCompositionVisual* g_video = nullptr;

    bool init(HWND hwnd) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &g_d3d, nullptr, nullptr);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &g_d3d, nullptr,
                                   nullptr);
        if (FAILED(hr)) {
            dbg::print("[compose] no d3d11 device 0x%08lx", hr);
            return false;
        }
        ID3D11Multithread* mt = nullptr;
        if (SUCCEEDED(g_d3d->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
        }
        IDXGIDevice* dxgi = nullptr;
        g_d3d->QueryInterface(IID_PPV_ARGS(&dxgi));
        hr = DCompositionCreateDevice(dxgi, IID_PPV_ARGS(&g_dev));
        dxgi->Release();
        if (FAILED(hr) || FAILED(g_dev->CreateTargetForHwnd(hwnd, TRUE, &g_target)) || FAILED(g_dev->CreateVisual(&g_root)) ||
            FAILED(g_dev->CreateVisual(&g_video)) || FAILED(g_dev->CreateVisual(&g_web))) {
            dbg::print("[compose] directcomposition failed 0x%08lx", hr);
            cleanup();
            return false;
        }
        g_root->AddVisual(g_web, FALSE, nullptr);
        g_root->AddVisual(g_video, FALSE, g_web);
        g_target->SetRoot(g_root);
        g_dev->Commit();
        dbg::print("[compose] ready");
        return true;
    }

    void cleanup() {
        IUnknown* all[] = {g_web, g_video, g_root, g_target, g_dev, g_d3d};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        g_web = g_video = g_root = nullptr;
        g_target = nullptr;
        g_dev = nullptr;
        g_d3d = nullptr;
    }

    void commit() {
        if (g_dev)
            g_dev->Commit();
    }

    ID3D11Device* d3d() {
        return g_d3d;
    }

    IDCompositionDevice* device() {
        return g_dev;
    }

    IDCompositionVisual* web_visual() {
        return g_web;
    }

    IDCompositionVisual* video_visual() {
        return g_video;
    }
}
