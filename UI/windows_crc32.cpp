//https://www.programmersought.com/article/17665094415/

#include "UI\windows_crc32.h"
#include <Windows.h>

using namespace std;
 
#define Poly 0xEDB88320L//CRC32 standard
 static unsigned int crc_tab32[256];//CRC query table
 //Generate CRC query table
void init_crc32_tab(void)
{
    int i, j;
    unsigned int crc;
 
    for (i = 0; i < 256; i++)
    {
        crc = (unsigned long)i;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x00000001L)
                crc = (crc >> 1) ^ Poly;
            else
                crc = crc >> 1;
        }
        crc_tab32[i] = crc;
    }
}
 //Get CRC
unsigned int get_crc32(unsigned int crcinit, unsigned char * bs, unsigned int bssize)
{
    unsigned int crc = crcinit ^ 0xffffffff;
 
    while (bssize--)
        crc = (crc >> 8) ^ crc_tab32[(crc & 0xff) ^ *bs++];
 
    return crc ^ 0xffffffff;
}
 //Get file CRC
int calc_img_crc(TCHAR *pFileName, unsigned int *uiCrcValue)
{
	init_crc32_tab();
    HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
 
    const unsigned int size = 16 * 1024;
    unsigned char crcbuf[size];
    DWORD rdlen;
         unsigned int crc = 0;//The initial value of CRC is 0
 
    while (ReadFile(hFile, crcbuf, size, &rdlen, NULL), rdlen)
        crc = get_crc32(crc, crcbuf, rdlen);
 
    *uiCrcValue = crc;
    CloseHandle(hFile);
 
    return 0;
}
