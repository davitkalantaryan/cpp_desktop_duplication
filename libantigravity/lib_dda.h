


#pragma once


#include <QImage>

typedef void (*TypeDesktopChange)(void*,const QImage*);

int RegisterDesktopChangeCalbakc(void* a_userData, TypeDesktopChange a_clbk);
