// =============================
// File: OutputManager.h
// =============================
#pragma once
#include "CommonTypes.h"
#include <string>

class OUTPUTMANAGER {
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
    RECT m_DeskBounds{ 0,0,0,0 };

    // Saved frame tracking
    uint64_t m_LastSavedSerial = 0;
    uint64_t m_SaveCounter = 0; // 1.jpg, 2.jpg, ...
};
