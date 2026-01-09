


#pragma once


#include <QImage>

typedef void (*TypeDesktopChange)(void*,const QImage*);

int RegisterAndStartDesktopChangeCalbakc(void* a_userData, TypeDesktopChange a_clbk);
void UnregisterDesktopChangeCalbakc(void);
