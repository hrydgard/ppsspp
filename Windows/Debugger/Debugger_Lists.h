#pragma once

#include "../../Core/Debugger/DebugInterface.h"
#include "../../Core/HLE/sceKernelThread.h"

class CtrlThreadList
{
	HWND wnd;
	WNDPROC oldProc;
	std::vector<DebugThreadInfo> threads;
	char stringBuffer[256];

public:
	void setDialogItem(HWND hwnd);
	void reloadThreads();
	void handleNotify(LPARAM lParam);
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	const char* getCurrentThreadName();
};