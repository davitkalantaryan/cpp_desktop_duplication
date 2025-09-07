// =============================
// File: src/examples/from_msdn/OutputManager.h
// =============================
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <QtGui/QImage>

// Minimal OutputManager that **keeps your pipeline intact** but writes frames to QImage
// instead of drawing to a window. Drop-in replacement for the sample's OutputManager.*

class OutputManager {
public:
    OutputManager();
    ~OutputManager();

    // Call once with your existing D3D11 device/context. Rotation comes from duplication desc.
    bool InitOutput(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_MODE_ROTATION rotation);

    // Call once per frame with the acquired IDXGIResource from DuplicationManager::GetFrame.
    // Returns false on failure. On success, LastImage() holds the new QImage.
    bool ProcessFrame(IDXGIResource* desktopResource);

    // If rotation changes (e.g., after a modeswitch), update it.
    void SetRotation(DXGI_MODE_ROTATION r);

    const QImage& LastImage() const { return m_lastImage; }

private:
    using ComPtr = Microsoft::WRL::ComPtr<ID3D11Device>;
    using ComPtrCtx = Microsoft::WRL::ComPtr<ID3D11DeviceContext>;

    QImage d3d11TextureToQImage(ID3D11Texture2D* src) const;

    ComPtr    m_device;
    ComPtrCtx m_context;
    DXGI_MODE_ROTATION m_rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    QImage m_lastImage;
};


// =============================
// File: src/examples/from_msdn/OutputManager.cpp
// =============================
#include "OutputManager.h"
#include <QtCore/QDebug>

OutputManager::OutputManager() = default;
OutputManager::~OutputManager() = default;

bool OutputManager::InitOutput(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_MODE_ROTATION rotation) {
    if (!device || !context) return false;
    m_device = device; // ComPtr will AddRef
    m_context = context;
    m_rotation = rotation;
    m_lastImage = QImage();
    return true;
}

void OutputManager::SetRotation(DXGI_MODE_ROTATION r) { m_rotation = r; }

bool OutputManager::ProcessFrame(IDXGIResource* desktopResource) {
    if (!desktopResource || !m_device || !m_context) return false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frameTex;
    if (FAILED(desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(frameTex.GetAddressOf()))))
        return false;

    m_lastImage = d3d11TextureToQImage(frameTex.Get());
    return !m_lastImage.isNull();
}

QImage OutputManager::d3d11TextureToQImage(ID3D11Texture2D* src) const {
    if (!src) return {};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC desc{}; src->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.Usage = D3D11_USAGE_STAGING; sd.MiscFlags = 0;
    if (FAILED(m_device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) return {};

    m_context->CopyResource(staging.Get(), src);

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return {};

    const int w = static_cast<int>(desc.Width);
    const int h = static_cast<int>(desc.Height);

    QImage img;
    if (m_rotation == DXGI_MODE_ROTATION_IDENTITY || m_rotation == DXGI_MODE_ROTATION_UNSPECIFIED) {
        img = QImage(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            memcpy(img.scanLine(y), static_cast<const char*>(map.pData) + y * map.RowPitch, size_t(w) * 4);
        }
    }
    else if (m_rotation == DXGI_MODE_ROTATION_ROTATE180) {
        img = QImage(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            const auto* srcRow = reinterpret_cast<const QRgb*>(static_cast<const BYTE*>(map.pData) + (h - 1 - y) * map.RowPitch);
            auto* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) dst[x] = srcRow[w - 1 - x];
        }
    }
    else {
        const bool cw = (m_rotation == DXGI_MODE_ROTATION_ROTATE90);
        img = QImage(h, w, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            const auto* srcRow = reinterpret_cast<const QRgb*>(static_cast<const BYTE*>(map.pData) + y * map.RowPitch);
            for (int x = 0; x < w; ++x) {
                const int dx = cw ? (h - 1 - y) : y;
                const int dy = cw ? x : (w - 1 - x);
                *reinterpret_cast<QRgb*>(img.scanLine(dy) + dx * 4) = srcRow[x];
            }
        }
    }

    m_context->Unmap(staging.Get(), 0);
    return img;
}
