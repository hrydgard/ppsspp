#pragma once
#include <string>
#include "Common/CommonWindows.h"
#include "Common/CommonTypes.h"
#include "Core/Debugger/DebugInterface.h"

class ScanRemoveWindow {
	HWND parentHwnd;
	DebugInterface* cpu;

	bool scan;
	u32 address;
	u32 size;

	bool GetCheckState(HWND hwnd, int dlgItem);
	bool fetchDialogData(HWND hwnd);

	static INT_PTR CALLBACK StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	INT_PTR DlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

public:
	ScanRemoveWindow(HWND parent, DebugInterface* cpu) : cpu(cpu) {
		parentHwnd = parent;
		scan = true;
		address = -1;
		size = 1;
	}

	bool exec();
};
