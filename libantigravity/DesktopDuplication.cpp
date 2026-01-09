// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include <windows.h>
#include <string>
#include <QImage>
#include <QDir>
#include "lib_dda.h"

// Define a simple struct to hold our saving state if needed
struct CallbackData {
    std::string OutputDirectory;
    int FrameCount;
};

// Callback function
void FrameCallback(void* userData, const void* a_img)
{
    const QImage* img = (const QImage*)a_img;
    if (!userData || !img) return;
    CallbackData* data = (CallbackData*)userData;

    // Ensure directory exists (basic check, optimized to not do it every frame if possible in real apps, 
    // but for this test consistent with original logic)
    static bool dirChecked = false;
    QString dirPath = QString::fromStdString(data->OutputDirectory);
    if (!dirChecked)
    {
        QDir dir(dirPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        dirChecked = true;
    }

    // Save
    QString filename = QString("%1/frame_%2.png")
        .arg(dirPath)
        .arg(data->FrameCount, 6, 10, QChar('0'));
    
    img->save(filename, "PNG");
    data->FrameCount++;
}

//
// Program entry point
//
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ INT nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Prepare User Data
    CallbackData cbData;
    cbData.OutputDirectory = "./out";
    cbData.FrameCount = 0;

    // Register Callbacks
    int result = RegisterAndStartDesktopChangeCalbakc(&cbData, FrameCallback);
    if (result != 0)
    {
        MessageBoxA(nullptr, "Failed to register callback", "Error", MB_OK);
        return -1;
    }

    // Wait Loop as requested
    while (1)
    {
        Sleep(10);
        // In a real app we might look for a quit signal or key press
        // For this test, user said they will terminate via Task Manager
    }

    // Unregister (Unreachable in infinite loop but good practice)
    UnregisterDesktopChangeCalbakc();

    return 0;
}
