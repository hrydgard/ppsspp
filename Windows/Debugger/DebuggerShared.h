#pragma once
#include <Windows.h>
#include "..\..\Core\Debugger\DebugInterface.h"

extern HMENU g_hPopupMenus;

enum { WM_DEB_RUNTOWPARAM = WM_USER+2,
	WM_DEB_GOTOWPARAM,
	WM_DEB_GOTOADDRESSEDIT,
	WM_DEB_MAPLOADED,
	WM_DEB_TABPRESSED,
	WM_DEB_SETDEBUGLPARAM,
	WM_DEB_UPDATE,
};

bool executeExpressionWindow(HWND hwnd, DebugInterface* cpu, u32& dest);
void displayExpressionError(HWND hwnd);
bool parseExpression(char* exp, DebugInterface* cpu, u32& dest);