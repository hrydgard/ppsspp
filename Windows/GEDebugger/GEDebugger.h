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

#include "Common/CommonWindows.h"
#include "Globals.h"
#include "Windows/resource.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/TabControl.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"

enum {
	WM_GEDBG_BREAK_CMD = WM_USER + 200,
	WM_GEDBG_BREAK_DRAW,
	WM_GEDBG_STEPDISPLAYLIST,
	WM_GEDBG_TOGGLEPCBREAKPOINT,
	WM_GEDBG_RUNTOWPARAM,
	WM_GEDBG_SETCMDWPARAM,
};

enum BreakNextType {
	BREAK_NONE,
	BREAK_NEXT_OP,
	BREAK_NEXT_DRAW,
	BREAK_NEXT_TEX,
	BREAK_NEXT_NONTEX,
	BREAK_NEXT_FRAME,
	BREAK_NEXT_PRIM,
};

class CtrlDisplayListView;
class TabDisplayLists;
class TabStateFlags;
class TabStateLighting;
class TabStateTexture;
class TabStateSettings;
class TabVertices;
class TabMatrices;

class CGEDebugger : public Dialog {
public:
	CGEDebugger(HINSTANCE _hInstance, HWND _hParent);
	~CGEDebugger();

	static void Init();

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	void SetupPreviews();
	void UpdatePreviews();
	void UpdatePrimPreview(u32 op);
	void CleanupPrimPreview();
	void UpdateSize(WORD width, WORD height);
	void SavePosition();
	void SetBreakNext(BreakNextType type);
	void UpdateTextureLevel(int level);

	CtrlDisplayListView *displayList;
	TabDisplayLists *lists;
	TabStateFlags *flags;
	TabStateLighting *lighting;
	TabStateTexture *textureState;
	TabStateSettings *settings;
	TabVertices *vertices;
	TabMatrices *matrices;
	SimpleGLWindow *frameWindow;
	SimpleGLWindow *texWindow;
	TabControl *tabs;
	TabControl *fbTabs;
	int textureLevel_;

	int minWidth,minHeight;
};
