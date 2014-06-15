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

#include <vector>
#include <string>
#include <set>

#include "base/functional.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Windows/GEDebugger/CtrlDisplayListView.h"
#include "Windows/GEDebugger/TabDisplayLists.h"
#include "Windows/GEDebugger/TabState.h"
#include "Windows/GEDebugger/TabVertices.h"
#include "Windows/InputBox.h"
#include "Windows/WindowsHost.h"
#include "Windows/WndMainWindow.h"
#include "Windows/main.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Stepping.h"
#include "Core/Config.h"
#include <windowsx.h>
#include <commctrl.h>

using namespace GPUBreakpoints;
using namespace GPUStepping;

static bool attached = false;
// TODO
static bool textureCaching = true;

static BreakNextType breakNext = BREAK_NONE;

enum PrimaryDisplayType {
	PRIMARY_FRAMEBUF,
	PRIMARY_DEPTHBUF,
	PRIMARY_STENCILBUF,
};

void CGEDebugger::Init() {
	SimpleGLWindow::RegisterClass();
	CtrlDisplayListView::registerClass();
}

CGEDebugger::CGEDebugger(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDEBUGGER, _hInstance, _hParent), frameWindow(NULL), texWindow(NULL), textureLevel_(0) {
	GPUBreakpoints::Init();
	Core_ListenShutdown(ForceUnpause);

	// minimum size = a little more than the default
	RECT windowRect;
	GetWindowRect(m_hDlg,&windowRect);
	minWidth = windowRect.right-windowRect.left+10;
	minHeight = windowRect.bottom-windowRect.top+10;

	// it's ugly, but .rc coordinates don't match actual pixels and it screws
	// up both the size and the aspect ratio
	RECT frameRect;
	HWND frameWnd = GetDlgItem(m_hDlg,IDC_GEDBG_FRAME);

	GetWindowRect(frameWnd,&frameRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&frameRect,2);
	MoveWindow(frameWnd,frameRect.left,frameRect.top,512,272,TRUE);

	tabs = new TabControl(GetDlgItem(m_hDlg,IDC_GEDBG_MAINTAB));
	HWND wnd = tabs->AddTabWindow(L"CtrlDisplayListView",L"Display List");
	displayList = CtrlDisplayListView::getFrom(wnd);

	fbTabs = new TabControl(GetDlgItem(m_hDlg, IDC_GEDBG_FBTABS));
	fbTabs->SetMinTabWidth(50);
	// Must be in the same order as PrimaryDisplayType.
	fbTabs->AddTab(NULL, L"Color");
	fbTabs->AddTab(NULL, L"Depth");
	fbTabs->AddTab(NULL, L"Stencil");
	fbTabs->ShowTab(0, true);

	flags = new TabStateFlags(_hInstance, m_hDlg);
	tabs->AddTabDialog(flags, L"Flags");

	lighting = new TabStateLighting(_hInstance, m_hDlg);
	tabs->AddTabDialog(lighting, L"Lighting");

	textureState = new TabStateTexture(_hInstance, m_hDlg);
	tabs->AddTabDialog(textureState, L"Texture");

	settings = new TabStateSettings(_hInstance, m_hDlg);
	tabs->AddTabDialog(settings, L"Settings");

	vertices = new TabVertices(_hInstance, m_hDlg);
	tabs->AddTabDialog(vertices, L"Vertices");

	matrices = new TabMatrices(_hInstance, m_hDlg);
	tabs->AddTabDialog(matrices, L"Matrices");

	lists = new TabDisplayLists(_hInstance, m_hDlg);
	tabs->AddTabDialog(lists, L"Lists");

	tabs->ShowTab(0, true);

	// set window position
	int x = g_Config.iGEWindowX == -1 ? windowRect.left : g_Config.iGEWindowX;
	int y = g_Config.iGEWindowY == -1 ? windowRect.top : g_Config.iGEWindowY;
	int w = g_Config.iGEWindowW == -1 ? minWidth : g_Config.iGEWindowW;
	int h = g_Config.iGEWindowH == -1 ? minHeight : g_Config.iGEWindowH;
	MoveWindow(m_hDlg,x,y,w,h,FALSE);

	UpdateTextureLevel(textureLevel_);
}

