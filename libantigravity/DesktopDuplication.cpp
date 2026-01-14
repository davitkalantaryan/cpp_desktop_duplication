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

// Define a simple struct to hold our saving state
struct CallbackData {
    std::string OutputDirectory;
    int FrameCount;
};

// Callback function
void FrameCallback(void* userData, const void* a_img, const void* a_rect, const void* a_point)
{
    const QImage* img = (const QImage*)a_img;
    const QRect* rect = (const QRect*)a_rect;
    const QPoint* pt = (const QPoint*)a_point;

    if (!userData || !img || !rect || !pt) return;
    CallbackData* data = (CallbackData*)userData;

    // Ensure directory exists
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

    // Save with rich filename
    // frame_rect0x0x1980x100_mouse1--x200_200_000001.png
    QString filename = QString("%1/frame_rect%2x%3x%4x%5_mouse%6--x%7_%8_%9.png")
        .arg(dirPath)
        .arg(rect->x())
        .arg(rect->y())
        .arg(rect->width())
        .arg(rect->height())
        .arg(1) 
        .arg(pt->x())
        .arg(pt->y())
        .arg(data->FrameCount++, 6, 10, QChar('0'));
    
    img->save(filename, "PNG");
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

    // Wait Loop
    while (1)
    {
        Sleep(10);
    }

    // Unregister
    UnregisterDesktopChangeCalbakc();

    return 0;
}
