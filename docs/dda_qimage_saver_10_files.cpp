// PSEUDOCODE PLAN
// 1) Keep original pipeline (threads composite into a shared D3D11 texture via keyed mutex).
// 2) Minimal changes:
//    - Add a global atomic frame serial. Workers bump it after composing a frame.
//    - OutputManager::UpdateApplicationWindow becomes “SaveIfNewFrame”:
//         * Acquire keyed mutex as reader (key 0), copy shared texture → CPU staging, release (key 1).
//         * Convert BGRA8 → QImage and save to incrementing filename (1.jpg, 2.jpg, ...).
//    - No window/swapchain/HLSL usage in OutputManager anymore.
//    - Keep the rest of the files mostly intact (stubs maintained where needed).
// 3) Advantages: minimal churn, no HLSL dependency for saving; workers still free to use existing compositing.
// 4) Notes: this saves the *full virtual desktop* each time any output produces a new frame.

// =============================
// File: CommonTypes.h
// =============================
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <atlbase.h>
#include <stdint.h>
#include <atomic>

// Error code used across sample
enum DUPL_RETURN {
    DUPL_RETURN_SUCCESS = 0,
    DUPL_RETURN_ERROR_EXPECTED,
    DUPL_RETURN_ERROR_UNEXPECTED
};

// Forward decls
struct DX_RESOURCES {
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
};

struct FRAME_DATA {
    ID3D11Texture2D* Frame = nullptr; // acquired frame
    DXGI_OUTDUPL_FRAME_INFO FrameInfo{};
};

// Shared pointer info (kept to minimize changes)
struct PTR_INFO {
    BOOL Visible = FALSE;
    POINT Position{0,0};
};

// Passed to worker thread
struct THREAD_DATA {
    HANDLE TerminateThreadsEvent = nullptr;
    UINT Output = 0;
    LONG OffsetX = 0;
    LONG OffsetY = 0;
    DX_RESOURCES DxRes{};
    HANDLE SharedHandle = nullptr; // shared surface handle
};

// Global frame serial signaled by workers when they commit to shared surface
inline std::atomic<uint64_t>& GlobalFrameSerial() {
    static std::atomic<uint64_t> s{0};
    return s;
}

// Utility: release COM
template<typename T> inline void SafeRelease(T*& p){ if(p){ p->Release(); p=nullptr; } }

// Utility: translate failures
DUPL_RETURN ProcessFailure(ID3D11Device* Dev, const wchar_t* Str, const wchar_t* Title, HRESULT hr, const HRESULT* Expected = nullptr);

// =============================
// File: DuplicationManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class DUPLICATIONMANAGER {
public:
    DUPLICATIONMANAGER() = default;
    ~DUPLICATIONMANAGER();

    DUPL_RETURN InitDupl(ID3D11Device* Device, UINT Output);
    void GetOutputDesc(DXGI_OUTPUT_DESC* Desc) const { *Desc = m_OutputDesc; }

    DUPL_RETURN GetFrame(FRAME_DATA* Data, bool* Timeout);
    void DoneWithFrame();

private:
    CComPtr<IDXGIOutputDuplication> m_DeskDupl;
    DXGI_OUTPUT_DESC m_OutputDesc{};
};

// =============================
// File: DuplicationManager.cpp
// =============================
#include "DuplicationManager.h"

DUPLICATIONMANAGER::~DUPLICATIONMANAGER(){ }

