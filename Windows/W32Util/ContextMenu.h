// Copyright (c) 2021- PPSSPP Project.

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

#include "Common/CommonWindows.h"

// Must match the order in ppsspp.rc.
enum class ContextMenuID {
	MEMVIEW = 0,
	DISASM = 1,
	REGLIST = 2,
	BREAKPOINTLIST = 3,
	THREADLIST = 4,
	NEWBREAKPOINT = 5,
	DISPLAYLISTVIEW = 6,
	GEDBG_STATE = 7,
	GEDBG_PREVIEW = 8,
	GEDBG_MATRIX = 9,
	GEDBG_TABS = 10,
	CPUWATCHLIST = 11,
	CPUADDWATCH = 12,
};

struct ContextPoint {
	static ContextPoint FromCursor();
	static ContextPoint FromClient(const POINT &clientPoint);
	static ContextPoint FromEvent(LPARAM lParam);

	POINT pos_{};
	bool isClient_ = false;
};

void ContextMenuInit(HINSTANCE inst);

HMENU GetContextMenu(ContextMenuID);
int TriggerContextMenu(ContextMenuID which, HWND wnd, const ContextPoint &pt);
