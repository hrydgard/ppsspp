#include "stdafx.h"
#include <WinUser.h>
#include "Misc.h"

namespace W32Util
{
	void CenterWindow(HWND hwnd)
	{
		HWND hwndParent;
		RECT rect, rectP;
		int width, height;      
		int screenwidth, screenheight;
		int x, y;

		//make the window relative to its parent
		hwndParent = GetParent(hwnd);
		if (!hwndParent)
			return;

		GetWindowRect(hwnd, &rect);
		GetWindowRect(hwndParent, &rectP);
        
		width  = rect.right  - rect.left;
		height = rect.bottom - rect.top;

		x = ((rectP.right-rectP.left) -  width) / 2 + rectP.left;
		y = ((rectP.bottom-rectP.top) - height) / 2 + rectP.top;

		screenwidth  = GetSystemMetrics(SM_CXSCREEN);
		screenheight = GetSystemMetrics(SM_CYSCREEN);
    
		//make sure that the dialog box never moves outside of
		//the screen
		if(x < 0) x = 0;
		if(y < 0) y = 0;
		if(x + width  > screenwidth)  x = screenwidth  - width;
		if(y + height > screenheight) y = screenheight - height;

		MoveWindow(hwnd, x, y, width, height, FALSE);
	}
 
	void NiceSizeFormat(size_t size, char *out)
	{
		char *sizes[] = {"B","KB","MB","GB","TB","PB","EB"};
		int s = 0;
		int frac = 0;
		while (size>=1024)
		{
			s++;
			frac = (int)size & 1023;
			size /= 1024;
		}
		float f = (float)size + ((float)frac / 1024.0f);
		if (s==0)
			sprintf(out,"%d B",size);
		else
			sprintf(out,"%3.1f %s",f,sizes[s]);
	}

	BOOL CopyTextToClipboard(HWND hwnd, const TCHAR *text)
	{
		OpenClipboard(hwnd);
		EmptyClipboard();
		HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (strlen(text) + 1) * sizeof(TCHAR)); 
		if (hglbCopy == NULL) 
		{ 
			CloseClipboard(); 
			return FALSE; 
		} 

		// Lock the handle and copy the text to the buffer. 

		TCHAR *lptstrCopy = (TCHAR *)GlobalLock(hglbCopy); 
		strcpy(lptstrCopy, text); 
		lptstrCopy[strlen(text)] = (TCHAR) 0;    // null character 
		GlobalUnlock(hglbCopy); 
		SetClipboardData(CF_TEXT,hglbCopy);
		CloseClipboard();
		return TRUE;
	}

	void MakeTopMost(HWND hwnd, bool topMost) {
		HWND style = HWND_NOTOPMOST;
		if (topMost) style = HWND_TOPMOST;
		SetWindowPos(hwnd, style, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
	}

}