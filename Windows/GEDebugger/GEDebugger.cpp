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

#include "native/base/mutex.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Windows/GEDebugger/CtrlDisplayListView.h"
#include "Windows/GEDebugger/TabDisplayLists.h"
#include "Windows/GEDebugger/TabState.h"
#include "Windows/WindowsHost.h"
#include "Windows/WndMainWindow.h"
#include "Windows/main.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include <windowsx.h>
#include <commctrl.h>

enum PauseAction {
	PAUSE_CONTINUE,
	PAUSE_GETFRAMEBUF,
	PAUSE_GETTEX,
};

static bool attached = false;
// TODO
static bool textureCaching = true;
static recursive_mutex pauseLock;
static condition_variable pauseWait;
static PauseAction pauseAction = PAUSE_CONTINUE;
static recursive_mutex actionLock;
static condition_variable actionWait;

static std::vector<bool> breakCmds;
static std::set<u32> breakPCs;
static bool breakNextOp = false;
static bool breakNextDraw = false;

static bool bufferResult;
static GPUDebugBuffer bufferFrame;
static GPUDebugBuffer bufferTex;

// TODO: Simplify and move out of windows stuff, just block in a common way for everyone.

void CGEDebugger::Init() {
	SimpleGLWindow::RegisterClass();
	CtrlDisplayListView::registerClass();
}

static void SetPauseAction(PauseAction act) {
	{
		lock_guard guard(pauseLock);
		actionLock.lock();
		pauseAction = act;
	}

	pauseWait.notify_one();
	actionWait.wait(actionLock);
	actionLock.unlock();
}

static void RunPauseAction() {
	lock_guard guard(actionLock);

	switch (pauseAction) {
	case PAUSE_CONTINUE:
		// Don't notify, just go back, woke up by accident.
		return;

	case PAUSE_GETFRAMEBUF:
		bufferResult = gpuDebug->GetCurrentFramebuffer(bufferFrame);
		break;

	case PAUSE_GETTEX:
		bufferResult = gpuDebug->GetCurrentTexture(bufferTex);
		break;
	}

	actionWait.notify_one();
	pauseAction = PAUSE_CONTINUE;
}

static void ForceUnpause() {
	lock_guard guard(pauseLock);
	lock_guard actionGuard(actionLock);
	pauseAction = PAUSE_CONTINUE;
	pauseWait.notify_one();
}

CGEDebugger::CGEDebugger(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDEBUGGER, _hInstance, _hParent), frameWindow(NULL), texWindow(NULL) {
	breakCmds.resize(256, false);
	Core_ListenShutdown(ForceUnpause);

	// minimum size = a little more than the default
	RECT windowRect;
	GetWindowRect(m_hDlg,&windowRect);
	minWidth = windowRect.right-windowRect.left+10;
	minHeight = windowRect.bottom-windowRect.top+10;

	// it's ugly, but .rc coordinates don't match actual pixels and it screws
	// up both the size and the aspect ratio
	// TODO: Could be scrollable in case the framebuf is larger?  Also should be better positioned.
	RECT frameRect;
	HWND frameWnd = GetDlgItem(m_hDlg,IDC_GEDBG_FRAME);

	GetWindowRect(frameWnd,&frameRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&frameRect,2);
	MoveWindow(frameWnd,frameRect.left,frameRect.top,512,272,TRUE);

	tabs = new TabControl(GetDlgItem(m_hDlg,IDC_GEDBG_MAINTAB));
	HWND wnd = tabs->AddTabWindow(L"CtrlDisplayListView",L"Display List");
	displayList = CtrlDisplayListView::getFrom(wnd);

	flags = new TabStateFlags(_hInstance, m_hDlg);
	tabs->AddTabDialog(flags, L"Flags");

	lighting = new TabStateLighting(_hInstance, m_hDlg);
	tabs->AddTabDialog(lighting, L"Lighting");

	textureState = new TabStateTexture(_hInstance, m_hDlg);
	tabs->AddTabDialog(textureState, L"Texture");

	settings = new TabStateSettings(_hInstance, m_hDlg);
	tabs->AddTabDialog(settings, L"Settings");

	lists = new TabDisplayLists(_hInstance, m_hDlg);
	tabs->AddTabDialog(lists, L"Lists");

	tabs->ShowTab(0, true);

	// set window position
	int x = g_Config.iGEWindowX == -1 ? windowRect.left : g_Config.iGEWindowX;
	int y = g_Config.iGEWindowY == -1 ? windowRect.top : g_Config.iGEWindowY;
	int w = g_Config.iGEWindowW == -1 ? minWidth : g_Config.iGEWindowW;
	int h = g_Config.iGEWindowH == -1 ? minHeight : g_Config.iGEWindowH;
	MoveWindow(m_hDlg,x,y,w,h,FALSE);
}

