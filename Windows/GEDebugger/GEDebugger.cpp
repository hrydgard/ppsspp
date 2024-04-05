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

#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "Common/CommonWindows.h"
#include <commctrl.h>

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"

#include "Core/Config.h"
#include "Core/Screenshot.h"

#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Windows/GEDebugger/CtrlDisplayListView.h"
#include "Windows/GEDebugger/TabDisplayLists.h"
#include "Windows/GEDebugger/TabState.h"
#include "Windows/GEDebugger/TabVertices.h"
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/InputBox.h"
#include "Windows/MainWindow.h"
#include "Windows/main.h"

#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/Stepping.h"

using namespace GPUBreakpoints;
using namespace GPUDebug;
using namespace GPUStepping;

enum PrimaryDisplayType {
	PRIMARY_FRAMEBUF,
	PRIMARY_DEPTHBUF,
	PRIMARY_STENCILBUF,
};

enum class GEPanelIndex {
	LEFT,
	RIGHT,
	TOPRIGHT,
	COUNT,
};

static void *AddDisplayListTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, HINSTANCE inst, HWND parent) {
	HWND wnd = tabs->AddTabWindow(L"CtrlDisplayListView", tab->name);
	return CtrlDisplayListView::getFrom(wnd);
}

static void RemoveDisplayListTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr) {
	CtrlDisplayListView *view = (CtrlDisplayListView *)ptr;
	DestroyWindow(view->GetHWND());
}

static void UpdateDisplayListTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr) {
	CtrlDisplayListView *view = (CtrlDisplayListView *)ptr;
	DisplayList list;
	if (gpuDebug != nullptr && gpuDebug->GetCurrentDisplayList(list)) {
		view->setDisplayList(list);
	} else {
		view->clearDisplayList();
	}
}

template <typename T>
static void *AddStateTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, HINSTANCE inst, HWND parent) {
	T *w = new T(inst, parent);
	tabs->AddTabDialog(w, tab->name);
	return w;
}

static void RemoveStateTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr) {
	Dialog *view = (Dialog *)ptr;
	delete view;
}

static void UpdateStateTab(GEDebuggerTab *tab, TabControl *tabs, GETabPosition pos, void *ptr) {
	Dialog *view = (Dialog *)ptr;
	view->Update();
}

static const std::vector<GEDebuggerTab> defaultTabs = {
	{ L"Display List", GETabPosition::LEFT, GETabType::LIST_DISASM, {}, &AddDisplayListTab, &RemoveDisplayListTab, &UpdateDisplayListTab },
	{ L"Flags", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabStateFlags>, &RemoveStateTab, &UpdateStateTab },
	{ L"Light", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabStateLighting>, &RemoveStateTab, &UpdateStateTab },
	{ L"Texture", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabStateTexture>, &RemoveStateTab, &UpdateStateTab },
	{ L"Settings", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabStateSettings>, &RemoveStateTab, &UpdateStateTab },
	{ L"Verts", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabVertices>, &RemoveStateTab, &UpdateStateTab },
	{ L"Matrices", GETabPosition::LEFT, GETabType::STATE, {}, &AddStateTab<TabMatrices>, &RemoveStateTab, &UpdateStateTab },
	{ L"Lists", GETabPosition::LEFT, GETabType::LISTS, {}, &AddStateTab<TabDisplayLists>, &RemoveStateTab, &UpdateStateTab },
	{ L"Watch", GETabPosition::LEFT, GETabType::WATCH, {}, &AddStateTab<TabStateWatch>, &RemoveStateTab, &UpdateStateTab },
};

StepCountDlg::StepCountDlg(HINSTANCE _hInstance, HWND _hParent) : Dialog((LPCSTR)IDD_GEDBG_STEPCOUNT, _hInstance, _hParent) {
	DialogManager::AddDlg(this);

	for (int i = 0; i < 4; i++) // Add items 1, 10, 100, 1000
		SendMessageA(GetDlgItem(m_hDlg, IDC_GEDBG_STEPCOUNT_COMBO), CB_ADDSTRING, 0, (LPARAM)std::to_string((int)pow(10, i)).c_str());
	SetWindowTextA(GetDlgItem(m_hDlg, IDC_GEDBG_STEPCOUNT_COMBO), "1");
}

StepCountDlg::~StepCountDlg() {
	DialogManager::RemoveDlg(this);
}

void StepCountDlg::Jump(int count, bool relative) {
	if (relative && count == 0)
		return;
	SetBreakNext(BreakNext::COUNT);
	SetBreakCount(count, relative);
};