DUPL_RETURN DUPLICATIONMANAGER::InitDupl(ID3D11Device* Device, UINT Output){
    CComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&DxgiDevice);
    if(FAILED(hr)) return ProcessFailure(Device, L"QITable IDXGIDevice", L"Error", hr);

    CComPtr<IDXGIAdapter> DxgiAdapter; hr = DxgiDevice->GetAdapter(&DxgiAdapter);
    if(FAILED(hr)) return ProcessFailure(Device, L"GetAdapter", L"Error", hr);

    CComPtr<IDXGIOutput> DxgiOutput; hr = DxgiAdapter->EnumOutputs(Output, &DxgiOutput);
    if(FAILED(hr)) return ProcessFailure(Device, L"EnumOutputs", L"Error", hr);

    DxgiOutput->GetDesc(&m_OutputDesc);

    CComPtr<IDXGIOutput1> DxgiOutput1; hr = DxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&DxgiOutput1);
    if(FAILED(hr)) return ProcessFailure(Device, L"QITable IDXGIOutput1", L"Error", hr);

    hr = DxgiOutput1->DuplicateOutput(Device, &m_DeskDupl);
    if(FAILED(hr)) return ProcessFailure(Device, L"DuplicateOutput", L"Error", hr);

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN DUPLICATIONMANAGER::GetFrame(FRAME_DATA* Data, bool* Timeout){
    if(!m_DeskDupl) return DUPL_RETURN_ERROR_UNEXPECTED;
    DXGI_OUTDUPL_FRAME_INFO fi{};
    CComPtr<IDXGIResource> DesktopResource;
    HRESULT hr = m_DeskDupl->AcquireNextFrame(100, &fi, &DesktopResource);
    if(hr == DXGI_ERROR_WAIT_TIMEOUT){ *Timeout = true; return DUPL_RETURN_SUCCESS; }
    if(FAILED(hr)) return ProcessFailure(nullptr, L"AcquireNextFrame", L"Error", hr);
    *Timeout = false;
    Data->FrameInfo = fi;
    DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Data->Frame);
    return DUPL_RETURN_SUCCESS;
}

void DUPLICATIONMANAGER::DoneWithFrame(){ if(m_DeskDupl) m_DeskDupl->ReleaseFrame(); }

// =============================
// File: DisplayManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class DISPLAYMANAGER{
public:
    DISPLAYMANAGER() = default;
    ~DISPLAYMANAGER() = default;

    // Copy full frame into the shared surface at (OffsetX, OffsetY)
    DUPL_RETURN ProcessFrame(_In_ FRAME_DATA* Frame, _In_ ID3D11Texture2D* SharedSurf, LONG OffsetX, LONG OffsetY);
};

// =============================
// File: DisplayManager.cpp
// =============================
#include "DisplayManager.h"

DUPL_RETURN DISPLAYMANAGER::ProcessFrame(_In_ FRAME_DATA* Frame, _In_ ID3D11Texture2D* SharedSurf, LONG OffsetX, LONG OffsetY){
    if(!Frame->Frame || !SharedSurf) return DUPL_RETURN_ERROR_UNEXPECTED;

    // Query dims
    D3D11_TEXTURE2D_DESC srcDesc{}, dstDesc{};
    Frame->Frame->GetDesc(&srcDesc);
    SharedSurf->GetDesc(&dstDesc);

    // Destination box (full source size) at given offset
    D3D11_BOX srcBox{}; srcBox.left=0; srcBox.top=0; srcBox.front=0; srcBox.right=srcDesc.Width; srcBox.bottom=srcDesc.Height; srcBox.back=1;

    // Copy
    CComPtr<ID3D11Device> dev; Frame->Frame->GetDevice(&dev);
    CComPtr<ID3D11DeviceContext> ctx; dev->GetImmediateContext(&ctx);
    ctx->CopySubresourceRegion(SharedSurf, 0, OffsetX, OffsetY, 0, Frame->Frame, 0, &srcBox);

    return DUPL_RETURN_SUCCESS;
}

// =============================
// File: OutputManager.h
// =============================
#pragma once
#include "CommonTypes.h"
#include <string>

class OUTPUTMANAGER{
public:
    OUTPUTMANAGER();
    ~OUTPUTMANAGER();

    // Initializes device and shared surface (full virtual desktop bounds supplied)
    DUPL_RETURN InitOutput(RECT DeskBounds, _Out_ HANDLE* SharedHandle, _Out_ ID3D11Texture2D** SharedSurf, _Out_ IDXGIKeyedMutex** KeyMutex);

    // Called frequently: if a new frame serial is observed, save the shared surface to an incrementing JPEG
    DUPL_RETURN UpdateApplicationWindow();

    void CleanRefs();

private:
    DUPL_RETURN SaveSharedSurfaceToJpeg();

    // D3D objects
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    // Shared desktop surface + mutex
    ID3D11Texture2D* m_SharedSurf = nullptr;
    IDXGIKeyedMutex* m_KeyMutex = nullptr;

    // CPU staging texture for readback
    ID3D11Texture2D* m_Staging = nullptr;

    // Virtual desktop bounds
    RECT m_DeskBounds{0,0,0,0};