CGEDebugger::~CGEDebugger() {
	DestroyWindow(displayList->GetHWND());
	CleanupPrimPreview();
	delete flags;
	delete lighting;
	delete textureState;
	delete settings;
	delete vertices;
	delete matrices;
	delete lists;
	delete tabs;
	delete fbTabs;
}

void CGEDebugger::SetupPreviews() {
	if (frameWindow == NULL) {
		frameWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_FRAME));
		frameWindow->Initialize(SimpleGLWindow::ALPHA_IGNORE | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		frameWindow->Clear();
	}
	if (texWindow == NULL) {
		texWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_TEX));
		texWindow->Initialize(SimpleGLWindow::ALPHA_BLEND | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		texWindow->Clear();
	}
}

void CGEDebugger::UpdatePreviews() {
	wchar_t desc[256];
	const GPUDebugBuffer *primaryBuffer = NULL;
	bool bufferResult = false;
	GPUgstate state = {0};

	if (gpuDebug != NULL) {
		state = gpuDebug->GetGState();
	}

	switch (PrimaryDisplayType(fbTabs->CurrentTabIndex())) {
	case PRIMARY_FRAMEBUF:
		bufferResult = GPU_GetCurrentFramebuffer(primaryBuffer);
		if (bufferResult) {
			_snwprintf(desc, ARRAY_SIZE(desc), L"Color: 0x%08x (%dx%d) fmt %i", state.getFrameBufRawAddress(), primaryBuffer->GetStride(), primaryBuffer->GetHeight(), state.FrameBufFormat());
		}
		break;

	case PRIMARY_DEPTHBUF:
		bufferResult = GPU_GetCurrentDepthbuffer(primaryBuffer);
		if (bufferResult) {
			_snwprintf(desc, ARRAY_SIZE(desc), L"Depth: 0x%08x (%dx%d)", state.getDepthBufRawAddress(), primaryBuffer->GetStride(), primaryBuffer->GetHeight());
		}
		break;

	case PRIMARY_STENCILBUF:
		bufferResult = GPU_GetCurrentStencilbuffer(primaryBuffer);
		if (bufferResult) {
			_snwprintf(desc, ARRAY_SIZE(desc), L"Stencil: 0x%08x (%dx%d)", state.getFrameBufRawAddress(), primaryBuffer->GetStride(), primaryBuffer->GetHeight());
		}
		break;
	}

	if (bufferResult && primaryBuffer != NULL) {
		auto fmt = SimpleGLWindow::Format(primaryBuffer->GetFormat());
		frameWindow->Draw(primaryBuffer->GetData(), primaryBuffer->GetStride(), primaryBuffer->GetHeight(), primaryBuffer->GetFlipped(), fmt);
		SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, desc);
	} else if (frameWindow != NULL) {
		frameWindow->Clear();
		SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, L"Failed");
	}

	const GPUDebugBuffer *bufferTex = NULL;
	UpdateTextureLevel(textureLevel_);
	bufferResult = GPU_GetCurrentTexture(bufferTex, textureLevel_);

	if (bufferResult) {
		auto fmt = SimpleGLWindow::Format(bufferTex->GetFormat());
		texWindow->Draw(bufferTex->GetData(), bufferTex->GetStride(), bufferTex->GetHeight(), bufferTex->GetFlipped(), fmt);

		if (gpuDebug != NULL) {
			if (state.isTextureAlphaUsed()) {
				texWindow->SetFlags(SimpleGLWindow::ALPHA_BLEND | SimpleGLWindow::RESIZE_SHRINK_CENTER);
			} else {
				texWindow->SetFlags(SimpleGLWindow::RESIZE_SHRINK_CENTER);
			}
			_snwprintf(desc, ARRAY_SIZE(desc), L"Texture L%d: 0x%08x (%dx%d)", textureLevel_, state.getTextureAddress(textureLevel_), state.getTextureWidth(textureLevel_), state.getTextureHeight(textureLevel_));
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, desc);

			UpdateLastTexture(state.getTextureAddress(textureLevel_));
		} else {
			UpdateLastTexture((u32)-1);
		}
	} else if (texWindow != NULL) {
		texWindow->Clear();
		if (gpuDebug == NULL || state.isTextureMapEnabled()) {
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"Texture: failed");
		} else {
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"Texture: disabled");
		}
		UpdateLastTexture((u32)-1);
	}

	DisplayList list;
	if (gpuDebug != NULL && gpuDebug->GetCurrentDisplayList(list)) {
		const u32 op = Memory::Read_U32(list.pc);
		const u32 cmd = op >> 24;
		// TODO: Bezier/spline?
		if (cmd == GE_CMD_PRIM) {
			UpdatePrimPreview(op);
		}

		displayList->setDisplayList(list);
	}

	flags->Update();
	lighting->Update();
	textureState->Update();
	settings->Update();
	vertices->Update();
	matrices->Update();
	lists->Update();
}

