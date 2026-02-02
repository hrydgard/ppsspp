#include "ppsspp_config.h"
#include "Common/CommonWindows.h"

#include <WinUser.h>
#include <shellapi.h>
#include <commctrl.h>
#include <ShlObj.h>

#include "Misc.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"

bool KeyDownAsync(int vkey) {
#if PPSSPP_PLATFORM(UWP)
	return 0;
#else
	return (GetAsyncKeyState(vkey) & 0x8000) != 0;
#endif
}

namespace W32Util
{
	static RECT MapRectFromClientToWndCoords(HWND hwnd, const RECT &r) {
		RECT wnd_coords = r;
		// map to screen
		MapWindowPoints(hwnd, NULL, reinterpret_cast<POINT *>(&wnd_coords), 2);
		RECT scr_coords;
		GetWindowRect(hwnd, &scr_coords);
		// map to window coords by substracting the window coord origin in  screen coords.
		OffsetRect(&wnd_coords, -scr_coords.left, -scr_coords.top);
		return wnd_coords;
	}

	RECT GetNonclientMenuBorderRect(HWND hwnd) {
		RECT r;
		GetClientRect(hwnd, &r);
		r = MapRectFromClientToWndCoords(hwnd, r);
		const int y = r.top - 1;
		return {
			r.left,
			y,
			r.right,
			y + 1
		};
	}

	void CenterWindow(HWND hwnd) {
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
 
	bool CopyTextToClipboard(HWND hWnd, std::string_view text) {
		std::wstring wtext = ConvertUTF8ToWString(text);
		if (!OpenClipboard(hWnd)) {
			return false;
		}

		EmptyClipboard();
		HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (wtext.size() + 1) * sizeof(wchar_t));
		if (!hglbCopy) {
			CloseClipboard();
			return false;
		}

		// Lock the handle and copy the text to the buffer.

		wchar_t *lptstrCopy = (wchar_t *)GlobalLock(hglbCopy);
		if (lptstrCopy) {
			wcscpy(lptstrCopy, wtext.c_str());
			lptstrCopy[wtext.size()] = (wchar_t) 0;    // null character
			GlobalUnlock(hglbCopy);
			SetClipboardData(CF_UNICODETEXT, hglbCopy);
		}
		CloseClipboard();
		return lptstrCopy != nullptr;
	}

