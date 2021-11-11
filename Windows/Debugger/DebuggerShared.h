#pragma once
#include "Common/CommonWindows.h"
#include "Core/Debugger/DebugInterface.h"

enum { WM_DEB_GOTOWPARAM = WM_USER+2,
	WM_DEB_GOTOADDRESSEDIT,
	WM_DEB_MAPLOADED,
	WM_DEB_TABPRESSED,
	WM_DEB_SETDEBUGLPARAM,
	WM_DEB_UPDATE,
	WM_DEB_SETSTATUSBARTEXT,
	WM_DEB_GOTOHEXEDIT
};

bool executeExpressionWindow(HWND hwnd, DebugInterface* cpu, u32& dest);
void displayExpressionError(HWND hwnd);
bool parseExpression(const char* exp, DebugInterface* cpu, u32& dest);