    // Saved frame tracking
    uint64_t m_LastSavedSerial = 0;
    uint64_t m_SaveCounter = 0; // 1.jpg, 2.jpg, ...
};

// =============================
// File: OutputManager.cpp
// =============================
#include "OutputManager.h"
#include <QImage>
#include <QString>

OUTPUTMANAGER::OUTPUTMANAGER(){}
OUTPUTMANAGER::~OUTPUTMANAGER(){ CleanRefs(); }

void OUTPUTMANAGER::CleanRefs(){
    SafeRelease(m_Staging);
    SafeRelease(m_KeyMutex);
    SafeRelease(m_SharedSurf);
    SafeRelease(m_Context);
    SafeRelease(m_Device);
}

DUPL_RETURN OUTPUTMANAGER::InitOutput(RECT DeskBounds, _Out_ HANDLE* SharedHandle, _Out_ ID3D11Texture2D** SharedSurf, _Out_ IDXGIKeyedMutex** KeyMutex){
    m_DeskBounds = DeskBounds;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, nullptr, 0, D3D11_SDK_VERSION, &m_Device, &fl, &m_Context);
    if(FAILED(hr)) return ProcessFailure(nullptr, L"D3D11CreateDevice", L"Error", hr);

    // Create shared surface BGRA8 with keyed mutex
    const UINT width  = m_DeskBounds.right - m_DeskBounds.left;
    const UINT height = m_DeskBounds.bottom - m_DeskBounds.top;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width; td.Height = height;
    td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc = {1,0};
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.CPUAccessFlags = 0; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr2 = m_Device->CreateTexture2D(&td, nullptr, &m_SharedSurf);
    if(FAILED(hr2)) return ProcessFailure(m_Device, L"CreateTexture2D shared", L"Error", hr2);

    // Keyed mutex
    hr2 = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&m_KeyMutex);
    if(FAILED(hr2)) return ProcessFailure(m_Device, L"QITable IDXGIKeyedMutex", L"Error", hr2);

    // Shared handle to open in worker devices
    CComPtr<IDXGIResource> dxgiRes; m_SharedSurf->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiRes);
    dxgiRes->GetSharedHandle(SharedHandle);

    // Staging texture for CPU readback
    D3D11_TEXTURE2D_DESC sd = td; sd.BindFlags=0; sd.MiscFlags=0; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr2 = m_Device->CreateTexture2D(&sd, nullptr, &m_Staging);
    if(FAILED(hr2)) return ProcessFailure(m_Device, L"CreateTexture2D staging", L"Error", hr2);

    *SharedSurf = m_SharedSurf; (*SharedSurf)->AddRef();
    *KeyMutex   = m_KeyMutex;   (*KeyMutex)->AddRef();

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN OUTPUTMANAGER::UpdateApplicationWindow(){
    // Only save when workers have announced a new serial
    const uint64_t cur = GlobalFrameSerial().load(std::memory_order_acquire);
    if(cur == 0 || cur == m_LastSavedSerial) return DUPL_RETURN_SUCCESS;

    // Acquire as reader: UI uses key 0 per original protocol
    HRESULT hr = m_KeyMutex->AcquireSync(0, 1000);
    if(hr == WAIT_TIMEOUT) return DUPL_RETURN_SUCCESS;
    if(FAILED(hr)) return ProcessFailure(m_Device, L"AcquireSync(0)", L"Error", hr);

    // Copy shared → staging
    m_Context->CopyResource(m_Staging, m_SharedSurf);

    // Release to writers (key 1)
    m_KeyMutex->ReleaseSync(1);

    // Save to JPEG
    DUPL_RETURN ret = SaveSharedSurfaceToJpeg();
    if(ret == DUPL_RETURN_SUCCESS){ m_LastSavedSerial = cur; }
    return ret;
}

static inline void BGRAtoRGBA(uint8_t* dst, const uint8_t* src, int n){
    for(int i=0;i<n;i+=4){ dst[i+0]=src[i+2]; dst[i+1]=src[i+1]; dst[i+2]=src[i+0]; dst[i+3]=src[i+3]; }
}