BOOL StepCountDlg::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	int count;
	bool relative;
	auto GetValue = [&]() {
		char str[7]; // +/-99999\0
		GetWindowTextA(GetDlgItem(m_hDlg, IDC_GEDBG_STEPCOUNT_COMBO), str, 7);
		relative = str[0] == '+' || str[0] == '-';
		return TryParse(str, &count);
	};

	switch (message) {
	case WM_CLOSE:
		Show(false);
		return TRUE;
	case WM_COMMAND:
		switch (wParam) {
		case IDC_GEDBG_STEPCOUNT_DEC:
			if (GetValue())
				Jump(-abs(count), true);
			return TRUE;
		case IDC_GEDBG_STEPCOUNT_INC:
			if (GetValue())
				Jump(abs(count), true);
			return TRUE;
		case IDC_GEDBG_STEPCOUNT_JUMP:
			if (GetValue())
				Jump(abs(count), false);
			return TRUE;
		case IDOK:
			if (GetValue())
				Jump(count, relative);
			Show(false);
			return TRUE;
		case IDCANCEL:
			SetFocus(m_hParent);
			Show(false);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void CGEDebugger::Init() {
	SimpleGLWindow::RegisterClass();
	CtrlDisplayListView::registerClass();
}

CGEDebugger::CGEDebugger(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDEBUGGER, _hInstance, _hParent)
	, stepCountDlg(_hInstance, m_hDlg) {
	SetMenu(m_hDlg, LoadMenu(_hInstance, MAKEINTRESOURCE(IDR_GEDBG_MENU)));

	// minimum size = a little more than the default
	RECT windowRect;
	GetWindowRect(m_hDlg, &windowRect);
	minWidth_ = windowRect.right-windowRect.left + 10;
	minHeight_ = windowRect.bottom-windowRect.top + 10;

	// it's ugly, but .rc coordinates don't match actual pixels and it screws
	// up both the size and the aspect ratio
	RECT frameRect;
	HWND frameWnd = GetDlgItem(m_hDlg,IDC_GEDBG_FRAME);
	GetWindowRect(frameWnd,&frameRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&frameRect,2);
	MoveWindow(frameWnd,frameRect.left,frameRect.top,512,272,TRUE);

	tabs = new TabControl(GetDlgItem(m_hDlg, IDC_GEDBG_MAINTAB));
	tabsRight_ = new TabControl(GetDlgItem(m_hDlg, IDC_GEDBG_RIGHTTAB));
	tabsTR_ = new TabControl(GetDlgItem(m_hDlg, IDC_GEDBG_TOPRIGHTTAB));

	fbTabs = new TabControl(GetDlgItem(m_hDlg, IDC_GEDBG_FBTABS));
	fbTabs->SetMinTabWidth(50);
	// Must be in the same order as PrimaryDisplayType.
	fbTabs->AddTab(NULL, L"Color");
	fbTabs->AddTab(NULL, L"Depth");
	fbTabs->AddTab(NULL, L"Stencil");
	fbTabs->ShowTab(0, true);

	tabStates_ = defaultTabs;
	// Restore settings, if any set.
	_assert_msg_(defaultTabs.size() <= 32, "Cannot have more than 32 tabs");
	if ((g_Config.uGETabsLeft | g_Config.uGETabsRight | g_Config.uGETabsTopRight) != 0) {
		for (int i = 0; i < (int)tabStates_.size(); ++i) {
			int mask = 1 << i;
			tabStates_[i].pos = (GETabPosition)0;
			if (g_Config.uGETabsLeft & mask)
				tabStates_[i].pos |= GETabPosition::LEFT;
			if (g_Config.uGETabsRight & mask)
				tabStates_[i].pos |= GETabPosition::RIGHT;
			if (g_Config.uGETabsTopRight & mask)
				tabStates_[i].pos |= GETabPosition::TOPRIGHT;
			// If this is a new tab, add it to left.
			if (tabStates_[i].pos == (GETabPosition)0) {
				tabStates_[i].pos |= GETabPosition::LEFT;
				g_Config.uGETabsLeft |= 1 << i;
			}
		}
	} else {
		g_Config.uGETabsLeft = (1 << tabStates_.size()) - 1;
	}
	for (GEDebuggerTab &tabState : tabStates_) {
		AddTab(&tabState, tabState.pos);
	}

	if (tabs->Count() > 0)
		tabs->ShowTab(0, true);
	if (tabsRight_->Count() > 0)
		tabsRight_->ShowTab(0, true);
	if (tabsTR_->Count() > 0)
		tabsTR_->ShowTab(0, true);

	// set window position
	int x = g_Config.iGEWindowX == -1 ? windowRect.left : g_Config.iGEWindowX;
	int y = g_Config.iGEWindowY == -1 ? windowRect.top : g_Config.iGEWindowY;
	int w = g_Config.iGEWindowW == -1 ? minWidth_ : std::max(minWidth_, g_Config.iGEWindowW);
	int h = g_Config.iGEWindowH == -1 ? minHeight_ : std::max(minHeight_, g_Config.iGEWindowH);
	MoveWindow(m_hDlg,x,y,w,h,FALSE);

	SetTimer(m_hDlg, 1, USER_TIMER_MINIMUM, nullptr);

	UpdateTextureLevel(textureLevel_);
}

CGEDebugger::~CGEDebugger() {
	CleanupPrimPreview();

	for (GEDebuggerTab &tabState : tabStates_) {
		RemoveTab(&tabState, GETabPosition::ALL);
	}

	delete tabs;
	delete tabsRight_;
	delete tabsTR_;
	delete fbTabs;
}

void CGEDebugger::SetupPreviews() {
	if (primaryWindow == nullptr) {
		primaryWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_FRAME));
		primaryWindow->Initialize(SimpleGLWindow::ALPHA_IGNORE | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		primaryWindow->SetHoverCallback([&] (int x, int y) {
			PrimaryPreviewHover(x, y);
		});
		primaryWindow->SetRightClickMenu(ContextMenuID::GEDBG_PREVIEW, [&] (int cmd, int x, int y) {
			HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_PREVIEW);
			switch (cmd) {
			case 0:
				// Setup.
				CheckMenuItem(subMenu, ID_GEDBG_ENABLE_PREVIEW, MF_BYCOMMAND | ((previewsEnabled_ & 1) ? MF_CHECKED : MF_UNCHECKED));
				EnableMenuItem(subMenu, ID_GEDBG_TRACK_PIXEL_STOP, primaryTrackX_ == 0xFFFFFFFF ? MF_GRAYED : MF_ENABLED);
				break;
			case ID_GEDBG_EXPORT_IMAGE:
				if (primaryBuffer_)
					PreviewExport(primaryBuffer_);
				break;
			case ID_GEDBG_COPY_IMAGE:
				if (primaryBuffer_)
					PreviewToClipboard(primaryBuffer_, false);
				break;
			case ID_GEDBG_COPY_IMAGE_ALPHA:
				if (primaryBuffer_)
					PreviewToClipboard(primaryBuffer_, true);
				break;
			case ID_GEDBG_TRACK_PIXEL:
				primaryTrackX_ = x;
				primaryTrackY_ = y;
				break;
			case ID_GEDBG_TRACK_PIXEL_STOP:
				primaryTrackX_ = 0xFFFFFFFF;
				primaryTrackY_ = 0xFFFFFFFF;
				break;
			case ID_GEDBG_ENABLE_PREVIEW:
				previewsEnabled_ ^= 1;
				primaryWindow->Redraw();
			default:
				break;
			}

			return true;
		});
		primaryWindow->SetRedrawCallback([&] {
			HandleRedraw(1);
		});
		primaryWindow->Clear();
	}
	if (secondWindow == nullptr) {
		secondWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_TEX));
		secondWindow->Initialize(SimpleGLWindow::ALPHA_BLEND | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		secondWindow->SetHoverCallback([&] (int x, int y) {
			SecondPreviewHover(x, y);
		});
		secondWindow->SetRightClickMenu(ContextMenuID::GEDBG_PREVIEW, [&] (int cmd, int x, int y) {
			HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_PREVIEW);
			switch (cmd) {
			case 0:
				// Setup.
				CheckMenuItem(subMenu, ID_GEDBG_ENABLE_PREVIEW, MF_BYCOMMAND | ((previewsEnabled_ & 2) ? MF_CHECKED : MF_UNCHECKED));
				EnableMenuItem(subMenu, ID_GEDBG_TRACK_PIXEL_STOP, secondTrackX_ == 0xFFFFFFFF ? MF_GRAYED : MF_ENABLED);
				break;
			case ID_GEDBG_EXPORT_IMAGE:
				if (secondBuffer_)
					PreviewExport(secondBuffer_);
				break;
			case ID_GEDBG_COPY_IMAGE:
				if (secondBuffer_)
					PreviewToClipboard(secondBuffer_, false);
				break;
			case ID_GEDBG_COPY_IMAGE_ALPHA:
				if (secondBuffer_)
					PreviewToClipboard(secondBuffer_, true);
				break;
			case ID_GEDBG_TRACK_PIXEL:
				secondTrackX_ = x;
				secondTrackY_ = y;
				break;
			case ID_GEDBG_TRACK_PIXEL_STOP:
				secondTrackX_ = 0xFFFFFFFF;
				secondTrackY_ = 0xFFFFFFFF;
				break;
			case ID_GEDBG_ENABLE_PREVIEW:
				previewsEnabled_ ^= 2;
				secondWindow->Redraw();
			default:
				break;
			}

			return true;
		});
		secondWindow->SetRedrawCallback([&] {
			HandleRedraw(2);
		});
		secondWindow->Clear();
	}
}

