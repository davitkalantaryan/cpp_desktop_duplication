
#pragma once

typedef void (*TypeDesktopChange)(void*, const void* a_qtImage, const void* a_qtRect, const void* a_qtPoint);

int RegisterAndStartDesktopChangeCalbakc(void* a_userData, TypeDesktopChange a_clbk);
void UnregisterDesktopChangeCalbakc(void);