	void MakeTopMost(HWND hwnd, bool topMost) {
		SetWindowPos(hwnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
	}

	void GetWindowRes(HWND hWnd, int *xres, int *yres) {
		RECT rc;
		GetClientRect(hWnd, &rc);
		*xres = rc.right - rc.left;
		*yres = rc.bottom - rc.top;
	}

	void ShowFileInFolder(std::string_view path) {
		// SHParseDisplayName can't handle relative paths, so normalize first.
		std::string resolved = ReplaceAll(File::ResolvePath(path), "/", "\\");

		// Shell also can't handle \\?\UNC\ paths.
		// TODO: Move this to ResolvePath?
		if (startsWith(resolved, "\\\\?\\UNC\\")) {
			resolved = "\\" + resolved.substr(7);
		}

		PIDLIST_ABSOLUTE pidl = nullptr;
		std::wstring wresolved = ConvertUTF8ToWString(resolved);
		HRESULT hr = SHParseDisplayName(wresolved.c_str(), nullptr, &pidl, 0, nullptr);

		if (pidl) {
			if (SUCCEEDED(hr))
				SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			ILFree(pidl);
		} else {
			ERROR_LOG(Log::System, "SHParseDisplayName failed: %s", resolved.c_str());
		}
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

	void GetSelfExecuteParams(std::wstring &workingDirectory, std::wstring &moduleFilename) {
		workingDirectory.resize(MAX_PATH);
		size_t sz = GetCurrentDirectoryW((DWORD)workingDirectory.size(), &workingDirectory[0]);
		if (sz != 0 && sz < workingDirectory.size()) {
			// This means success, so now we can remove the null terminator.
			workingDirectory.resize(sz);
		} else if (sz > workingDirectory.size()) {
			// If insufficient, sz will include the null terminator, so we remove after.
			workingDirectory.resize(sz);
			sz = GetCurrentDirectoryW((DWORD)sz, &workingDirectory[0]);
			workingDirectory.resize(sz);
		}

		moduleFilename.clear();
		do {
			moduleFilename.resize(moduleFilename.size() + MAX_PATH);
			// On failure, this will return the same value as passed in, but success will always be one lower.
			sz = GetModuleFileName(GetModuleHandle(nullptr), &moduleFilename[0], (DWORD)moduleFilename.size());
		} while (sz >= moduleFilename.size());
		moduleFilename.resize(sz);
	}

	void ExitAndRestart(bool overrideArgs, const std::string &args) {
		SpawnNewInstance(overrideArgs, args);

		ExitProcess(0);
	}

	bool ExecuteAndGetReturnCode(const wchar_t *executable, const wchar_t *cmdline, const wchar_t *currentDirectory, DWORD *exitCode) {
		PROCESS_INFORMATION processInformation = { 0 };
		STARTUPINFO startupInfo = { 0 };
		startupInfo.cb = sizeof(startupInfo);

		std::wstring cmdlineW;
		cmdlineW += L"PPSSPP ";  // could also put the executable name as first argument, but concerned about escaping.
		cmdlineW += cmdline;

		// Create the process
		bool result = CreateProcess(executable, (LPWSTR)cmdlineW.c_str(),
			NULL, NULL, FALSE,
			NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,
			NULL, currentDirectory, &startupInfo, &processInformation);

		if (!result) {
			// We failed.
			return false;
		}

		// Successfully created the process.  Wait for it to finish.
		WaitForSingleObject(processInformation.hProcess, INFINITE);
		result = GetExitCodeProcess(processInformation.hProcess, exitCode);
		CloseHandle(processInformation.hProcess);
		CloseHandle(processInformation.hThread);
		return result != 0;
	}

	void SpawnNewInstance(bool overrideArgs, const std::string &args) {
		// This preserves arguments (for example, config file) and working directory.
		std::wstring workingDirectory;
		std::wstring moduleFilename;
		GetSelfExecuteParams(workingDirectory, moduleFilename);

		const wchar_t *cmdline;
		std::wstring wargs;
		if (overrideArgs) {
			wargs = ConvertUTF8ToWString(args);
			cmdline = wargs.c_str();
		} else {
			cmdline = RemoveExecutableFromCommandLine(GetCommandLineW());
		}
		ShellExecute(nullptr, nullptr, moduleFilename.c_str(), cmdline, workingDirectory.c_str(), SW_SHOW);
	}

	ClipboardData::ClipboardData(const char *format, size_t sz) {
		format_ = RegisterClipboardFormatA(format);
		handle_ = format_ != 0 ? GlobalAlloc(GHND, sz) : 0;
		data = handle_ != 0 ? GlobalLock(handle_) : nullptr;
	}

	ClipboardData::ClipboardData(UINT format, size_t sz) {
		format_ = format;
		handle_ = GlobalAlloc(GHND, sz);
		data = handle_ != 0 ? GlobalLock(handle_) : nullptr;
	}

	ClipboardData::~ClipboardData() {
		if (handle_ != 0) {
			GlobalUnlock(handle_);
			GlobalFree(handle_);
		}
	}

	void ClipboardData::Set() {
		if (format_ == 0 || handle_ == 0 || data == 0)
			return;
		SetClipboardData(format_, handle_);
	}
}

static constexpr UINT_PTR IDT_UPDATE = 0xC0DE0042;
static constexpr UINT UPDATE_DELAY = 1000 / 60;

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
		lvc.cx = (int)(columns[i].size * totalListSize);
		lvc.pszText = (LPTSTR)columns[i].name;

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

GenericListControl::~GenericListControl() {
	SetWindowLongPtr(handle, GWLP_USERDATA, (LONG_PTR)nullptr);
	// Don't destroy the image list, it's done automatically by the list view.
}

void GenericListControl::SetIconList(int w, int h, const std::vector<HICON> &icons) {
	images_ = ImageList_Create(w, h, ILC_COLOR32 | ILC_MASK, 0, (int)icons.size());
	for (const HICON &icon : icons)
		ImageList_AddIcon((HIMAGELIST)images_, icon);

	ListView_SetImageList(handle, (HIMAGELIST)images_, LVSIL_STATE);
}

int GenericListControl::HandleNotify(LPARAM lParam) {
	LPNMHDR mhdr = (LPNMHDR) lParam;

	if (mhdr->code == NM_DBLCLK)
	{
		LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;
		if ((item->iItem != -1 && item->iItem < GetRowCount()) || sendInvalidRows)
			OnDoubleClick(item->iItem,item->iSubItem);
		return 0;
	}

	if (mhdr->code == NM_RCLICK)
	{
		const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)lParam;
		if ((item->iItem != -1 && item->iItem < GetRowCount()) || sendInvalidRows)
			OnRightClick(item->iItem,item->iSubItem,item->ptAction);
		return 0;
	}

	if (mhdr->code == NM_CUSTOMDRAW && (ListenRowPrePaint() || ListenColPrePaint())) {
		LPNMLVCUSTOMDRAW msg = (LPNMLVCUSTOMDRAW)lParam;
		switch (msg->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
			if (OnRowPrePaint((int)msg->nmcd.dwItemSpec, msg)) {
				return CDRF_NEWFONT;
			}
			return ListenColPrePaint() ? CDRF_NOTIFYSUBITEMDRAW : CDRF_DODEFAULT;

		case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
			if (OnColPrePaint((int)msg->nmcd.dwItemSpec, msg->iSubItem, msg)) {
				return CDRF_NEWFONT;
			}
			return CDRF_DODEFAULT;
		}

		return CDRF_DODEFAULT;
	}

	if (mhdr->code == LVN_GETDISPINFO)
	{
		NMLVDISPINFO* dispInfo = (NMLVDISPINFO*)lParam;

		stringBuffer[0] = 0;
		GetColumnText(stringBuffer, ARRAY_SIZE(stringBuffer), dispInfo->item.iItem,dispInfo->item.iSubItem);
		
		if (stringBuffer[0] == 0)
			wcscat(stringBuffer,L"Invalid");

		dispInfo->item.pszText = stringBuffer;
		dispInfo->item.mask |= LVIF_TEXT;
		return 0;
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

		return 0;
	}

	if (mhdr->code == LVN_INCREMENTALSEARCH) {
		NMLVFINDITEM *request = (NMLVFINDITEM *)lParam;
		uint32_t supported = LVFI_WRAP | LVFI_STRING | LVFI_PARTIAL | LVFI_SUBSTRING;
		if ((request->lvfi.flags & ~supported) == 0 && (request->lvfi.flags & LVFI_STRING) != 0) {
			bool wrap = (request->lvfi.flags & LVFI_WRAP) != 0;
			bool partial = (request->lvfi.flags & (LVFI_PARTIAL | LVFI_SUBSTRING)) != 0;

			// It seems like 0 is always sent for start, let's override.
			int startRow = request->iStart;
			if (startRow == 0)
				startRow = GetSelectedIndex();
			int result = OnIncrementalSearch(startRow, request->lvfi.psz, wrap, partial);
			if (result != -1) {
				request->lvfi.flags = LVFI_PARAM;
				request->lvfi.lParam = (LPARAM)result;
			}
		}
	}

	return 0;
}

int GenericListControl::OnIncrementalSearch(int startRow, const wchar_t *str, bool wrap, bool partial) {
	int size = GetRowCount();
	size_t searchlen = wcslen(str);
	if (!wrap)
		size -= startRow;

	// We start with the earliest column, preferring matches on the leftmost columns by default.
	for (int c = 0; c < columnCount; ++c) {
		for (int i = 0; i < size; ++i) {
			int r = (startRow + i) % size;
			stringBuffer[0] = 0;
			GetColumnText(stringBuffer, ARRAY_SIZE(stringBuffer), r, c);
			int difference = partial ? _wcsnicmp(str, stringBuffer, searchlen) : _wcsicmp(str, stringBuffer);
			if (difference == 0)
				return r;
		}
	}

	return -1;
}

void GenericListControl::Update() {
	if (!updateScheduled_) {
		SetTimer(handle, IDT_UPDATE, UPDATE_DELAY, nullptr);
		updateScheduled_ = true;
	}
}

void GenericListControl::ProcessUpdate() {
	updating = true;
	int newRows = GetRowCount();

	int items = ListView_GetItemCount(handle);
	ListView_SetItemCount(handle, newRows);

	// Scroll to top if we're removing items.  It kinda does this automatically, but it's buggy.
	if (items > newRows) {
		POINT pt{};
		ListView_GetOrigin(handle, &pt);

		if (pt.x != 0 || pt.y != 0)
			ListView_Scroll(handle, -pt.x, -pt.y);
	}

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

	for (auto &act : pendingActions_) {
		switch (act.action) {
		case Action::CHECK:
			ListView_SetCheckState(handle, act.item, act.state ? TRUE : FALSE);
			break;

		case Action::IMAGE:
			ListView_SetItemState(handle, act.item, (act.state & 0xF) << 12, LVIS_STATEIMAGEMASK);
			break;
		}
	}
	pendingActions_.clear();

	ResizeColumns();

	InvalidateRect(handle, nullptr, TRUE);
	ListView_RedrawItems(handle, 0, newRows - 1);
	UpdateWindow(handle);
	updating = false;
}


void GenericListControl::SetCheckState(int item, bool state) {
	pendingActions_.push_back({ Action::CHECK, item, state ? 1 : 0 });
	Update();
}

void GenericListControl::SetItemState(int item, uint8_t state) {
	pendingActions_.push_back({ Action::IMAGE, item, (int)state });
	Update();
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
	if (!list)
		return FALSE;

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

	case WM_TIMER:
		if (wParam == IDT_UPDATE) {
			list->ProcessUpdate();
			list->updateScheduled_ = false;
			KillTimer(hwnd, wParam);
		}
		break;
	}

