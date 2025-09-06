// =============================
// File: DuplicationManager.h
// =============================
#pragma once
#include "CommonTypes.h"

class DUPLICATIONMANAGER {
public:
    DUPLICATIONMANAGER() = default;
    ~DUPLICATIONMANAGER();

    DUPL_RETURN InitDupl(ID3D11Device* Device, UINT Output);
    void GetOutputDesc(DXGI_OUTPUT_DESC* Desc) const { *Desc = m_OutputDesc; }

    DUPL_RETURN GetFrame(FRAME_DATA* Data, bool* Timeout);
    void DoneWithFrame();

private:
    CComPtr<IDXGIOutputDuplication> m_DeskDupl;
    DXGI_OUTPUT_DESC m_OutputDesc{};
};
