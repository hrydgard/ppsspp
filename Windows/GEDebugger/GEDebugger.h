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

enum class GETabPosition {
	LEFT = 1,
	RIGHT = 2,
	TOPRIGHT = 4,
	ALL = 7,
};
ENUM_CLASS_BITOPS(GETabPosition);

enum class GETabType {
	LIST_DISASM,
	LISTS,
	STATE,
	WATCH,
};

struct GEDebuggerTab {
	const wchar_t *name;
	GETabPosition pos;
	GETabType type;
	struct {
		union {
			Dialog *dlg;
			CtrlDisplayListView *displayList;
			void *ptr;
		};
		int index = -1;
	} state[3];
	void *(*add)(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, HINSTANCE inst, HWND parent);
	void (*remove)(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr);
	void (*update)(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr);
};

class StepCountDlg : public Dialog {
public:
	StepCountDlg(HINSTANCE _hInstance, HWND _hParent);
	~StepCountDlg();
protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;
private:
	void Jump(int count, bool relative);
};

class CGEDebugger : public Dialog {
public:
	CGEDebugger(HINSTANCE _hInstance, HWND _hParent);
	~CGEDebugger();

	static void Init();

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

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
	void DescribePrimaryPreview(const GPUgstate &state, char desc[256]);
	void DescribeSecondPreview(const GPUgstate &state, char desc[256]);
	void PrimaryPreviewHover(int x, int y);
	void SecondPreviewHover(int x, int y);
	void PreviewExport(const GPUDebugBuffer *buffer);
	void PreviewToClipboard(const GPUDebugBuffer *buffer, bool saveAlpha);
	static void DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
	static void DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
	void UpdateMenus();
	void UpdateTab(GEDebuggerTab *tab);
	void AddTab(GEDebuggerTab *tab, GETabPosition mask);
	void RemoveTab(GEDebuggerTab *tab, GETabPosition mask);
	int HasTabIndex(GEDebuggerTab *tab, GETabPosition pos);
	void CheckTabMessage(TabControl *t, GETabPosition pos, LPARAM lParam);

	u32 TexturePreviewFlags(const GPUgstate &state);

	SimpleGLWindow *primaryWindow = nullptr;
	SimpleGLWindow *secondWindow = nullptr;
	std::vector<GEDebuggerTab> tabStates_;
	TabControl *tabs = nullptr;
	TabControl *tabsRight_ = nullptr;
	TabControl *tabsTR_ = nullptr;
	TabControl *fbTabs = nullptr;
	int textureLevel_ = 0;
	bool showClut_ = false;
	bool forceOpaque_ = false;
	bool autoFlush_ = true;
	// The most recent primary/framebuffer and texture buffers.
	const GPUDebugBuffer *primaryBuffer_ = nullptr;
	const GPUDebugBuffer *secondBuffer_ = nullptr;
	bool primaryIsFramebuffer_ = false;
	bool secondIsFramebuffer_ = false;

	uint32_t primaryTrackX_ = 0xFFFFFFFF;
	uint32_t primaryTrackY_ = 0xFFFFFFFF;
	uint32_t secondTrackX_ = 0xFFFFFFFF;
	uint32_t secondTrackY_ = 0xFFFFFFFF;

	bool updating_ = false;
	int previewsEnabled_ = 3;
	int minWidth_;
	int minHeight_;

	StepCountDlg stepCountDlg;
};
