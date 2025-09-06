// =============================
// File: DisplayManager.cpp
// =============================
#include "DisplayManager.h"

DUPL_RETURN DISPLAYMANAGER::ProcessFrame(_In_ FRAME_DATA* Frame, _In_ ID3D11Texture2D* SharedSurf, LONG OffsetX, LONG OffsetY) {
    if (!Frame->Frame || !SharedSurf) return DUPL_RETURN_ERROR_UNEXPECTED;

    // Query dims
    D3D11_TEXTURE2D_DESC srcDesc{}, dstDesc{};
    Frame->Frame->GetDesc(&srcDesc);
    SharedSurf->GetDesc(&dstDesc);

    // Destination box (full source size) at given offset
    D3D11_BOX srcBox{}; srcBox.left = 0; srcBox.top = 0; srcBox.front = 0; srcBox.right = srcDesc.Width; srcBox.bottom = srcDesc.Height; srcBox.back = 1;

    // Copy
    CComPtr<ID3D11Device> dev; Frame->Frame->GetDevice(&dev);
    CComPtr<ID3D11DeviceContext> ctx; dev->GetImmediateContext(&ctx);
    ctx->CopySubresourceRegion(SharedSurf, 0, OffsetX, OffsetY, 0, Frame->Frame, 0, &srcBox);

    return DUPL_RETURN_SUCCESS;
}