void CGEDebugger::UpdateTextureLevel(int level) {
	GPUgstate state = {0};
	if (gpuDebug != NULL) {
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
	// only resize the tab for now
	HWND tabControl = GetDlgItem(m_hDlg, IDC_GEDBG_MAINTAB);

	RECT tabRect;
	GetWindowRect(tabControl,&tabRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&tabRect,2);

	tabRect.right = tabRect.left + (width-tabRect.left*2);				// assume same gap on both sides
	tabRect.bottom = tabRect.top + (height-tabRect.top-tabRect.left);	// assume same gap on bottom too
	MoveWindow(tabControl,tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,TRUE);
}

void CGEDebugger::SavePosition()
{
	RECT rc;
	if (GetWindowRect(m_hDlg, &rc))
	{
		g_Config.iGEWindowX = rc.left;
		g_Config.iGEWindowY = rc.top;
		g_Config.iGEWindowW = rc.right - rc.left;
		g_Config.iGEWindowH = rc.bottom - rc.top;
	}
}

void CGEDebugger::SetBreakNext(BreakNextType type) {
	attached = true;
	SetupPreviews();

	breakNext = type;
	ResumeFromStepping();
}

BOOL CGEDebugger::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO* minmax = (MINMAXINFO*) lParam;
			minmax->ptMinTrackSize.x = minWidth;
			minmax->ptMinTrackSize.y = minHeight;
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
		attached = false;
		ResumeFromStepping();
		breakNext = BREAK_NONE;

		Show(false);
		return TRUE;

	case WM_SHOWWINDOW:
		SetupPreviews();
		break;

	case WM_ACTIVATE:
		if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
			g_activeWindow = WINDOW_GEDEBUGGER;
		}
		break;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_MAINTAB:
			tabs->HandleNotify(lParam);
			if (gpuDebug != NULL) {
				lists->Update();
			}
			break;
		case IDC_GEDBG_FBTABS:
			fbTabs->HandleNotify(lParam);
			if (attached && gpuDebug != NULL) {
				UpdatePreviews();
			}
			break;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_GEDBG_STEPDRAW:
			SetBreakNext(BREAK_NEXT_DRAW);
			break;

		case IDC_GEDBG_STEP:
			SetBreakNext(BREAK_NEXT_OP);
			break;

		case IDC_GEDBG_STEPTEX:
			AddTextureChangeTempBreakpoint();
			SetBreakNext(BREAK_NEXT_TEX);
			break;

		case IDC_GEDBG_STEPFRAME:
			SetBreakNext(BREAK_NEXT_FRAME);
			break;

		case IDC_GEDBG_STEPPRIM:
			AddCmdBreakpoint(GE_CMD_PRIM, true);
			AddCmdBreakpoint(GE_CMD_BEZIER, true);
			AddCmdBreakpoint(GE_CMD_SPLINE, true);
			SetBreakNext(BREAK_NEXT_PRIM);
			break;

		case IDC_GEDBG_BREAKTEX:
			{
				attached = true;
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

		case IDC_GEDBG_TEXLEVELDOWN:
			UpdateTextureLevel(textureLevel_ - 1);
			if (attached && gpuDebug != NULL) {
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_TEXLEVELUP:
			UpdateTextureLevel(textureLevel_ + 1);
			if (attached && gpuDebug != NULL) {
				UpdatePreviews();
			}
			break;

		case IDC_GEDBG_RESUME:
			frameWindow->Clear();
			texWindow->Clear();
			SetDlgItemText(m_hDlg, IDC_GEDBG_FRAMEBUFADDR, L"");
			SetDlgItemText(m_hDlg, IDC_GEDBG_TEXADDR, L"");

			ResumeFromStepping();
			breakNext = BREAK_NONE;
			break;
		}
		break;

	case WM_GEDBG_BREAK_CMD:
		{
			u32 pc = (u32)wParam;
			ClearTempBreakpoints();
			auto info = gpuDebug->DissassembleOp(pc);
			NOTICE_LOG(COMMON, "Waiting at %08x, %s", pc, info.desc.c_str());
			UpdatePreviews();
		}
		break;

	case WM_GEDBG_BREAK_DRAW:
		{
			NOTICE_LOG(COMMON, "Waiting at a draw");
			UpdatePreviews();
		}
		break;

	case WM_GEDBG_STEPDISPLAYLIST:
		SetBreakNext(BREAK_NEXT_OP);
		break;

	case WM_GEDBG_TOGGLEPCBREAKPOINT:
		{
			attached = true;
			u32 pc = (u32)wParam;
			bool temp;
			bool isBreak = IsAddressBreakpoint(pc, temp);
			if (isBreak && !temp) {
				RemoveAddressBreakpoint(pc);
			} else {
				AddAddressBreakpoint(pc);
			}
		}
		break;

	case WM_GEDBG_RUNTOWPARAM:
		{
			attached = true;
			u32 pc = (u32)wParam;
			AddAddressBreakpoint(pc, true);
			SendMessage(m_hDlg,WM_COMMAND,IDC_GEDBG_RESUME,0);
		}
		break;

	case WM_GEDBG_SETCMDWPARAM:
		{
			GPU_SetCmdValue((u32)wParam);
		}
		break;
	}

	return FALSE;
}