void CGEDebugger::DescribePrimaryPreview(const GPUgstate &state, char desc[256]) {
	if (primaryTrackX_ < primaryBuffer_->GetStride() && primaryTrackY_ < primaryBuffer_->GetHeight()) {
		u32 pix = primaryBuffer_->GetRawPixel(primaryTrackX_, primaryTrackY_);
		DescribePixel(pix, primaryBuffer_->GetFormat(), primaryTrackX_, primaryTrackY_, desc);
		return;
	}

	if (showClut_) {
		// In this case, we're showing the texture here.
		if (primaryIsFramebuffer_)
			snprintf(desc, 256, "FB Tex L%d: 0x%08x (%dx%d)", textureLevel_, state.getTextureAddress(textureLevel_), state.getTextureWidth(textureLevel_), state.getTextureHeight(textureLevel_));
		else
			snprintf(desc, 256, "Texture L%d: 0x%08x (%dx%d)", textureLevel_, state.getTextureAddress(textureLevel_), state.getTextureWidth(textureLevel_), state.getTextureHeight(textureLevel_));
		return;
	}

	_assert_msg_(primaryBuffer_ != nullptr, "Must have a valid primaryBuffer_");

	switch (PrimaryDisplayType(fbTabs->CurrentTabIndex())) {
	case PRIMARY_FRAMEBUF:
		snprintf(desc, 256, "Color: 0x%08x (%dx%d) fmt %s", state.getFrameBufRawAddress(), primaryBuffer_->GetStride(), primaryBuffer_->GetHeight(), GeBufferFormatToString(state.FrameBufFormat()));
		break;

	case PRIMARY_DEPTHBUF:
		snprintf(desc, 256, "Depth: 0x%08x (%dx%d)", state.getDepthBufRawAddress(), primaryBuffer_->GetStride(), primaryBuffer_->GetHeight());
		break;

	case PRIMARY_STENCILBUF:
		snprintf(desc, 256, "Stencil: 0x%08x (%dx%d)", state.getFrameBufRawAddress(), primaryBuffer_->GetStride(), primaryBuffer_->GetHeight());
		break;
	}
}

void CGEDebugger::DescribeSecondPreview(const GPUgstate &state, char desc[256]) {
	if (secondTrackX_ != 0xFFFFFFFF) {
		uint32_t x = secondTrackX_;
		uint32_t y = secondTrackY_;
		if (showClut_) {
			uint32_t clutWidth = secondBuffer_->GetStride() / 16;
			x = y * clutWidth + x;
			y = 0;
		}

		if (x < secondBuffer_->GetStride() && y < secondBuffer_->GetHeight()) {
			u32 pix = secondBuffer_->GetRawPixel(x, y);
			DescribePixel(pix, secondBuffer_->GetFormat(), x, y, desc);
			return;
		}
	}

	if (showClut_) {
		snprintf(desc, 256, "CLUT: 0x%08x (%d)", state.getClutAddress(), state.getClutPaletteFormat());
	} else if (secondIsFramebuffer_) {
		snprintf(desc, 256, "FB Tex L%d: 0x%08x (%dx%d)", textureLevel_, state.getTextureAddress(textureLevel_), state.getTextureWidth(textureLevel_), state.getTextureHeight(textureLevel_));
	} else {
		snprintf(desc, 256, "Texture L%d: 0x%08x (%dx%d)", textureLevel_, state.getTextureAddress(textureLevel_), state.getTextureWidth(textureLevel_), state.getTextureHeight(textureLevel_));
	}
}

void CGEDebugger::PreviewExport(const GPUDebugBuffer *dbgBuffer) {
	const TCHAR *filter = L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg)\0*.jpg\0All files\0*.*\0\0";
	std::string fn;
	if (W32Util::BrowseForFileName(false, GetDlgHandle(), L"Save Preview Image...", nullptr, filter, L"png", fn)) {
		ScreenshotFormat fmt = fn.find(".jpg") != fn.npos ? ScreenshotFormat::JPG : ScreenshotFormat::PNG;

		Path filename(fn);
		bool saveAlpha = fmt == ScreenshotFormat::PNG;

		u8 *flipbuffer = nullptr;
		u32 w = (u32)-1;
		u32 h = (u32)-1;
		const u8 *buffer = ConvertBufferToScreenshot(*dbgBuffer, saveAlpha, flipbuffer, w, h);
		if (buffer != nullptr) {
			if (saveAlpha) {
				Save8888RGBAScreenshot(filename, buffer, w, h);
			} else {
				Save888RGBScreenshot(filename, fmt, buffer, w, h);
			}
		}
		delete [] flipbuffer;
	}
}

