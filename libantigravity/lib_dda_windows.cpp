#include <limits.h>
#include <string>
#include <atomic>
#include <windows.h>
#include <process.h>

#include "lib_dda.h"
#include "DisplayManager.h"
#include "DuplicationManager.h"
#include "OutputManager.h"
#include "ThreadManager.h"

// Internal Globals
OUTPUTMANAGER OutMgr;
THREADMANAGER ThreadMgr;

// Events
HANDLE UnexpectedErrorEvent = nullptr;
HANDLE ExpectedErrorEvent = nullptr;
HANDLE TerminateThreadsEvent = nullptr;
HANDLE WrapperThreadHandle = nullptr;

//
// Errors (Copied from DesktopDuplication.cpp)
//
HRESULT SystemTransitionsExpectedErrors[] = {
    DXGI_ERROR_DEVICE_REMOVED,
    DXGI_ERROR_ACCESS_LOST,
    static_cast<HRESULT>(WAIT_ABANDONED),
    S_OK
};

//
// Dynamic Wait Class (Copied from DesktopDuplication.cpp)
//
typedef struct
{
    UINT    WaitTime;
    UINT    WaitCount;
}WAIT_BAND;

#define WAIT_BAND_COUNT 3
#define WAIT_BAND_STOP 0

class DYNAMIC_WAIT
{
public:
    DYNAMIC_WAIT();
    ~DYNAMIC_WAIT();
    void Wait();

private:
    static const WAIT_BAND   m_WaitBands[WAIT_BAND_COUNT];
    static const UINT       m_WaitSequenceTimeInSeconds = 2;
    UINT                    m_CurrentWaitBandIdx;
    UINT                    m_WaitCountInCurrentBand;
    LARGE_INTEGER           m_QPCFrequency;
    LARGE_INTEGER           m_LastWakeUpTime;
    BOOL                    m_QPCValid;
};

const WAIT_BAND DYNAMIC_WAIT::m_WaitBands[WAIT_BAND_COUNT] = {
    {250, 20},
    {2000, 60},
    {5000, WAIT_BAND_STOP}
};

DYNAMIC_WAIT::DYNAMIC_WAIT() : m_CurrentWaitBandIdx(0), m_WaitCountInCurrentBand(0)
{
    m_QPCValid = QueryPerformanceFrequency(&m_QPCFrequency);
    m_LastWakeUpTime.QuadPart = 0L;
}

DYNAMIC_WAIT::~DYNAMIC_WAIT() {}

void DYNAMIC_WAIT::Wait()
{
    LARGE_INTEGER CurrentQPC = { 0 };
    QueryPerformanceCounter(&CurrentQPC);
    if (m_QPCValid && (CurrentQPC.QuadPart <= (m_LastWakeUpTime.QuadPart + (m_QPCFrequency.QuadPart * m_WaitSequenceTimeInSeconds))))
    {
        if ((m_WaitBands[m_CurrentWaitBandIdx].WaitCount != WAIT_BAND_STOP) && (m_WaitCountInCurrentBand > m_WaitBands[m_CurrentWaitBandIdx].WaitCount))
        {
            m_CurrentWaitBandIdx++;
            m_WaitCountInCurrentBand = 0;
        }
    }
    else
    {
        m_WaitCountInCurrentBand = 0;
        m_CurrentWaitBandIdx = 0;
    }
    Sleep(m_WaitBands[m_CurrentWaitBandIdx].WaitTime);
    QueryPerformanceCounter(&m_LastWakeUpTime);
    m_WaitCountInCurrentBand++;
}

void DisplayMsg(_In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr)
{
    // Simplified logging/error handling for library
    // For now, suppress message boxes or log to debug output
    OutputDebugStringW(Str);
}