DUPL_RETURN OUTPUTMANAGER::SaveSharedSurfaceToJpeg(){
    D3D11_TEXTURE2D_DESC sd{}; m_Staging->GetDesc(&sd);
    D3D11_MAPPED_SUBRESOURCE map{};
    HRESULT hr = m_Context->Map(m_Staging, 0, D3D11_MAP_READ, 0, &map);
    if(FAILED(hr)) return ProcessFailure(m_Device, L"Map(staging)", L"Error", hr);

    const uint8_t* src = reinterpret_cast<const uint8_t*>(map.pData);
    const int w = static_cast<int>(sd.Width);
    const int h = static_cast<int>(sd.Height);

    // Create tightly packed RGBA buffer (QImage::Format_RGBA8888)
    std::unique_ptr<uint8_t[]> rgba(new uint8_t[w*h*4]);
    for(int y=0; y<h; ++y){
        const uint8_t* row = src + y*map.RowPitch;
        BGRAtoRGBA(rgba.get()+y*w*4, row, w*4);
    }

    m_Context->Unmap(m_Staging, 0);

    QImage img(rgba.get(), w, h, QImage::Format_RGBA8888);
    // filename counter
    m_SaveCounter += 1; 
    QString name = QString::number(m_SaveCounter) + ".jpg";
    if(!img.save(name, "JPG")){
        return ProcessFailure(nullptr, L"QImage::save failed", L"Error", E_FAIL);
    }
    return DUPL_RETURN_SUCCESS;
}

// =============================
// File: ThreadManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class THREADMANAGER{
public:
    THREADMANAGER();
    ~THREADMANAGER();

    DUPL_RETURN Initialize(_In_ HANDLE SharedHandle, _In_ UINT OutputCount);
    void WaitForThreadTermination();

private:
    static DWORD WINAPI DDProc(_In_ void* Param);

    UINT m_ThreadCount = 0;
    HANDLE* m_ThreadHandles = nullptr;
    THREAD_DATA* m_ThreadData = nullptr;
};

// =============================
// File: ThreadManager.cpp
// =============================
#include "ThreadManager.h"
#include "DuplicationManager.h"
#include "DisplayManager.h"