void CGEDebugger::PreviewToClipboard(const GPUDebugBuffer *dbgBuffer, bool saveAlpha) {
	if (!OpenClipboard(GetDlgHandle())) {
		return;
	}
	EmptyClipboard();

	uint8_t *flipbuffer = nullptr;
	uint32_t w = (uint32_t)-1;
	uint32_t h = (uint32_t)-1;
	const uint8_t *buffer = ConvertBufferToScreenshot(*dbgBuffer, saveAlpha, flipbuffer, w, h);
	if (buffer == nullptr) {
		delete [] flipbuffer;
		CloseClipboard();
		return;
	}

	uint32_t pixelSize = saveAlpha ? 4 : 3;
	uint32_t byteStride = pixelSize * w;
	while ((byteStride & 3) != 0)
		++byteStride;

	// Various apps don't support alpha well, so also copy as PNG.
	std::vector<uint8_t> png;
	if (saveAlpha) {
		// Overallocate if we can.
		png.resize(byteStride * h);
		Save8888RGBAScreenshot(png, buffer, w, h);

		W32Util::ClipboardData png1("PNG", png.size());
		W32Util::ClipboardData png2("image/png", png.size());
		if (!png.empty() && png1 && png2) {
			memcpy(png1.data, png.data(), png.size());
			memcpy(png2.data, png.data(), png.size());
			png1.Set();
			png2.Set();
		}
	}

	W32Util::ClipboardData bitmap(CF_DIBV5, sizeof(BITMAPV5HEADER) + byteStride * h);
	if (!bitmap) {
		delete [] flipbuffer;
		CloseClipboard();
		return;
	}

	BITMAPV5HEADER *header = (BITMAPV5HEADER *)bitmap.data;
	header->bV5Size = sizeof(BITMAPV5HEADER);
	header->bV5Width = w;
	header->bV5Height = h;
	header->bV5Planes = 1;
	header->bV5BitCount = saveAlpha ? 32 : 24;
	header->bV5Compression = saveAlpha ? BI_BITFIELDS : BI_RGB;
	header->bV5SizeImage = byteStride * h;
	header->bV5CSType = LCS_WINDOWS_COLOR_SPACE;
	header->bV5Intent = LCS_GM_GRAPHICS;

	if (saveAlpha) {
		header->bV5RedMask = 0x000000FF;
		header->bV5GreenMask = 0x0000FF00;
		header->bV5BlueMask = 0x00FF0000;
		// Only some applications respect the alpha mask...
		header->bV5AlphaMask = 0xFF000000;
	}

	uint8_t *pixels = (uint8_t *)(header + 1);
	for (uint32_t y = 0; y < h; ++y) {
		const uint8_t *src = buffer + y * pixelSize * w;
		uint8_t *dst = pixels + (h - y - 1) * byteStride;

		if (saveAlpha) {
			// No RB swap needed.
			memcpy(dst, src, pixelSize * w);
			continue;
		}

		for (uint32_t x = 0; x < w; ++x) {
			// Have to swap B/R again for the bitmap, unfortunate.
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
			src += pixelSize;
			dst += pixelSize;
		}
	}

	delete [] flipbuffer;

	bitmap.Set();
	CloseClipboard();
}

void CGEDebugger::UpdatePreviews() {
	auto memLock = Memory::Lock();
	if (!PSP_IsInited()) {
		return;
	}

	GPUgstate state{};

	if (gpuDebug != nullptr) {
		state = gpuDebug->GetGState();
	}

	updating_ = true;
	if (autoFlush_)
		GPU_FlushDrawing();
	UpdateTextureLevel(textureLevel_);
	UpdatePrimaryPreview(state);
	UpdateSecondPreview(state);

	u32 primOp = PrimPreviewOp();
	if (primOp != 0) {
		UpdatePrimPreview(primOp, 3);
	}

	wchar_t primCounter[1024]{};
	swprintf(primCounter, ARRAY_SIZE(primCounter), L"%d/%d", PrimsThisFrame(), PrimsLastFrame());
	SetDlgItemText(m_hDlg, IDC_GEDBG_PRIMCOUNTER, primCounter);

	for (GEDebuggerTab &tabState : tabStates_) {
		UpdateTab(&tabState);
	}

	updating_ = false;
}

void CGEDebugger::UpdateTab(GEDebuggerTab *tab) {
	auto doUpdate = [&](GETabPosition pos, TabControl *t, GEPanelIndex index) {
		if (tab->pos & pos)
			tab->update(tab, t, pos, tab->state[(int)index].ptr);
	};

	doUpdate(GETabPosition::LEFT, tabs, GEPanelIndex::LEFT);
	doUpdate(GETabPosition::RIGHT, tabsRight_, GEPanelIndex::RIGHT);
	doUpdate(GETabPosition::TOPRIGHT, tabsTR_, GEPanelIndex::TOPRIGHT);
}

void CGEDebugger::AddTab(GEDebuggerTab *tab, GETabPosition mask) {
	auto doAdd = [&](GETabPosition pos, TabControl *t, GEPanelIndex pindex) {
		int index = (int)pindex;
		// On init, we still have nullptr, but already have pos, so we use that.
		if ((mask & pos) && tab->state[index].ptr == nullptr) {
			tab->state[index].index = t->Count();
			tab->state[index].ptr = tab->add(tab, t, pos, m_hInstance, m_hDlg);
			tab->pos |= pos;
			t->ShowTab(tab->state[index].index, true);
			if (gpuDebug)
				tab->update(tab, t, pos, tab->state[index].ptr);
		}
	};

	doAdd(GETabPosition::LEFT, tabs, GEPanelIndex::LEFT);
	doAdd(GETabPosition::RIGHT, tabsRight_, GEPanelIndex::RIGHT);
	doAdd(GETabPosition::TOPRIGHT, tabsTR_, GEPanelIndex::TOPRIGHT);
}

void CGEDebugger::RemoveTab(GEDebuggerTab *tab, GETabPosition mask) {
	auto doRemove = [&](GETabPosition pos, TabControl *t, GEPanelIndex pindex) {
		int index = (int)pindex;
		if ((tab->pos & pos) && (mask & pos)) {
			auto &state = tab->state[index];
			_assert_(state.ptr != nullptr);
			t->RemoveTab(state.index);
			for (auto &tabState : tabStates_) {
				if (tabState.state[index].index > state.index)
					--tabState.state[index].index;
			}

			tab->remove(tab, t, pos, state.ptr);
			tab->pos = GETabPosition((int)tab->pos & ~(int)pos);
			state.ptr = nullptr;
			state.index = -1;
		}
	};

	doRemove(GETabPosition::LEFT, tabs, GEPanelIndex::LEFT);
	doRemove(GETabPosition::RIGHT, tabsRight_, GEPanelIndex::RIGHT);
	doRemove(GETabPosition::TOPRIGHT, tabsTR_, GEPanelIndex::TOPRIGHT);
}

int CGEDebugger::HasTabIndex(GEDebuggerTab *tab, GETabPosition pos) {
	int stateIndex = 0;
	switch (pos) {
	case GETabPosition::LEFT: stateIndex = (int)GEPanelIndex::LEFT; break;
	case GETabPosition::RIGHT: stateIndex = (int)GEPanelIndex::RIGHT; break;
	case GETabPosition::TOPRIGHT: stateIndex = (int)GEPanelIndex::TOPRIGHT; break;
	default: _assert_msg_(false, "Invalid GE tab position"); break;
	}

	if (tab->pos & pos) {
		auto &state = tab->state[stateIndex];
		if (state.ptr == nullptr)
			return -1;
		return state.index;
	}
	return -1;
}

