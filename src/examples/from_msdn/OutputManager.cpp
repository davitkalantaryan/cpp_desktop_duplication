// =============================
// File: OutputManager.cpp
// =============================
#include "OutputManager.h"
#include <QImage>
#include <QString>

OUTPUTMANAGER::OUTPUTMANAGER() {}
OUTPUTMANAGER::~OUTPUTMANAGER() { CleanRefs(); }

void OUTPUTMANAGER::CleanRefs() {
    SafeRelease(m_Staging);
    SafeRelease(m_KeyMutex);
    SafeRelease(m_SharedSurf);
    SafeRelease(m_Context);
    SafeRelease(m_Device);
}

DUPL_RETURN OUTPUTMANAGER::InitOutput(RECT DeskBounds, _Out_ HANDLE* SharedHandle, _Out_ ID3D11Texture2D** SharedSurf, _Out_ IDXGIKeyedMutex** KeyMutex) {
    m_DeskBounds = DeskBounds;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, nullptr, 0, D3D11_SDK_VERSION, &m_Device, &fl, &m_Context);
    if (FAILED(hr)) return ProcessFailure(nullptr, L"D3D11CreateDevice", L"Error", hr);

    // Create shared surface BGRA8 with keyed mutex
    const UINT width = m_DeskBounds.right - m_DeskBounds.left;
    const UINT height = m_DeskBounds.bottom - m_DeskBounds.top;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width; td.Height = height;
    td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc = { 1,0 };
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.CPUAccessFlags = 0; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr2 = m_Device->CreateTexture2D(&td, nullptr, &m_SharedSurf);
    if (FAILED(hr2)) return ProcessFailure(m_Device, L"CreateTexture2D shared", L"Error", hr2);

    // Keyed mutex
    hr2 = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&m_KeyMutex);
    if (FAILED(hr2)) return ProcessFailure(m_Device, L"QITable IDXGIKeyedMutex", L"Error", hr2);

    // Shared handle to open in worker devices
    CComPtr<IDXGIResource> dxgiRes; m_SharedSurf->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiRes);
    dxgiRes->GetSharedHandle(SharedHandle);

    // Staging texture for CPU readback
    D3D11_TEXTURE2D_DESC sd = td; sd.BindFlags = 0; sd.MiscFlags = 0; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr2 = m_Device->CreateTexture2D(&sd, nullptr, &m_Staging);
    if (FAILED(hr2)) return ProcessFailure(m_Device, L"CreateTexture2D staging", L"Error", hr2);

    *SharedSurf = m_SharedSurf; (*SharedSurf)->AddRef();
    *KeyMutex = m_KeyMutex;   (*KeyMutex)->AddRef();

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN OUTPUTMANAGER::UpdateApplicationWindow() {
    // Only save when workers have announced a new serial
    const uint64_t cur = GlobalFrameSerial().load(std::memory_order_acquire);
    if (cur == 0 || cur == m_LastSavedSerial) return DUPL_RETURN_SUCCESS;

    // Acquire as reader: UI uses key 0 per original protocol
    HRESULT hr = m_KeyMutex->AcquireSync(0, 1000);
    if (hr == WAIT_TIMEOUT) return DUPL_RETURN_SUCCESS;
    if (FAILED(hr)) return ProcessFailure(m_Device, L"AcquireSync(0)", L"Error", hr);

    // Copy shared ? staging
    m_Context->CopyResource(m_Staging, m_SharedSurf);

    // Release to writers (key 1)
    m_KeyMutex->ReleaseSync(1);

    // Save to JPEG
    DUPL_RETURN ret = SaveSharedSurfaceToJpeg();
    if (ret == DUPL_RETURN_SUCCESS) { m_LastSavedSerial = cur; }
    return ret;
}

static inline void BGRAtoRGBA(uint8_t* dst, const uint8_t* src, int n) {
    for (int i = 0; i < n; i += 4) { dst[i + 0] = src[i + 2]; dst[i + 1] = src[i + 1]; dst[i + 2] = src[i + 0]; dst[i + 3] = src[i + 3]; }
}

DUPL_RETURN OUTPUTMANAGER::SaveSharedSurfaceToJpeg() {
    D3D11_TEXTURE2D_DESC sd{}; m_Staging->GetDesc(&sd);
    D3D11_MAPPED_SUBRESOURCE map{};
    HRESULT hr = m_Context->Map(m_Staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) return ProcessFailure(m_Device, L"Map(staging)", L"Error", hr);

    const uint8_t* src = reinterpret_cast<const uint8_t*>(map.pData);
    const int w = static_cast<int>(sd.Width);
    const int h = static_cast<int>(sd.Height);

    // Create tightly packed RGBA buffer (QImage::Format_RGBA8888)
    std::unique_ptr<uint8_t[]> rgba(new uint8_t[w * h * 4]);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + y * map.RowPitch;
        BGRAtoRGBA(rgba.get() + y * w * 4, row, w * 4);
    }

    m_Context->Unmap(m_Staging, 0);

    QImage img(rgba.get(), w, h, QImage::Format_RGBA8888);
    // filename counter
    m_SaveCounter += 1;
    QString name = QString::number(m_SaveCounter) + ".jpg";
    if (!img.save(name, "JPG")) {
        return ProcessFailure(nullptr, L"QImage::save failed", L"Error", E_FAIL);
    }
    return DUPL_RETURN_SUCCESS;
}