CGEDebugger::~CGEDebugger() {
	delete frameWindow;
	delete texWindow;

	delete flags;
	delete lighting;
	delete textureState;
	delete settings;
	delete lists;
	delete tabs;
}

void CGEDebugger::SetupPreviews() {
	if (frameWindow == NULL) {
		frameWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_FRAME));
		frameWindow->Initialize(SimpleGLWindow::ALPHA_IGNORE | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		// TODO: Why doesn't this work?
		frameWindow->Clear();
	}
	if (texWindow == NULL) {
		texWindow = SimpleGLWindow::GetFrom(GetDlgItem(m_hDlg, IDC_GEDBG_TEX));
		texWindow->Initialize(SimpleGLWindow::ALPHA_BLEND | SimpleGLWindow::RESIZE_SHRINK_CENTER);
		// TODO: Why doesn't this work?
		texWindow->Clear();
	}
}

void CGEDebugger::UpdatePreviews() {
	// TODO: Do something different if not paused?

	bufferResult = false;
	SetPauseAction(PAUSE_GETFRAMEBUF);

	if (bufferResult) {
		auto fmt = SimpleGLWindow::Format(bufferFrame.GetFormat());
		frameWindow->Draw(bufferFrame.GetData(), bufferFrame.GetStride(), bufferFrame.GetHeight(), bufferFrame.GetFlipped(), fmt);
	} else {
		ERROR_LOG(COMMON, "Unable to get framebuffer.");
		frameWindow->Clear();
	}

	bufferResult = false;
	SetPauseAction(PAUSE_GETTEX);

	if (bufferResult) {
		auto fmt = SimpleGLWindow::Format(bufferTex.GetFormat());
		texWindow->Draw(bufferTex.GetData(), bufferTex.GetStride(), bufferTex.GetHeight(), bufferTex.GetFlipped(), fmt);
	} else {
		ERROR_LOG(COMMON, "Unable to get texture.");
		texWindow->Clear();
	}

	DisplayList list;
	if (gpuDebug->GetCurrentDisplayList(list)) {
		displayList->setDisplayList(list);
	}

	flags->Update();
	lighting->Update();
	textureState->Update();
	settings->Update();
	lists->Update();
}

void CGEDebugger::UpdateSize(WORD width, WORD height)
{
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
			break;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_GEDBG_STEPDRAW:
			attached = true;
			SetupPreviews();

			pauseWait.notify_one();
			breakNextOp = false;
			breakNextDraw = true;
			break;

		case IDC_GEDBG_STEP:
			SendMessage(m_hDlg,WM_GEDBG_STEPDISPLAYLIST,0,0);
			break;

		case IDC_GEDBG_RESUME:
			frameWindow->Clear();
			texWindow->Clear();
			// TODO: detach?  Should probably have separate UI, or just on activate?
			breakNextOp = false;
			breakNextDraw = false;
			pauseWait.notify_one();
			break;
		}
		break;

	case WM_GEDBG_BREAK_CMD:
		{
			u32 pc = (u32)wParam;
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
		attached = true;
		SetupPreviews();

		pauseWait.notify_one();
		breakNextOp = true;
		breakNextDraw = false;
		break;
	}

	return FALSE;
}

// The below WindowsHost methods are called on the GPU thread.

bool WindowsHost::GPUDebuggingActive() {
	return attached;
}

static void PauseWithMessage(UINT msg, WPARAM wParam = NULL, LPARAM lParam = NULL) {
	lock_guard guard(pauseLock);
	if (Core_IsInactive()) {
		return;
	}

	PostMessage(geDebuggerWindow->GetDlgHandle(), msg, wParam, lParam);

	do {
		RunPauseAction();
		pauseWait.wait(pauseLock);
	} while (pauseAction != PAUSE_CONTINUE);
}

void WindowsHost::GPUNotifyCommand(u32 pc) {
	u32 op = Memory::ReadUnchecked_U32(pc);
	u8 cmd = op >> 24;

	const bool breakPC = breakPCs.find(pc) != breakPCs.end();
	if (breakNextOp || breakCmds[cmd] || breakPC) {
		PauseWithMessage(WM_GEDBG_BREAK_CMD, (WPARAM) pc);
	}
}

void WindowsHost::GPUNotifyDisplay(u32 framebuf, u32 stride, int format) {
}

void WindowsHost::GPUNotifyDraw() {
	if (breakNextDraw) {
		PauseWithMessage(WM_GEDBG_BREAK_DRAW);
	}
}

void WindowsHost::GPUNotifyTextureAttachment(u32 addr) {
}

bool WindowsHost::GPUAllowTextureCache(u32 addr) {
	return textureCaching;
}