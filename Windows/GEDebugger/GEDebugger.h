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
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Debugger.h"
#include "Windows/resource.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/TabControl.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"

enum {
	WM_GEDBG_STEPDISPLAYLIST = WM_USER + 200,
	WM_GEDBG_TOGGLEPCBREAKPOINT,
	WM_GEDBG_RUNTOWPARAM,
	WM_GEDBG_SETCMDWPARAM,
	WM_GEDBG_UPDATE_WATCH,
};

class CtrlDisplayListView;
class TabDisplayLists;
class TabStateFlags;
class TabStateLighting;
class TabStateTexture;
class TabStateSettings;
class TabVertices;
class TabMatrices;
class TabStateWatch;
struct GPUgstate;

class StepCountDlg : public Dialog {
public:
	StepCountDlg(HINSTANCE _hInstance, HWND _hParent);
	~StepCountDlg();
protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
private:
	void Jump(int count, bool relative);
};

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
	void UpdatePrimaryPreview(const GPUgstate &state);
	void UpdateSecondPreview(const GPUgstate &state);
	u32 PrimPreviewOp();
	void UpdatePrimPreview(u32 op, int which);
	void CleanupPrimPreview();
	void HandleRedraw(int which);
	void UpdateSize(WORD width, WORD height);
	void SavePosition();
	void UpdateTextureLevel(int level);
	void DescribePrimaryPreview(const GPUgstate &state, wchar_t desc[256]);
	void DescribeSecondPreview(const GPUgstate &state, wchar_t desc[256]);
	void PrimaryPreviewHover(int x, int y);
	void SecondPreviewHover(int x, int y);
	void PreviewExport(const GPUDebugBuffer *buffer);
	void DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, wchar_t desc[256]);
	void DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, wchar_t desc[256]);

	u32 TexturePreviewFlags(const GPUgstate &state);

	CtrlDisplayListView *displayList = nullptr;
	TabDisplayLists *lists = nullptr;
	TabStateFlags *flags = nullptr;
	TabStateLighting *lighting = nullptr;
	TabStateTexture *textureState = nullptr;
	TabStateSettings *settings = nullptr;
	TabVertices *vertices = nullptr;
	TabMatrices *matrices = nullptr;
	SimpleGLWindow *primaryWindow = nullptr;
	SimpleGLWindow *secondWindow = nullptr;
	TabStateWatch *watch = nullptr;
	TabControl *tabs = nullptr;
	TabControl *fbTabs = nullptr;
	int textureLevel_ = 0;
	bool showClut_ = false;
	bool forceOpaque_ = false;
	// The most recent primary/framebuffer and texture buffers.
	const GPUDebugBuffer *primaryBuffer_ = nullptr;
	const GPUDebugBuffer *secondBuffer_ = nullptr;

	bool updating_ = false;
	int previewsEnabled_ = 3;
	int minWidth_;
	int minHeight_;

	StepCountDlg stepCountDlg;
};
