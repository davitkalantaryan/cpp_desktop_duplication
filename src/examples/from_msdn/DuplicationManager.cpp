// =============================
// File: DuplicationManager.cpp
// =============================
#include "DuplicationManager.h"

DUPLICATIONMANAGER::~DUPLICATIONMANAGER() {}

DUPL_RETURN DUPLICATIONMANAGER::InitDupl(ID3D11Device* Device, UINT Output) {
    CComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&DxgiDevice);
    if (FAILED(hr)) return ProcessFailure(Device, L"QITable IDXGIDevice", L"Error", hr);

    CComPtr<IDXGIAdapter> DxgiAdapter; hr = DxgiDevice->GetAdapter(&DxgiAdapter);
    if (FAILED(hr)) return ProcessFailure(Device, L"GetAdapter", L"Error", hr);

    CComPtr<IDXGIOutput> DxgiOutput; hr = DxgiAdapter->EnumOutputs(Output, &DxgiOutput);
    if (FAILED(hr)) return ProcessFailure(Device, L"EnumOutputs", L"Error", hr);

    DxgiOutput->GetDesc(&m_OutputDesc);

    CComPtr<IDXGIOutput1> DxgiOutput1; hr = DxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&DxgiOutput1);
    if (FAILED(hr)) return ProcessFailure(Device, L"QITable IDXGIOutput1", L"Error", hr);

    hr = DxgiOutput1->DuplicateOutput(Device, &m_DeskDupl);
    if (FAILED(hr)) return ProcessFailure(Device, L"DuplicateOutput", L"Error", hr);

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN DUPLICATIONMANAGER::GetFrame(FRAME_DATA* Data, bool* Timeout) {
    if (!m_DeskDupl) return DUPL_RETURN_ERROR_UNEXPECTED;
    DXGI_OUTDUPL_FRAME_INFO fi{};
    CComPtr<IDXGIResource> DesktopResource;
    HRESULT hr = m_DeskDupl->AcquireNextFrame(100, &fi, &DesktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) { *Timeout = true; return DUPL_RETURN_SUCCESS; }
    if (FAILED(hr)) return ProcessFailure(nullptr, L"AcquireNextFrame", L"Error", hr);
    *Timeout = false;
    Data->FrameInfo = fi;
    DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Data->Frame);
    return DUPL_RETURN_SUCCESS;
}

void DUPLICATIONMANAGER::DoneWithFrame() { if (m_DeskDupl) m_DeskDupl->ReleaseFrame(); }
