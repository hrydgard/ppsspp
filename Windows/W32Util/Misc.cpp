#include "stdafx.h"
#include <WinUser.h>
#include "Misc.h"
#include "util/text/utf8.h"
#include <commctrl.h>

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

	BOOL CopyTextToClipboard(HWND hwnd, const char *text) {
		std::wstring wtext = ConvertUTF8ToWString(text);
		OpenClipboard(hwnd);
		EmptyClipboard();
		HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (wtext.size() + 1) * sizeof(wchar_t)); 
		if (hglbCopy == NULL) { 
			CloseClipboard(); 
			return FALSE; 
		} 

		// Lock the handle and copy the text to the buffer. 

		wchar_t *lptstrCopy = (wchar_t *)GlobalLock(hglbCopy); 
		wcscpy(lptstrCopy, wtext.c_str()); 
		lptstrCopy[wtext.size()] = (wchar_t) 0;    // null character 
		GlobalUnlock(hglbCopy); 
		SetClipboardData(CF_UNICODETEXT, hglbCopy);
		CloseClipboard();
		return TRUE;
	}

	void MakeTopMost(HWND hwnd, bool topMost) {
		HWND style = HWND_NOTOPMOST;
		if (topMost) style = HWND_TOPMOST;
		SetWindowPos(hwnd, style, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
	}

}



GenericListControl::GenericListControl(HWND hwnd, const GenericListViewColumn* _columns, int _columnCount)
	: handle(hwnd), columns(_columns),columnCount(_columnCount),valid(false)
{
	DWORD style = GetWindowLong(handle,GWL_STYLE) | LVS_REPORT;
	SetWindowLong(handle, GWL_STYLE, style);

	SetWindowLongPtr(handle,GWLP_USERDATA,(LONG_PTR)this);
	oldProc = (WNDPROC) SetWindowLongPtr(handle,GWLP_WNDPROC,(LONG_PTR)wndProc);

	SendMessage(handle, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

	LVCOLUMN lvc; 
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.fmt = LVCFMT_LEFT;
	
	RECT rect;
	GetClientRect(handle,&rect);

	int totalListSize = rect.right-rect.left;
	for (int i = 0; i < columnCount; i++) {
		lvc.cx = columns[i].size * totalListSize;
		lvc.pszText = columns[i].name;
		ListView_InsertColumn(handle, i, &lvc);
	}

	SetSendInvalidRows(false);
	valid = true;
}

void GenericListControl::HandleNotify(LPARAM lParam)
{
	LPNMHDR mhdr = (LPNMHDR) lParam;

	if (mhdr->code == NM_DBLCLK)
	{
		LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;
		if ((item->iItem != -1 && item->iItem < GetRowCount()) || sendInvalidRows)
			OnDoubleClick(item->iItem,item->iSubItem);
		return;
	}

	if (mhdr->code == NM_RCLICK)
	{
		const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)lParam;
		if ((item->iItem != -1 && item->iItem < GetRowCount()) || sendInvalidRows)
			OnRightClick(item->iItem,item->iSubItem,item->ptAction);
		return;
	}

	if (mhdr->code == LVN_GETDISPINFO)
	{
		NMLVDISPINFO* dispInfo = (NMLVDISPINFO*)lParam;

		stringBuffer[0] = 0;
		GetColumnText(stringBuffer,dispInfo->item.iItem,dispInfo->item.iSubItem);
		
		if (stringBuffer[0] == 0)
			wcscat(stringBuffer,L"Invalid");

		dispInfo->item.pszText = stringBuffer;
		return;
	}
}

void GenericListControl::Update()
{
	int newRows = GetRowCount();

	int items = ListView_GetItemCount(handle);
	while (items < newRows)
	{
		LVITEM lvI;
		lvI.pszText   = LPSTR_TEXTCALLBACK; // Sends an LVN_GETDISPINFO message.
		lvI.mask      = LVIF_TEXT | LVIF_IMAGE |LVIF_STATE;
		lvI.stateMask = 0;
		lvI.iSubItem  = 0;
		lvI.state     = 0;
		lvI.iItem  = items;
		lvI.iImage = items;

		ListView_InsertItem(handle, &lvI);
		items++;
	}

	while (items > newRows)
	{
		ListView_DeleteItem(handle,--items);
	}

	ResizeColumns();

	InvalidateRect(handle,NULL,true);
	UpdateWindow(handle);
}

void GenericListControl::ResizeColumns()
{
	RECT rect;
	GetClientRect(handle,&rect);

	int totalListSize = rect.right-rect.left;
	for (int i = 0; i < columnCount; i++)
	{
		ListView_SetColumnWidth(handle,i,columns[i].size * totalListSize);
	}
}

LRESULT CALLBACK GenericListControl::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	GenericListControl* list = (GenericListControl*) GetWindowLongPtr(hwnd,GWLP_USERDATA);

	LRESULT returnValue;
	if (list->valid && list->WindowMessage(msg,wParam,lParam,returnValue) == true)
		return returnValue;

	switch (msg)
	{
	case WM_SIZE:
		list->ResizeColumns();
		break;
	}

	return (LRESULT)CallWindowProc((WNDPROC)list->oldProc,hwnd,msg,wParam,lParam);
}

int GenericListControl::GetSelectedIndex()
{
	return ListView_GetNextItem(handle, -1, LVNI_SELECTED);
}