u32 CGEDebugger::TexturePreviewFlags(const GPUgstate &state) {
	if (state.isTextureAlphaUsed() && !forceOpaque_) {
		return SimpleGLWindow::ALPHA_BLEND | SimpleGLWindow::RESIZE_BEST_CENTER;
	} else {
		return SimpleGLWindow::RESIZE_BEST_CENTER;
	}
}

void CGEDebugger::UpdatePrimaryPreview(const GPUgstate &state) {
	bool bufferResult = false;
	u32 flags = SimpleGLWindow::ALPHA_IGNORE | SimpleGLWindow::RESIZE_SHRINK_CENTER;

	SetupPreviews();

	primaryBuffer_ = nullptr;
	primaryIsFramebuffer_ = false;
	if (showClut_) {
		bufferResult = GPU_GetCurrentTexture(primaryBuffer_, textureLevel_, &primaryIsFramebuffer_);
		flags = TexturePreviewFlags(state);
		if (bufferResult) {
			UpdateLastTexture(state.getTextureAddress(textureLevel_));
		} else {
			UpdateLastTexture((u32)-1);
		}
	} else {
		switch (PrimaryDisplayType(fbTabs->CurrentTabIndex())) {
		case PRIMARY_FRAMEBUF:
			bufferResult = GPU_GetCurrentFramebuffer(primaryBuffer_, GPU_DBG_FRAMEBUF_RENDER);
			break;

		case PRIMARY_DEPTHBUF:
			bufferResult = GPU_GetCurrentDepthbuffer(primaryBuffer_);
			break;

		case PRIMARY_STENCILBUF:
			bufferResult = GPU_GetCurrentStencilbuffer(primaryBuffer_);
			break;
		}
	}

	if (bufferResult && primaryBuffer_ != nullptr) {
		auto fmt = SimpleGLWindow::Format(primaryBuffer_->GetFormat());
		primaryWindow->SetFlags(flags);
		primaryWindow->Draw(primaryBuffer_->GetData(), primaryBuffer_->GetStride(), primaryBuffer_->GetHeight(), primaryBuffer_->GetFlipped(), fmt);

		char desc[256];
		wchar_t w_desc[256];
		DescribePrimaryPreview(state, desc);
		ConvertUTF8ToWString(w_desc, ARRAY_SIZE(w_desc), desc);

		SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, w_desc);
	} else if (primaryWindow != nullptr) {
		primaryWindow->Clear();
		primaryBuffer_ = nullptr;

		SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, L"Failed");
	}
}

void CGEDebugger::UpdateSecondPreview(const GPUgstate &state) {
	bool bufferResult = false;

	SetupPreviews();

	secondBuffer_ = nullptr;
	secondIsFramebuffer_ = false;
	if (showClut_) {
		bufferResult = GPU_GetCurrentClut(secondBuffer_);
	} else {
		bufferResult = GPU_GetCurrentTexture(secondBuffer_, textureLevel_, &secondIsFramebuffer_);
		if (bufferResult) {
			UpdateLastTexture(state.getTextureAddress(textureLevel_));
		} else {
			UpdateLastTexture((u32)-1);
		}
	}

	if (bufferResult) {
		auto fmt = SimpleGLWindow::Format(secondBuffer_->GetFormat());
		secondWindow->SetFlags(TexturePreviewFlags(state));
		if (showClut_) {
			// Reduce the stride so it's easier to see.
			secondWindow->Draw(secondBuffer_->GetData(), secondBuffer_->GetStride() / 16, secondBuffer_->GetHeight() * 16, secondBuffer_->GetFlipped(), fmt);
		} else {
			secondWindow->Draw(secondBuffer_->GetData(), secondBuffer_->GetStride(), secondBuffer_->GetHeight(), secondBuffer_->GetFlipped(), fmt);
		}

		char desc[256];
		DescribeSecondPreview(state, desc);
		wchar_t w_desc[256];
		ConvertUTF8ToWString(w_desc, ARRAY_SIZE(w_desc), desc);
		SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, w_desc);
	} else if (secondWindow != nullptr) {
		secondWindow->Clear();
		secondBuffer_ = nullptr;

		if (gpuDebug == nullptr || state.isTextureMapEnabled()) {
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"Texture: failed");
		} else {
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"Texture: disabled");
		}
	}
}

void CGEDebugger::PrimaryPreviewHover(int x, int y) {
	if (primaryBuffer_ == nullptr) {
		return;
	}

	SetupPreviews();

	char desc[256] = {0};

	if (!primaryWindow->HasTex()) {
		desc[0] = 0;
	} else if (x < 0 || y < 0) {
		// This means they left the area.
		GPUgstate state{};
		if (gpuDebug != nullptr) {
			state = gpuDebug->GetGState();
		}
		DescribePrimaryPreview(state, desc);
	} else {
		// Coordinates are relative to actual framebuffer size.
		u32 pix = primaryBuffer_->GetRawPixel(x, y);
		DescribePixel(pix, primaryBuffer_->GetFormat(), x, y, desc);
	}

	wchar_t w_desc[256];
	ConvertUTF8ToWString(w_desc, ARRAY_SIZE(w_desc), desc);

	SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, w_desc);
}

void CGEDebugger::SecondPreviewHover(int x, int y) {
	if (secondBuffer_ == nullptr) {
		return;
	}

	char desc[256] = {0};

	if (!secondWindow->HasTex()) {
		desc[0] = 0;
	} else if (x < 0 || y < 0) {
		// This means they left the area.
		GPUgstate state{};
		if (gpuDebug != nullptr) {
			state = gpuDebug->GetGState();
		}
		DescribeSecondPreview(state, desc);
	} else {
		if (showClut_) {
			// Use the clut index, rather than coords.
			uint32_t clutWidth = secondBuffer_->GetStride() / 16;
			u32 pix = secondBuffer_->GetRawPixel(y * clutWidth + x, 0);
			DescribePixel(pix, secondBuffer_->GetFormat(), y * clutWidth + x, 0, desc);
		} else {
			u32 pix = secondBuffer_->GetRawPixel(x, y);
			DescribePixel(pix, secondBuffer_->GetFormat(), x, y, desc);
		}
	}
	wchar_t w_desc[256];
	ConvertUTF8ToWString(w_desc, ARRAY_SIZE(w_desc), desc);
	SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, w_desc);
}

