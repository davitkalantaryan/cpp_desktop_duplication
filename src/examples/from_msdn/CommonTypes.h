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
    POINT Position{ 0,0 };
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
    static std::atomic<uint64_t> s{ 0 };
    return s;
}

// Utility: release COM
template<typename T> inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Utility: translate failures
//DUPL_RETURN ProcessFailure(ID3D11Device* Dev, const wchar_t* Str, const wchar_t* Title, HRESULT hr, const HRESULT* Expected = nullptr);
_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors=nullptr);
