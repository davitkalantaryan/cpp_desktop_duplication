

#include <cinternal/disable_compiler_warnings.h>
#include <stdio.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cinternal/undisable_compiler_warnings.h>


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int main()
{
    const HCURSOR Cursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!Cursor)
    {
        fprintf(stderr, "Cursor load failed\n");
        return 0;
    }

    const HINSTANCE hInstance = (HINSTANCE)GetModuleHandleA(nullptr);

    // Register class
    WNDCLASSEXW Wc;
    Wc.cbSize = sizeof(WNDCLASSEXW);
    Wc.style = CS_HREDRAW | CS_VREDRAW;
    Wc.lpfnWndProc = WndProc;
    Wc.cbClsExtra = 0;
    Wc.cbWndExtra = 0;
    Wc.hInstance = hInstance;
    Wc.hIcon = nullptr;
    Wc.hCursor = Cursor;
    Wc.hbrBackground = nullptr;
    Wc.lpszMenuName = nullptr;
    Wc.lpszClassName = L"ddasample";
    Wc.hIconSm = nullptr;
    if (!RegisterClassExW(&Wc))
    {
        //ProcessFailure(nullptr, L"Window class registration failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    HWND WindowHandle = nullptr;

    RECT WindowRect = { 0, 0, 800, 600 };
    AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
    WindowHandle = CreateWindowW(L"ddasample", L"DXGI desktop duplication sample",
        WS_OVERLAPPEDWINDOW,
        0, 0,
        WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!WindowHandle)
    {
        //ProcessFailure(nullptr, L"Window creation failed", L"Error", E_FAIL);
        return 0;
    }

    DestroyCursor(Cursor);

    return 0;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    case WM_SIZE:
    {
        // Tell output manager that window size has changed
        OutMgr.WindowResize();
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}
