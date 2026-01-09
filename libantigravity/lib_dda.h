


#pragma once


typedef void (*TypeDesktopChange)(void*,const void*);

int RegisterAndStartDesktopChangeCalbakc(void* a_userData, TypeDesktopChange a_clbk);
void UnregisterDesktopChangeCalbakc(void);
