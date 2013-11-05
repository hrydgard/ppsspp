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

struct GenericListViewColumn
{
	wchar_t *name;
	float size;
};

// the most significant bit states whether the key is currently down.
// simply checking if it's != 0 is not enough, as bit0 is set if
// the key was pressed between the last call to GetAsyncKeyState
inline bool KeyDownAsync(int vkey)
{
	return (GetAsyncKeyState(vkey) & 0x8000) != 0;
}

class GenericListControl
{
public:
	GenericListControl(HWND hwnd, const GenericListViewColumn* _columns, int _columnCount);
	virtual ~GenericListControl() { };
	void HandleNotify(LPARAM lParam);
	void Update();
	int GetSelectedIndex();
	HWND GetHandle() { return handle; };
	void SetSendInvalidRows(bool enabled) { sendInvalidRows = enabled; };
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue) = 0;
	virtual void GetColumnText(wchar_t* dest, int row, int col) = 0;
	virtual int GetRowCount() = 0;
	virtual void OnDoubleClick(int itemIndex, int column) { };
	virtual void OnRightClick(int itemIndex, int column, const POINT& point) { };
private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ResizeColumns();

	HWND handle;
	WNDPROC oldProc;
	const GenericListViewColumn* columns;
	int columnCount;
	wchar_t stringBuffer[256];
	bool valid;
	bool sendInvalidRows;
};