// The below WindowsHost methods are called on the GPU thread.

bool WindowsHost::GPUDebuggingActive() {
	return attached;
}

static void DeliverMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
	PostMessage(geDebuggerWindow->GetDlgHandle(), msg, wParam, lParam);
}

static void PauseWithMessage(UINT msg, WPARAM wParam = NULL, LPARAM lParam = NULL) {
	if (attached) {
		EnterStepping(std::bind(&DeliverMessage, msg, wParam, lParam));
	}
}

void WindowsHost::GPUNotifyCommand(u32 pc) {
	u32 op = Memory::ReadUnchecked_U32(pc);
	u8 cmd = op >> 24;

	if (breakNext == BREAK_NEXT_OP || IsBreakpoint(pc, op)) {
		PauseWithMessage(WM_GEDBG_BREAK_CMD, (WPARAM) pc);
	}
}

void WindowsHost::GPUNotifyDisplay(u32 framebuf, u32 stride, int format) {
	if (breakNext == BREAK_NEXT_FRAME) {
		// This should work fine, start stepping at the first op of the new frame.
		breakNext = BREAK_NEXT_OP;
	}
}

void WindowsHost::GPUNotifyDraw() {
	if (breakNext == BREAK_NEXT_DRAW) {
		PauseWithMessage(WM_GEDBG_BREAK_DRAW);
	}
}

void WindowsHost::GPUNotifyTextureAttachment(u32 addr) {
}

bool WindowsHost::GPUAllowTextureCache(u32 addr) {
	return textureCaching;
}