void CGEDebugger::DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]) {
	switch (fmt) {
	case GPU_DBG_FORMAT_565:
	case GPU_DBG_FORMAT_565_REV:
	case GPU_DBG_FORMAT_5551:
	case GPU_DBG_FORMAT_5551_REV:
	case GPU_DBG_FORMAT_5551_BGRA:
	case GPU_DBG_FORMAT_4444:
	case GPU_DBG_FORMAT_4444_REV:
	case GPU_DBG_FORMAT_4444_BGRA:
	case GPU_DBG_FORMAT_8888:
	case GPU_DBG_FORMAT_8888_BGRA:
		DescribePixelRGBA(pix, fmt, x, y, desc);
		break;

	case GPU_DBG_FORMAT_16BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, pix, pix * (1.0f / 65535.0f));
		break;

	case GPU_DBG_FORMAT_8BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, pix, pix * (1.0f / 255.0f));
		break;

	case GPU_DBG_FORMAT_24BIT_8X:
	{
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		// These are only ever going to be depth values, so let's also show scaled to 16 bit.
		snprintf(desc, 256, "%d,%d: %d / %f / %f", x, y, pix & 0x00FFFFFF, (pix & 0x00FFFFFF) * (1.0f / 16777215.0f), depthScale.DecodeToU16((pix & 0x00FFFFFF) * (1.0f / 16777215.0f)));
		break;
	}

	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
		{
			// These are only ever going to be depth values, so let's also show scaled to 16 bit.
			int z24 = pix & 0x00FFFFFF;
			int z16 = z24 - 0x800000 + 0x8000;
			snprintf(desc, 256, "%d,%d: %d / %f", x, y, z16, z16 * (1.0f / 65535.0f));
		}
		break;

	case GPU_DBG_FORMAT_24X_8BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, (pix >> 24) & 0xFF, ((pix >> 24) & 0xFF) * (1.0f / 255.0f));
		break;

	case GPU_DBG_FORMAT_FLOAT: {
		float pixf = *(float *)&pix;
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		snprintf(desc, 256, "%d,%d: %f / %f", x, y, pixf, depthScale.DecodeToU16(pixf));
		break;
	}

	case GPU_DBG_FORMAT_FLOAT_DIV_256:
		{
			double z = *(float *)&pix;
			int z24 = (int)(z * 16777215.0);

			DepthScaleFactors factors = GetDepthScaleFactors(gstate_c.UseFlags());
			// TODO: Use GetDepthScaleFactors here too, verify it's the same.
			int z16 = z24 - 0x800000 + 0x8000;

			int z16_2 = factors.DecodeToU16(z);

			snprintf(desc, 256, "%d,%d: %d / %f", x, y, z16, (z - 0.5 + (1.0 / 512.0)) * 256.0);
		}
		break;

	default:
		snprintf(desc, 256, "Unexpected format");
	}
}

void CGEDebugger::DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]) {
	u32 r = -1, g = -1, b = -1, a = -1;

	switch (fmt) {
	case GPU_DBG_FORMAT_565:
		r = Convert5To8((pix >> 0) & 0x1F);
		g = Convert6To8((pix >> 5) & 0x3F);
		b = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_565_REV:
		b = Convert5To8((pix >> 0) & 0x1F);
		g = Convert6To8((pix >> 5) & 0x3F);
		r = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_5551:
		r = Convert5To8((pix >> 0) & 0x1F);
		g = Convert5To8((pix >> 5) & 0x1F);
		b = Convert5To8((pix >> 10) & 0x1F);
		a = (pix >> 15) & 1 ? 255 : 0;
		break;
	case GPU_DBG_FORMAT_5551_REV:
		a = pix & 1 ? 255 : 0;
		b = Convert5To8((pix >> 1) & 0x1F);
		g = Convert5To8((pix >> 6) & 0x1F);
		r = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_5551_BGRA:
		b = Convert5To8((pix >> 0) & 0x1F);
		g = Convert5To8((pix >> 5) & 0x1F);
		r = Convert5To8((pix >> 10) & 0x1F);
		a = (pix >> 15) & 1 ? 255 : 0;
		break;
	case GPU_DBG_FORMAT_4444:
		r = Convert4To8((pix >> 0) & 0x0F);
		g = Convert4To8((pix >> 4) & 0x0F);
		b = Convert4To8((pix >> 8) & 0x0F);
		a = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_4444_REV:
		a = Convert4To8((pix >> 0) & 0x0F);
		b = Convert4To8((pix >> 4) & 0x0F);
		g = Convert4To8((pix >> 8) & 0x0F);
		r = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_4444_BGRA:
		b = Convert4To8((pix >> 0) & 0x0F);
		g = Convert4To8((pix >> 4) & 0x0F);
		r = Convert4To8((pix >> 8) & 0x0F);
		a = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_8888:
		r = (pix >> 0) & 0xFF;
		g = (pix >> 8) & 0xFF;
		b = (pix >> 16) & 0xFF;
		a = (pix >> 24) & 0xFF;
		break;
	case GPU_DBG_FORMAT_8888_BGRA:
		b = (pix >> 0) & 0xFF;
		g = (pix >> 8) & 0xFF;
		r = (pix >> 16) & 0xFF;
		a = (pix >> 24) & 0xFF;
		break;

	default:
		snprintf(desc, 256, "Unexpected format");
		return;
	}

	snprintf(desc, 256, "%d,%d: r=%d, g=%d, b=%d, a=%d", x, y, r, g, b, a);
}

void CGEDebugger::UpdateTextureLevel(int level) {
	GPUgstate state{};
	if (gpuDebug != nullptr) {
		state = gpuDebug->GetGState();
	}

	int maxValid = 0;
	for (int i = 1; i < state.getTextureMaxLevel() + 1; ++i) {
		if (state.getTextureAddress(i) != 0) {
			maxValid = i;
		}
	}

	textureLevel_ = std::min(std::max(0, level), maxValid);
	EnableWindow(GetDlgItem(m_hDlg, IDC_GEDBG_TEXLEVELDOWN), textureLevel_ > 0);
	EnableWindow(GetDlgItem(m_hDlg, IDC_GEDBG_TEXLEVELUP), textureLevel_ < maxValid);
}

