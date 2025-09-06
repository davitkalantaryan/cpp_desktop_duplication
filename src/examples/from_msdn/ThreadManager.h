// =============================
// File: ThreadManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class THREADMANAGER {
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
