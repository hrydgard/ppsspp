//https://www.programmersought.com/article/17665094415/

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

int calc_img_crc(TCHAR *pFileName, unsigned int *uiCrcValue);
void init_crc32_tab();