void CGEDebugger::UpdateSize(WORD width, WORD height) {
	// only resize the tabs for now
	HWND tabControl = GetDlgItem(m_hDlg, IDC_GEDBG_MAINTAB);
	HWND tabControlRight = GetDlgItem(m_hDlg, IDC_GEDBG_RIGHTTAB);
	HWND tabControlTR = GetDlgItem(m_hDlg, IDC_GEDBG_TOPRIGHTTAB);

	RECT tabRect;
	GetWindowRect(tabControl,&tabRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&tabRect,2);

	// Assume the same gap (tabRect.left) on all sides.
	if (tabsRight_ && tabsRight_->Count() == 0) {
		tabRect.right = tabRect.left + (width - tabRect.left * 2);
	} else {
		tabRect.right = tabRect.left + (width / 2 - tabRect.left * 2);
	}
	tabRect.bottom = tabRect.top + (height - tabRect.top - tabRect.left);

	RECT tabRectRight = tabRect;
	if (tabs && tabsRight_ && tabs->Count() == 0 && tabsRight_->Count() != 0) {
		tabRect.right = tabRect.left;
		tabRect.bottom = tabRect.top;
	} else {
		tabRectRight.left += tabRect.right;
		tabRectRight.right += tabRect.right + tabRect.left;
	}

	RECT frameRect;
	HWND frameWnd = GetDlgItem(m_hDlg, IDC_GEDBG_FRAME);
	GetWindowRect(frameWnd, &frameRect);
	MapWindowPoints(HWND_DESKTOP, m_hDlg, (LPPOINT)&frameRect, 2);

	RECT trRect = { frameRect.right + 10, frameRect.top, tabRectRight.right, tabRectRight.top };
	if (tabsTR_ && tabsTR_->Count() == 0) {
		trRect.right = trRect.left;
		trRect.bottom = trRect.top;
	}

	MoveWindow(tabControl, tabRect.left, tabRect.top, tabRect.right - tabRect.left, tabRect.bottom - tabRect.top, TRUE);
	MoveWindow(tabControlRight, tabRectRight.left, tabRectRight.top, tabRectRight.right - tabRectRight.left, tabRectRight.bottom - tabRectRight.top, TRUE);
	MoveWindow(tabControlTR, trRect.left, trRect.top, trRect.right - trRect.left, trRect.bottom - trRect.top, TRUE);
}

void CGEDebugger::SavePosition() {
	RECT rc;
	// Don't save while we're still loading.
	if (tabs && GetWindowRect(m_hDlg, &rc)) {
		g_Config.iGEWindowX = rc.left;
		g_Config.iGEWindowY = rc.top;
		g_Config.iGEWindowW = rc.right - rc.left;
		g_Config.iGEWindowH = rc.bottom - rc.top;
	}
}

BOOL CGEDebugger::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO* minmax = (MINMAXINFO*) lParam;
			minmax->ptMinTrackSize.x = minWidth_;
			minmax->ptMinTrackSize.y = minHeight_;
		}
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		SavePosition();
		return TRUE;

	case WM_MOVE:
		SavePosition();
		return TRUE;

	case WM_CLOSE:
		GPUDebug::SetActive(false);

		stepCountDlg.Show(false);
		Show(false);
		return TRUE;

	case WM_SHOWWINDOW:
		SetupPreviews();
		break;

	case WM_ACTIVATE:
		if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
			g_activeWindow = WINDOW_GEDEBUGGER;
		} else {
			g_activeWindow = WINDOW_OTHER;
		}
		break;

	case WM_TIMER:
		if (GPUStepping::IsStepping()) {
			static int lastCounter = 0;
			if (lastCounter != GPUStepping::GetSteppingCounter()) {
				UpdatePreviews();
				lastCounter = GPUStepping::GetSteppingCounter();
			}
		} else if (!PSP_IsInited() && primaryBuffer_) {
			SendMessage(m_hDlg, WM_COMMAND, IDC_GEDBG_RESUME, 0);
		}
		break;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_MAINTAB:
			tabs->HandleNotify(lParam);
			if (gpuDebug != nullptr) {
				for (GEDebuggerTab &tabState : tabStates_) {
					if (tabState.type == GETabType::LISTS)
						UpdateTab(&tabState);
				}
			}
			CheckTabMessage(tabs, GETabPosition::LEFT, lParam);
			break;
		case IDC_GEDBG_RIGHTTAB:
			tabsRight_->HandleNotify(lParam);
			CheckTabMessage(tabsRight_, GETabPosition::RIGHT, lParam);
			break;
		case IDC_GEDBG_TOPRIGHTTAB:
			tabsTR_->HandleNotify(lParam);
			CheckTabMessage(tabsTR_, GETabPosition::TOPRIGHT, lParam);
			break;
		case IDC_GEDBG_FBTABS:
			fbTabs->HandleNotify(lParam);
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				UpdatePreviews();
			}
			break;
		}
		break;

	case WM_MENUSELECT:
		UpdateMenus();
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_GEDBG_STEPDRAW:
			SetBreakNext(BreakNext::DRAW);
			break;

		case IDC_GEDBG_STEP:
			SetBreakNext(BreakNext::OP);
			break;

		case IDC_GEDBG_STEPTEX:
			SetBreakNext(BreakNext::TEX);
			break;

		case IDC_GEDBG_STEPFRAME:
			SetBreakNext(BreakNext::FRAME);
			break;

		case IDC_GEDBG_STEPVSYNC:
			SetBreakNext(BreakNext::VSYNC);
			break;

		case IDC_GEDBG_STEPPRIM:
			SetBreakNext(BreakNext::PRIM);
			break;

		case IDC_GEDBG_STEPCURVE:
			SetBreakNext(BreakNext::CURVE);
			break;

		case IDC_GEDBG_STEPCOUNT:
			stepCountDlg.Show(true);
			break;

		case IDC_GEDBG_BREAKTEX:
			{
				GPUDebug::SetActive(true);
				if (!gpuDebug) {
					break;
				}
				const auto state = gpuDebug->GetGState();
				u32 texAddr = state.getTextureAddress(textureLevel_);
				// TODO: Better interface that allows add/remove or something.
				if (InputBox_GetHex(GetModuleHandle(NULL), m_hDlg, L"Texture Address", texAddr, texAddr)) {
					if (IsTextureBreakpoint(texAddr)) {
						RemoveTextureBreakpoint(texAddr);
					} else {
						AddTextureBreakpoint(texAddr);
					}
				}
			}
			break;

		case IDC_GEDBG_BREAKTARGET:
			{
				GPUDebug::SetActive(true);
				if (!gpuDebug) {
					break;
				}
				const auto state = gpuDebug->GetGState();
				u32 fbAddr = state.getFrameBufRawAddress();
				// TODO: Better interface that allows add/remove or something.
				if (InputBox_GetHex(GetModuleHandle(NULL), m_hDlg, L"Framebuffer Address", fbAddr, fbAddr)) {
					if (IsRenderTargetBreakpoint(fbAddr)) {
						RemoveRenderTargetBreakpoint(fbAddr);
					} else {
						AddRenderTargetBreakpoint(fbAddr);
					}
				}
			}
			break;

		case IDC_GEDBG_TEXLEVELDOWN:
			UpdateTextureLevel(textureLevel_ - 1);
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_TEXLEVELUP:
			UpdateTextureLevel(textureLevel_ + 1);
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_RESUME:
			SetupPreviews();
			primaryWindow->Clear();
			secondWindow->Clear();
			SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, L"");
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"");
			SetDlgItemText(m_hDlg, IDC_GEDBG_PRIMCOUNTER, L"");

			SetBreakNext(BreakNext::NONE);
			break;

		case IDC_GEDBG_RECORD:
			GPURecord::RecordNextFrame([](const Path &path) {
				// Opens a Windows Explorer window with the file, when done.
				System_ShowFileInFolder(path);
			});
			break;

		case IDC_GEDBG_FLUSH:
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				if (!autoFlush_)
					GPU_FlushDrawing();
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_FLUSHAUTO:
			autoFlush_ = !autoFlush_;
			break;

		case IDC_GEDBG_FORCEOPAQUE:
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				forceOpaque_ = SendMessage(GetDlgItem(m_hDlg, IDC_GEDBG_FORCEOPAQUE), BM_GETCHECK, 0, 0) != 0;
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_SHOWCLUT:
			if (GPUDebug::IsActive() && gpuDebug != nullptr) {
				showClut_ = SendMessage(GetDlgItem(m_hDlg, IDC_GEDBG_SHOWCLUT), BM_GETCHECK, 0, 0) != 0;
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_SETPRIMFILTER:
		{
			std::string value = GPUDebug::GetRestrictPrims();
			if (InputBox_GetString(GetModuleHandle(NULL), m_hDlg, L"Prim counter ranges", value, value)) {
				GPUDebug::SetRestrictPrims(value.c_str());
			}
			break;
		}
		}
		break;

	case WM_GEDBG_STEPDISPLAYLIST:
		SetBreakNext(BreakNext::OP);
		break;

	case WM_GEDBG_TOGGLEPCBREAKPOINT:
		{
			GPUDebug::SetActive(true);
			u32 pc = (u32)wParam;
			bool temp;
			bool isBreak = IsAddressBreakpoint(pc, temp);
			if (isBreak && !temp) {
				if (GetAddressBreakpointCond(pc, nullptr)) {
					int ret = MessageBox(m_hDlg, L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
					if (ret == IDYES)
						RemoveAddressBreakpoint(pc);
				} else {
					RemoveAddressBreakpoint(pc);
				}
			} else {
				AddAddressBreakpoint(pc);
			}
		}
		break;

	case WM_GEDBG_RUNTOWPARAM:
		{
			GPUDebug::SetActive(true);
			u32 pc = (u32)wParam;
			AddAddressBreakpoint(pc, true);
			SendMessage(m_hDlg,WM_COMMAND,IDC_GEDBG_RESUME,0);
		}
		break;

	case WM_GEDBG_SETCMDWPARAM:
		GPU_SetCmdValue((u32)wParam);
		break;

	case WM_GEDBG_UPDATE_WATCH:
		// Just a notification to update.
		for (GEDebuggerTab &tabState : tabStates_) {
			if (tabState.type == GETabType::WATCH)
				UpdateTab(&tabState);
		}
		break;
	}

	return FALSE;
}

