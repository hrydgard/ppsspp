// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.


#pragma once

#include "Debugger/Debugger_Disasm.h"
#include "Debugger/Debugger_MemoryDlg.h"
#include "Common/CommonWindows.h"

#define MAX_CPUCOUNT 1

extern CDisasm *disasmWindow[MAX_CPUCOUNT];
extern CMemoryDlg *memoryWindow[MAX_CPUCOUNT];

#if PPSSPP_API(ANY_GL)
#include "Windows/GEDebugger/GEDebugger.h"
extern CGEDebugger* geDebuggerWindow;
#endif

extern HMENU g_hPopupMenus;
extern int g_activeWindow;

enum { WINDOW_MAINWINDOW, WINDOW_CPUDEBUGGER, WINDOW_GEDEBUGGER };
