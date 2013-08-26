#pragma once

#include "Common/CommonWindows.h"

namespace W32Util
{
	void CenterWindow(HWND hwnd);
	HBITMAP CreateBitmapFromARGB(HWND someHwnd, DWORD *image, int w, int h);
	void NiceSizeFormat(size_t size, char *out);
	BOOL CopyTextToClipboard(HWND hwnd, const char *text);
	void MakeTopMost(HWND hwnd, bool topMost);
}