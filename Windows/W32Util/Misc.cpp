#include "stdafx.h"
#include "CommonWindows.h"

#include <WinUser.h>
#include <shellapi.h>
#include <commctrl.h>

#include "Misc.h"
#include "util/text/utf8.h"

bool IsVistaOrHigher() {
	OSVERSIONINFOEX osvi;
	DWORDLONG dwlConditionMask = 0;
	int op = VER_GREATER_EQUAL;
	ZeroMemory(&osvi, sizeof(osvi));
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = 6;  // Vista is 6.0
	osvi.dwMinorVersion = 0;

	VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, op);

	return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask) != FALSE;
}

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
			sprintf(out, "%d B", (int)size);
		else
			sprintf(out, "%3.1f %s", f, sizes[s]);
	}

	BOOL CopyTextToClipboard(HWND hwnd, const char *text) {
		std::wstring wtext = ConvertUTF8ToWString(text);
		return CopyTextToClipboard(hwnd, wtext);
	}

	BOOL CopyTextToClipboard(HWND hwnd, const std::wstring &wtext) {
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

	static const wchar_t *RemoveExecutableFromCommandLine(const wchar_t *cmdline) {
		if (!cmdline) {
			return L"";
		}

		switch (cmdline[0]) {
		case '"':
			// We don't need to handle escaped quotes, since filenames can't have that.
			cmdline = wcschr(cmdline + 1, '"');
			if (cmdline) {
				++cmdline;
				if (cmdline[0] == ' ') {
					++cmdline;
				}
			}
			break;

		default:
			cmdline = wcschr(cmdline, ' ');
			if (cmdline) {
				++cmdline;
			}
			break;
		}

		return cmdline;
	}

	void ExitAndRestart() {
		// This preserves arguments (for example, config file) and working directory.

		wchar_t moduleFilename[MAX_PATH];
		wchar_t workingDirectory[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, workingDirectory);
		const wchar_t *cmdline = RemoveExecutableFromCommandLine(GetCommandLineW());
		GetModuleFileName(GetModuleHandle(NULL), moduleFilename, MAX_PATH);
		ShellExecute(NULL, NULL, moduleFilename, cmdline, workingDirectory, SW_SHOW);

		ExitProcess(0);
	}
}



GenericListControl::GenericListControl(HWND hwnd, const GenericListViewDef& def)
	: handle(hwnd), columns(def.columns),columnCount(def.columnCount),valid(false),
	inResizeColumns(false),updating(false)
{
	DWORD style = GetWindowLong(handle,GWL_STYLE) | LVS_REPORT;
	SetWindowLong(handle, GWL_STYLE, style);

	SetWindowLongPtr(handle,GWLP_USERDATA,(LONG_PTR)this);
	oldProc = (WNDPROC) SetWindowLongPtr(handle,GWLP_WNDPROC,(LONG_PTR)wndProc);

	auto exStyle = LVS_EX_FULLROWSELECT;
	if (def.checkbox)
		exStyle |= LVS_EX_CHECKBOXES;
	SendMessage(handle, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, exStyle);

	LVCOLUMN lvc; 
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	
	RECT rect;
	GetClientRect(handle,&rect);

	int totalListSize = rect.right-rect.left;
	for (int i = 0; i < columnCount; i++) {
		lvc.cx = columns[i].size * totalListSize;
		lvc.pszText = columns[i].name;

		if (columns[i].flags & GLVC_CENTERED)
			lvc.fmt = LVCFMT_CENTER;
		else
			lvc.fmt = LVCFMT_LEFT;

		ListView_InsertColumn(handle, i, &lvc);
	}

	if (def.columnOrder != NULL)
		ListView_SetColumnOrderArray(handle,columnCount,def.columnOrder);

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
	 
	// handle checkboxes
	if (mhdr->code == LVN_ITEMCHANGED && updating == false)
	{
		NMLISTVIEW* item = (NMLISTVIEW*) lParam;
		if (item->iItem != -1 && (item->uChanged & LVIF_STATE) != 0)
		{
			// image is 1 if unchcked, 2 if checked
			int oldImage = (item->uOldState & LVIS_STATEIMAGEMASK) >> 12;
			int newImage = (item->uNewState & LVIS_STATEIMAGEMASK) >> 12;
			if (oldImage != newImage)
				OnToggle(item->iItem,newImage == 2);
		}

		return;
	}
}

void GenericListControl::Update()
{
	updating = true;
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
	updating = false;
}


void GenericListControl::SetCheckState(int item, bool state)
{
	updating = true;
	ListView_SetCheckState(handle,item,state ? TRUE : FALSE);
	updating = false;
}

void GenericListControl::ResizeColumns()
{
	if (inResizeColumns)
		return;
	inResizeColumns = true;

	RECT rect;
	GetClientRect(handle, &rect);

	int totalListSize = rect.right - rect.left;
	for (int i = 0; i < columnCount; i++)
	{
		ListView_SetColumnWidth(handle, i, columns[i].size * totalListSize);
	}
	inResizeColumns = false;
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

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_INSERT:
		case 'C':
			if (KeyDownAsync(VK_CONTROL))
				list->ProcessCopy();
			break;

		case 'A':
			if (KeyDownAsync(VK_CONTROL))
				list->SelectAll();
			break;
		}
		break;
	}

	return (LRESULT)CallWindowProc((WNDPROC)list->oldProc,hwnd,msg,wParam,lParam);
}

void GenericListControl::ProcessCopy()
{
	int start = GetSelectedIndex();
	int size;
	if (start == -1)
		size = GetRowCount();
	else
		size = ListView_GetSelectedCount(handle);

	CopyRows(start, size);
}

void GenericListControl::CopyRows(int start, int size)
{
	std::wstring data;

	if (start == 0 && size == GetRowCount())
	{
		// Let's also copy the header if everything is selected.
		for (int c = 0; c < columnCount; ++c)
		{
			data.append(columns[c].name);
			if (c < columnCount - 1)
				data.append(L"\t");
			else
				data.append(L"\r\n");
		}
	}

	for (int r = start; r < start + size; ++r)
	{
		for (int c = 0; c < columnCount; ++c)
		{
			stringBuffer[0] = 0;
			GetColumnText(stringBuffer, r, c);
			data.append(stringBuffer);
			if (c < columnCount - 1)
				data.append(L"\t");
			else
				data.append(L"\r\n");
		}
	}
	W32Util::CopyTextToClipboard(handle, data);
}

void GenericListControl::SelectAll()
{
	ListView_SetItemState(handle, -1, LVIS_SELECTED, LVIS_SELECTED);
}

int GenericListControl::GetSelectedIndex()
{
	return ListView_GetNextItem(handle, -1, LVNI_SELECTED);
}