DWORD WINAPI THREADMANAGER::DDProc(_In_ void* Param){
    THREAD_DATA* T = reinterpret_cast<THREAD_DATA*>(Param);

    // Open shared surface on this device
    CComPtr<ID3D11Device> dev = T->DxRes.Device;
    CComPtr<ID3D11DeviceContext> ctx = T->DxRes.Context;

    CComPtr<ID3D11Texture2D> shared;
    HRESULT hr = dev->OpenSharedResource(T->SharedHandle, __uuidof(ID3D11Texture2D), (void**)&shared);
    if(FAILED(hr)) return 0;
    CComPtr<IDXGIKeyedMutex> km; shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km);

    DUPLICATIONMANAGER dupl; if(dupl.InitDupl(dev, T->Output) != DUPL_RETURN_SUCCESS) return 0;
    DXGI_OUTPUT_DESC outDesc{}; dupl.GetOutputDesc(&outDesc);

    DISPLAYMANAGER disp;

    FRAME_DATA fd{};
    bool waitMore = false;

    while(WaitForSingleObject(T->TerminateThreadsEvent, 0) == WAIT_TIMEOUT){
        bool timeout=false;
        DUPL_RETURN r = dupl.GetFrame(&fd, &timeout);
        if(r != DUPL_RETURN_SUCCESS){ break; }
        if(timeout){ continue; }

        // Writers use key 1 per original protocol
        hr = km->AcquireSync(1, 1000);
        if(SUCCEEDED(hr)){
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

THREADMANAGER::THREADMANAGER(){}
THREADMANAGER::~THREADMANAGER(){ WaitForThreadTermination(); if(m_ThreadHandles){ delete[] m_ThreadHandles; } if(m_ThreadData){ delete[] m_ThreadData; } }

DUPL_RETURN THREADMANAGER::Initialize(_In_ HANDLE SharedHandle, _In_ UINT OutputCount){
    m_ThreadCount = OutputCount;
    m_ThreadHandles = new HANDLE[m_ThreadCount]{};
    m_ThreadData = new THREAD_DATA[m_ThreadCount]{};

    for(UINT i=0;i<OutputCount;++i){
        // Create per-thread device
        D3D_FEATURE_LEVEL fl; ID3D11Device* dev=nullptr; ID3D11DeviceContext* ctx=nullptr;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if(FAILED(hr)) return ProcessFailure(nullptr, L"D3D11CreateDevice (thread)", L"Error", hr);

        m_ThreadData[i].DxRes.Device = dev;
        m_ThreadData[i].DxRes.Context = ctx;
        m_ThreadData[i].Output = i;
        m_ThreadData[i].SharedHandle = SharedHandle;
        m_ThreadData[i].TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        m_ThreadHandles[i] = CreateThread(nullptr, 0, DDProc, &m_ThreadData[i], 0, nullptr);
    }
    return DUPL_RETURN_SUCCESS;
}

void THREADMANAGER::WaitForThreadTermination(){
    if(!m_ThreadHandles) return;
    WaitForMultipleObjects(m_ThreadCount, m_ThreadHandles, TRUE, INFINITE);
    for(UINT i=0;i<m_ThreadCount;++i){
        if(m_ThreadData[i].DxRes.Context) m_ThreadData[i].DxRes.Context->Release();
        if(m_ThreadData[i].DxRes.Device) m_ThreadData[i].DxRes.Device->Release();
        if(m_ThreadData[i].TerminateThreadsEvent) CloseHandle(m_ThreadData[i].TerminateThreadsEvent);
        CloseHandle(m_ThreadHandles[i]);
    }
}

// =============================
// File: DisplayManager.* (already provided)
// =============================

// =============================
// File: DesktopDuplication.cpp
// =============================
#include "CommonTypes.h"
#include "OutputManager.h"
#include "ThreadManager.h"
#include <vector>

static RECT GetVirtualDesktopBounds(){
    RECT r{0,0,0,0};
    // Query all outputs via DXGI
    CComPtr<IDXGIFactory1> f; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    LONG minL=0, minT=0, maxR=0, maxB=0; bool first=true;
    for(UINT a=0;;++a){
        CComPtr<IDXGIAdapter> ad; if(f->EnumAdapters(a,&ad)==DXGI_ERROR_NOT_FOUND) break;
        for(UINT o=0;;++o){ CComPtr<IDXGIOutput> out; if(ad->EnumOutputs(o,&out)==DXGI_ERROR_NOT_FOUND) break; DXGI_OUTPUT_DESC d{}; out->GetDesc(&d);
            if(first){ minL=d.DesktopCoordinates.left; minT=d.DesktopCoordinates.top; maxR=d.DesktopCoordinates.right; maxB=d.DesktopCoordinates.bottom; first=false; }
            else{ if(d.DesktopCoordinates.left<minL) minL=d.DesktopCoordinates.left; if(d.DesktopCoordinates.top<minT) minT=d.DesktopCoordinates.top; if(d.DesktopCoordinates.right>maxR) maxR=d.DesktopCoordinates.right; if(d.DesktopCoordinates.bottom>maxB) maxB=d.DesktopCoordinates.bottom; }
        }
    }
    r.left=minL; r.top=minT; r.right=maxR; r.bottom=maxB; return r;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int){
    RECT desk = GetVirtualDesktopBounds();

    OUTPUTMANAGER out;
    HANDLE sharedHandle=nullptr; ID3D11Texture2D* sharedSurf=nullptr; IDXGIKeyedMutex* km=nullptr;
    if(out.InitOutput(desk, &sharedHandle, &sharedSurf, &km) != DUPL_RETURN_SUCCESS) return -1;

    // Spawn capture threads (one per output)
    // Count outputs
    CComPtr<IDXGIFactory1> f; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    UINT outputCount=0; for(UINT a=0;;++a){ CComPtr<IDXGIAdapter> ad; if(f->EnumAdapters(a,&ad)==DXGI_ERROR_NOT_FOUND) break; for(UINT o=0;;++o){ CComPtr<IDXGIOutput> outp; if(ad->EnumOutputs(o,&outp)==DXGI_ERROR_NOT_FOUND) break; ++outputCount; } }

    THREADMANAGER tm; if(tm.Initialize(sharedHandle, outputCount) != DUPL_RETURN_SUCCESS) return -2;

    // Main loop: poll and save new frames
    for(;;){
        out.UpdateApplicationWindow();
        Sleep(1);
    }

    tm.WaitForThreadTermination();
    return 0;
}

// =============================
// File: ThreadManager.h/cpp provided above
// =============================

// =============================
// File: OutputManager.h/cpp provided above
// =============================

// =============================
// NOTE: HLSL files no longer used by this saver path.
// =============================
