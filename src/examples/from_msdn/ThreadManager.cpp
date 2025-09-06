// =============================
// File: ThreadManager.cpp
// =============================
#include "ThreadManager.h"
#include "DuplicationManager.h"
#include "DisplayManager.h"

DWORD WINAPI THREADMANAGER::DDProc(_In_ void* Param) {
    THREAD_DATA* T = reinterpret_cast<THREAD_DATA*>(Param);

    // Open shared surface on this device
    CComPtr<ID3D11Device> dev = T->DxRes.Device;
    CComPtr<ID3D11DeviceContext> ctx = T->DxRes.Context;

    CComPtr<ID3D11Texture2D> shared;
    HRESULT hr = dev->OpenSharedResource(T->SharedHandle, __uuidof(ID3D11Texture2D), (void**)&shared);
    if (FAILED(hr)) return 0;
    CComPtr<IDXGIKeyedMutex> km; shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km);

    DUPLICATIONMANAGER dupl; if (dupl.InitDupl(dev, T->Output) != DUPL_RETURN_SUCCESS) return 0;
    DXGI_OUTPUT_DESC outDesc{}; dupl.GetOutputDesc(&outDesc);

    DISPLAYMANAGER disp;

    FRAME_DATA fd{};
    //bool waitMore = false;

    while (WaitForSingleObject(T->TerminateThreadsEvent, 0) == WAIT_TIMEOUT) {
        bool timeout = false;
        DUPL_RETURN r = dupl.GetFrame(&fd, &timeout);
        if (r != DUPL_RETURN_SUCCESS) { break; }
        if (timeout) { continue; }

        // Writers use key 1 per original protocol
        hr = km->AcquireSync(1, 1000);
        if (SUCCEEDED(hr)) {
            disp.ProcessFrame(&fd, shared, T->OffsetX, T->OffsetY);
            km->ReleaseSync(0);
            // announce new frame
            GlobalFrameSerial().fetch_add(1, std::memory_order_acq_rel);
        }
        dupl.DoneWithFrame();
        SafeRelease(fd.Frame);
    }
    return 0;
}

THREADMANAGER::THREADMANAGER() {}
THREADMANAGER::~THREADMANAGER() { WaitForThreadTermination(); if (m_ThreadHandles) { delete[] m_ThreadHandles; } if (m_ThreadData) { delete[] m_ThreadData; } }

DUPL_RETURN THREADMANAGER::Initialize(_In_ HANDLE SharedHandle, _In_ UINT OutputCount) {
    m_ThreadCount = OutputCount;
    m_ThreadHandles = new HANDLE[m_ThreadCount]{};
    m_ThreadData = new THREAD_DATA[m_ThreadCount]{};

    for (UINT i = 0; i < OutputCount; ++i) {
        // Create per-thread device
        D3D_FEATURE_LEVEL fl; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if (FAILED(hr)) return ProcessFailure(nullptr, L"D3D11CreateDevice (thread)", L"Error", hr);

        m_ThreadData[i].DxRes.Device = dev;
        m_ThreadData[i].DxRes.Context = ctx;
        m_ThreadData[i].Output = i;
        m_ThreadData[i].SharedHandle = SharedHandle;
        m_ThreadData[i].TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        m_ThreadHandles[i] = CreateThread(nullptr, 0, DDProc, &m_ThreadData[i], 0, nullptr);
    }
    return DUPL_RETURN_SUCCESS;
}

void THREADMANAGER::WaitForThreadTermination() {
    if (!m_ThreadHandles) return;
    WaitForMultipleObjects(m_ThreadCount, m_ThreadHandles, TRUE, INFINITE);
    for (UINT i = 0; i < m_ThreadCount; ++i) {
        if (m_ThreadData[i].DxRes.Context) m_ThreadData[i].DxRes.Context->Release();
        if (m_ThreadData[i].DxRes.Device) m_ThreadData[i].DxRes.Device->Release();
        if (m_ThreadData[i].TerminateThreadsEvent) CloseHandle(m_ThreadData[i].TerminateThreadsEvent);
        CloseHandle(m_ThreadHandles[i]);
    }
}