//
// Entry point for new duplication threads
//
DWORD WINAPI DDProc(_In_ void* Param)
{
    // Classes
    DISPLAYMANAGER DispMgr;
    DUPLICATIONMANAGER DuplMgr;

    // D3D objects
    ID3D11Texture2D* SharedSurf = nullptr;
    IDXGIKeyedMutex* KeyMutex = nullptr;

    // Data passed in from thread creation
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);

    // Get desktop
    DUPL_RETURN Ret;
    HDESK CurrentDesktop = nullptr;
    CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!CurrentDesktop)
    {
        // We do not have access to the desktop so request a retry
        SetEvent(TData->ExpectedErrorEvent);
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // Attach desktop to this thread
    bool DesktopAttached; DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
    CloseDesktop(CurrentDesktop);
    CurrentDesktop = nullptr;
    if (!DesktopAttached)
    {
        // We do not have access to the desktop so request a retry
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // New display manager
    DispMgr.InitD3D(&TData->DxRes);

    // Obtain handle to sync shared Surface
    HRESULT hr; hr = TData->DxRes.Device->OpenSharedResource(TData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&SharedSurf));
    if (FAILED (hr))
    {
        Ret = ProcessFailure(TData->DxRes.Device, L"Opening shared texture failed", L"Error", hr, SystemTransitionsExpectedErrors);
        goto Exit;
    }

    hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&KeyMutex));
    if (FAILED(hr))
    {
        Ret = ProcessFailure(nullptr, L"Failed to get keyed mutex interface in spawned thread", L"Error", hr);
        goto Exit;
    }

    // Make duplication manager
    Ret = DuplMgr.InitDupl(TData->DxRes.Device, TData->Output);
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        goto Exit;
    }

    // Get output description
    DXGI_OUTPUT_DESC DesktopDesc;
    RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
    DuplMgr.GetOutputDesc(&DesktopDesc);

    // Main duplication loop
    bool WaitToProcessCurrentFrame; WaitToProcessCurrentFrame = false;
    FRAME_DATA CurrentData;

    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT))
    {
        if (!WaitToProcessCurrentFrame)
        {
            // Get new frame from desktop duplication
            bool TimeOut;
            Ret = DuplMgr.GetFrame(&CurrentData, &TimeOut);
            if (Ret != DUPL_RETURN_SUCCESS)
            {
                // An error occurred getting the next frame drop out of loop which
                // will check if it was expected or not
                break;
            }

            // Check for timeout
            if (TimeOut)
            {
                // No new frame at the moment
                continue;
            }
        }

        // We have a new frame so try and process it
        // Try to acquire keyed mutex in order to access shared surface
        hr = KeyMutex->AcquireSync(0, 1000);
        if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
        {
            // Can't use shared surface right now, try again later
            WaitToProcessCurrentFrame = true;
            continue;
        }
        else if (FAILED(hr))
        {
            // Generic unknown failure
            Ret = ProcessFailure(TData->DxRes.Device, L"Unexpected error acquiring KeyMutex", L"Error", hr, SystemTransitionsExpectedErrors);
            DuplMgr.DoneWithFrame();
            break;
        }

        // We can now process the current frame
        WaitToProcessCurrentFrame = false;

        // Get mouse info
        Ret = DuplMgr.GetMouse(TData->PtrInfo, &(CurrentData.FrameInfo), TData->OffsetX, TData->OffsetY);
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            DuplMgr.DoneWithFrame();
            KeyMutex->ReleaseSync(1);
            break;
        }

        // Process new frame
        Ret = DispMgr.ProcessFrame(&CurrentData, SharedSurf, TData->OffsetX, TData->OffsetY, &DesktopDesc);
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            DuplMgr.DoneWithFrame();
            KeyMutex->ReleaseSync(1);
            break;
        }

        
        // Release acquired keyed mutex
        hr = KeyMutex->ReleaseSync(1);
        if (FAILED(hr))
        {
            Ret = ProcessFailure(TData->DxRes.Device, L"Unexpected error releasing the keyed mutex", L"Error", hr, SystemTransitionsExpectedErrors);
            DuplMgr.DoneWithFrame();
            break;
        }

        // Release frame back to desktop duplication
        Ret = DuplMgr.DoneWithFrame();
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            break;
        }
    }

Exit:
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        if (Ret == DUPL_RETURN_ERROR_EXPECTED)
        {
            // The system is in a transition state so request the duplication be restarted
            SetEvent(TData->ExpectedErrorEvent);
        }
        else
        {
            // Unexpected error so exit the application
            SetEvent(TData->UnexpectedErrorEvent);
        }
    }

    if (SharedSurf)
    {
        SharedSurf->Release();
        SharedSurf = nullptr;
    }

    if (KeyMutex)
    {
        KeyMutex->Release();
        KeyMutex = nullptr;
    }

    return 0;
}

// These are the errors we expect from IDXGIOutput1::DuplicateOutput due to a transition
HRESULT CreateDuplicationExpectedErrors[] = {
    DXGI_ERROR_DEVICE_REMOVED,
    static_cast<HRESULT>(E_ACCESSDENIED),
    DXGI_ERROR_UNSUPPORTED,
    DXGI_ERROR_SESSION_DISCONNECTED,
    S_OK
};

// These are the errors we expect from IDXGIOutputDuplication methods due to a transition
HRESULT FrameInfoExpectedErrors[] = {
    DXGI_ERROR_DEVICE_REMOVED,
    DXGI_ERROR_ACCESS_LOST,
    S_OK
};

// These are the errors we expect from IDXGIAdapter::EnumOutputs methods due to outputs becoming stale during a transition
HRESULT EnumOutputsExpectedErrors[] = {
    DXGI_ERROR_NOT_FOUND,
    S_OK
};