	return (LRESULT)CallWindowProc((WNDPROC)list->oldProc,hwnd,msg,wParam,lParam);
}

void GenericListControl::ProcessCopy() {
	int start = GetSelectedIndex();
	int size;
	if (start == -1)
		size = GetRowCount();
	else
		size = ListView_GetSelectedCount(handle);

	CopyRows(start, size);
}

void GenericListControl::CopyRows(int start, int size) {
	std::wstring data;

	if (start == 0 && size == GetRowCount()) {
		// Let's also copy the header if everything is selected.
		for (int c = 0; c < columnCount; ++c) {
			data.append(columns[c].name);
			if (c < columnCount - 1)
				data.append(L"\t");
			else
				data.append(L"\r\n");
		}
	}

	for (int r = start; r < start + size; ++r) {
		for (int c = 0; c < columnCount; ++c) {
			stringBuffer[0] = 0;
			GetColumnText(stringBuffer, ARRAY_SIZE(stringBuffer), r, c);
			data.append(stringBuffer);
			if (c < columnCount - 1)
				data.append(L"\t");
			else
				data.append(L"\r\n");
		}
	}

	W32Util::CopyTextToClipboard(handle, ConvertWStringToUTF8(data));
}

void GenericListControl::SelectAll() {
	ListView_SetItemState(handle, -1, LVIS_SELECTED, LVIS_SELECTED);
}

int GenericListControl::GetSelectedIndex() {
	return ListView_GetNextItem(handle, -1, LVNI_SELECTED);
}