void CGEDebugger::CheckTabMessage(TabControl *t, GETabPosition pos, LPARAM lParam) {
	NMHDR *msg = (LPNMHDR)lParam;
	if (msg->code != NM_RCLICK)
		return;

	POINT cursorPos;
	GetCursorPos(&cursorPos);
	int tabIndex = t->HitTest(cursorPos);
	if (tabIndex == -1)
		return;

	// Find the tabState that was clicked on.
	GEDebuggerTab *tab = nullptr;
	int tabStateIndex = 0;
	for (int i = 0; i < (int)tabStates_.size(); ++i) {
		GEDebuggerTab &tabState = tabStates_[i];
		int foundIndex = HasTabIndex(&tabState, pos);
		if (foundIndex == tabIndex) {
			tab = &tabState;
			tabStateIndex = i;
			break;
		}
	}
	// Shouldn't normally happen... maybe we added some other type of tab.
	if (!tab)
		return;

	int currentPanels = 0;
	for (int i = 0; i < (int)GEPanelIndex::COUNT; ++i) {
		if (tab->state[i].index != -1 && tab->state[i].ptr)
			currentPanels++;
	}

	HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_TABS);
	static const int itemIDs[] = { ID_GEDBG_SHOWONLEFT, ID_GEDBG_SHOWONRIGHT, ID_GEDBG_SHOWONTOPRIGHT };
	for (int i = 0; i < (int)GEPanelIndex::COUNT; ++i) {
		bool active = tab->state[i].index != -1 && tab->state[i].ptr;
		bool disabled = active && currentPanels == 1;
		CheckMenuItem(subMenu, itemIDs[i], active ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(subMenu, itemIDs[i], disabled ? MF_GRAYED : MF_ENABLED);
	}

	auto toggleState = [&](GEPanelIndex i, GETabPosition pos, uint32_t &configured) {
		auto &state = tab->state[(int)i];
		bool removing = state.index != -1 && state.ptr;
		if (removing) {
			RemoveTab(tab, pos);
			configured &= ~(1 << tabStateIndex);
		} else {
			AddTab(tab, pos);
			configured |= 1 << tabStateIndex;
		}

		RECT rc;
		GetClientRect(m_hDlg, &rc);
		UpdateSize(rc.right - rc.left, rc.bottom - rc.top);
	};

	switch (TriggerContextMenu(ContextMenuID::GEDBG_TABS, m_hDlg, ContextPoint::FromCursor())) {
	case ID_GEDBG_SHOWONLEFT:
		toggleState(GEPanelIndex::LEFT, GETabPosition::LEFT, g_Config.uGETabsLeft);
		break;

	case ID_GEDBG_SHOWONRIGHT:
		toggleState(GEPanelIndex::RIGHT, GETabPosition::RIGHT, g_Config.uGETabsRight);
		break;

	case ID_GEDBG_SHOWONTOPRIGHT:
		toggleState(GEPanelIndex::TOPRIGHT, GETabPosition::TOPRIGHT, g_Config.uGETabsTopRight);
		break;

	default:
		// Cancel, that's fine.
		break;
	}
}

void CGEDebugger::UpdateMenus() {
	CheckMenuItem(GetMenu(m_hDlg), IDC_GEDBG_FLUSHAUTO, MF_BYCOMMAND | (autoFlush_ ? MF_CHECKED : MF_UNCHECKED));
}