//
// Displays a message (Defined earlier but ensuring it matches expectation)
//
/*
void DisplayMsg(_In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr)
{
    // Simplified logging/error handling for library
    // For now, suppress message boxes or log to debug output
    OutputDebugStringW(Str);
}
*/
// Assuming DisplayMsg is already defined in this file (checked line 104 in previous version).

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


//
// Wrapper Thread Procedure (The Main Loop)
//
unsigned int __stdcall WrapperProc(void* data)
{
    UNREFERENCED_PARAMETER(data);

    INT SingleOutput = -1; // Duplicate all
    RECT DeskBounds;
    UINT OutputCount;
    bool FirstTime = true;
    bool Occluded = false;
    DYNAMIC_WAIT DynamicWait;

    while (WaitForSingleObjectEx(UnexpectedErrorEvent, 0, FALSE) != WAIT_OBJECT_0)
    {
        // Check if we requested termination externally
        if (WaitForSingleObjectEx(TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0)
        {
            break;
        }

        DUPL_RETURN Ret = DUPL_RETURN_SUCCESS;

        if (FirstTime || WaitForSingleObjectEx(ExpectedErrorEvent, 0, FALSE) == WAIT_OBJECT_0)
        {
            if (!FirstTime)
            {
                // Terminate other threads (monitor threads)
                SetEvent(TerminateThreadsEvent);
                ThreadMgr.WaitForThreadTermination();
                ResetEvent(TerminateThreadsEvent);
                ResetEvent(ExpectedErrorEvent);

                ThreadMgr.Clean();
                OutMgr.CleanRefs();

                DynamicWait.Wait();
            }
            else
            {
                FirstTime = false;
            }

            // Re-initialize
            Ret = OutMgr.InitOutput(SingleOutput, &OutputCount, &DeskBounds);
            if (Ret == DUPL_RETURN_SUCCESS)
            {
                HANDLE SharedHandle = OutMgr.GetSharedHandle();
                if (SharedHandle)
                {
                    // Pass events to ThreadManager so it can signal them
                    Ret = ThreadMgr.Initialize(SingleOutput, OutputCount, UnexpectedErrorEvent, ExpectedErrorEvent, TerminateThreadsEvent, SharedHandle, &DeskBounds);
                }
                else
                {
                    Ret = DUPL_RETURN_ERROR_UNEXPECTED;
                }
            }

            Occluded = true;
        }
        else
        {
            // Update/Save Frame
            Ret = OutMgr.UpdateApplicationWindow(ThreadMgr.GetPointerInfo(), &Occluded);
        }

        if (Ret != DUPL_RETURN_SUCCESS)
        {
            if (Ret == DUPL_RETURN_ERROR_EXPECTED)
            {
                SetEvent(ExpectedErrorEvent);
            }
            else
            {
                break; // Unexpected error
            }
        }
    }

    // Cleanup monitor threads if they are running
    SetEvent(TerminateThreadsEvent);
    ThreadMgr.WaitForThreadTermination();
    
    // Final cleanup
    ThreadMgr.Clean();
    OutMgr.CleanRefs();

    return 0;
}


int RegisterAndStartDesktopChangeCalbakc(void* a_userData, TypeDesktopChange a_clbk)
{
    if (WrapperThreadHandle != nullptr)
    {
        // Already registered/running
        return 0;
    }

    // Create Events
    UnexpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    ExpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!UnexpectedErrorEvent || !ExpectedErrorEvent || !TerminateThreadsEvent)
    {
        return -1;
    }

    // Set Callback
    OutMgr.SetFrameCallback(a_clbk, a_userData);

    // Start Wrapper Thread
    // We use _beginthreadex for C Runtime safety
    WrapperThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, WrapperProc, nullptr, 0, nullptr);

    if (!WrapperThreadHandle)
    {
        return -1;
    }

    return 0;
}

void UnregisterDesktopChangeCalbakc(void)
{
    if (WrapperThreadHandle)
    {
        // Signal termination
        SetEvent(TerminateThreadsEvent);
        SetEvent(UnexpectedErrorEvent); // Also signal this to break wait loops if any

        // Wait for wrapper thread to exit
        WaitForSingleObject(WrapperThreadHandle, INFINITE);
        CloseHandle(WrapperThreadHandle);
        WrapperThreadHandle = nullptr;
    }

    // Cleanup Events
    if (UnexpectedErrorEvent) CloseHandle(UnexpectedErrorEvent);
    if (ExpectedErrorEvent) CloseHandle(ExpectedErrorEvent);
    if (TerminateThreadsEvent) CloseHandle(TerminateThreadsEvent);

    UnexpectedErrorEvent = nullptr;
    ExpectedErrorEvent = nullptr;
    TerminateThreadsEvent = nullptr;
}
