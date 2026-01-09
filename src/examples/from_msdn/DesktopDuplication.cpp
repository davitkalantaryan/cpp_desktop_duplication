// =============================
// File: DesktopDuplication.cpp
// =============================
#include "CommonTypes.h"
#include "OutputManager.h"
#include "ThreadManager.h"
#include <vector>

static RECT GetVirtualDesktopBounds() {
    RECT r{ 0,0,0,0 };
    // Query all outputs via DXGI
    CComPtr<IDXGIFactory1> f; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    LONG minL = 0, minT = 0, maxR = 0, maxB = 0; bool first = true;
    for (UINT a = 0;; ++a) {
        CComPtr<IDXGIAdapter> ad; if (f->EnumAdapters(a, &ad) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0;; ++o) {
            CComPtr<IDXGIOutput> out; if (ad->EnumOutputs(o, &out) == DXGI_ERROR_NOT_FOUND) break; DXGI_OUTPUT_DESC d{}; out->GetDesc(&d);
            if (first) { minL = d.DesktopCoordinates.left; minT = d.DesktopCoordinates.top; maxR = d.DesktopCoordinates.right; maxB = d.DesktopCoordinates.bottom; first = false; }
            else { if (d.DesktopCoordinates.left < minL) minL = d.DesktopCoordinates.left; if (d.DesktopCoordinates.top < minT) minT = d.DesktopCoordinates.top; if (d.DesktopCoordinates.right > maxR) maxR = d.DesktopCoordinates.right; if (d.DesktopCoordinates.bottom > maxB) maxB = d.DesktopCoordinates.bottom; }
        }
    }
    r.left = minL; r.top = minT; r.right = maxR; r.bottom = maxB; return r;
}

int main(int a_argc, char* a_argv[]) {

    (void)a_argc;
    (void)a_argv;

    RECT desk = GetVirtualDesktopBounds();

    OUTPUTMANAGER out;
    HANDLE sharedHandle = nullptr; ID3D11Texture2D* sharedSurf = nullptr; IDXGIKeyedMutex* km = nullptr;
    if (out.InitOutput(desk, &sharedHandle, &sharedSurf, &km) != DUPL_RETURN_SUCCESS) return -1;

    // Spawn capture threads (one per output)
    // Count outputs
    CComPtr<IDXGIFactory1> f; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    UINT outputCount = 0; for (UINT a = 0;; ++a) { CComPtr<IDXGIAdapter> ad; if (f->EnumAdapters(a, &ad) == DXGI_ERROR_NOT_FOUND) break; for (UINT o = 0;; ++o) { CComPtr<IDXGIOutput> outp; if (ad->EnumOutputs(o, &outp) == DXGI_ERROR_NOT_FOUND) break; ++outputCount; } }

    THREADMANAGER tm; if (tm.Initialize(sharedHandle, outputCount) != DUPL_RETURN_SUCCESS) return -2;

    // Main loop: poll and save new frames
    for (;;) {
        out.UpdateApplicationWindow();
        Sleep(1);
    }

    tm.WaitForThreadTermination();
    return 0;
}


//
// Displays a message
//
void DisplayMsg(_In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        MessageBoxW(nullptr, Str, Title, MB_OK);
        return;
    }

    const UINT StringLen = (UINT)(wcslen(Str) + sizeof(" with HRESULT 0x########."));
    wchar_t* OutStr = new wchar_t[StringLen];
    if (!OutStr)
    {
        return;
    }

    INT LenWritten = swprintf_s(OutStr, StringLen, L"%s with 0x%X.", Str, hr);
    if (LenWritten != -1)
    {
        MessageBoxW(nullptr, OutStr, Title, MB_OK);
    }

    delete [] OutStr;
}


_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors)
{
    HRESULT TranslatedHr;

    // On an error check if the DX device is lost
    if (Device)
    {
        HRESULT DeviceRemovedReason = Device->GetDeviceRemovedReason();

        switch (DeviceRemovedReason)
        {
        case DXGI_ERROR_DEVICE_REMOVED :
        case DXGI_ERROR_DEVICE_RESET :
        case static_cast<HRESULT>(E_OUTOFMEMORY) :
        {
            // Our device has been stopped due to an external event on the GPU so map them all to
            // device removed and continue processing the condition
            TranslatedHr = DXGI_ERROR_DEVICE_REMOVED;
            break;
        }

        case S_OK :
        {
            // Device is not removed so use original error
            TranslatedHr = hr;
            break;
        }

        default :
        {
            // Device is removed but not a error we want to remap
            TranslatedHr = DeviceRemovedReason;
        }
        }
    }
    else
    {
        TranslatedHr = hr;
    }

    // Check if this error was expected or not
    if (ExpectedErrors)
    {
        HRESULT* CurrentResult = ExpectedErrors;

        while (*CurrentResult != S_OK)
        {
            if (*(CurrentResult++) == TranslatedHr)
            {
                return DUPL_RETURN_ERROR_EXPECTED;
            }
        }
    }

    // Error was not expected so display the message box
    DisplayMsg(Str, Title, TranslatedHr);

    return DUPL_RETURN_ERROR_UNEXPECTED;
}
