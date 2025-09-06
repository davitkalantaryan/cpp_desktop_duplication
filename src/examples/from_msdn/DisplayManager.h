// =============================
// File: DisplayManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class DISPLAYMANAGER {
public:
    DISPLAYMANAGER() = default;
    ~DISPLAYMANAGER() = default;

    // Copy full frame into the shared surface at (OffsetX, OffsetY)
    DUPL_RETURN ProcessFrame(_In_ FRAME_DATA* Frame, _In_ ID3D11Texture2D* SharedSurf, LONG OffsetX, LONG OffsetY);
};
