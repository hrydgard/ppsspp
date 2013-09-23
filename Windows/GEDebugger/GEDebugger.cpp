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
#include "Windows/WindowsHost.h"
#include "Windows/main.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

const UINT WM_GEDBG_BREAK_CMD = WM_USER + 200;
const UINT WM_GEDBG_BREAK_DRAW = WM_USER + 201;

enum PauseAction {
	PAUSE_CONTINUE,
	PAUSE_GETFRAMEBUF,
	// textures, etc.
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
static GPUDebugBuffer buffer;

// TODO: Simplify and move out of windows stuff, just block in a common way for everyone.

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
		bufferResult = gpuDebug->GetCurrentFramebuffer(buffer);
		break;
	}

	actionWait.notify_one();
	pauseAction = PAUSE_CONTINUE;
}

CGEDebugger::CGEDebugger(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDEBUGGER, _hInstance, _hParent) {
	breakCmds.resize(256, false);
	// TODO: Could be scrollable in case the framebuf is larger?  Also should be better positioned.
	frameWindow = new SimpleGLWindow(m_hInstance, m_hDlg, (750 - 512) / 2, 40, 512, 272);
	// TODO: Why doesn't this work?
	frameWindow->Clear();
}

CGEDebugger::~CGEDebugger() {
	delete frameWindow;
}

BOOL CGEDebugger::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		// TODO
		return TRUE;

	case WM_CLOSE:
		Show(false);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_GEDBG_BREAK:
			attached = true;
			//breakNextOp = true;
			pauseWait.notify_one();
			breakNextDraw = true;
			// TODO
			break;

		case IDC_GEDBG_RESUME:
			frameWindow->Clear();
			// TODO: detach?  Should probably have separate UI, or just on activate?
			//breakNextOp = false;
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
		}
		break;

	case WM_GEDBG_BREAK_DRAW:
		{
			NOTICE_LOG(COMMON, "Waiting at a draw");

			bufferResult = false;
			SetPauseAction(PAUSE_GETFRAMEBUF);

			if (bufferResult) {
				auto fmt = SimpleGLWindow::Format(buffer.GetFormat());
				frameWindow->Draw(buffer.GetData(), buffer.GetStride(), buffer.GetHeight(), buffer.GetFlipped(), fmt, SimpleGLWindow::RESIZE_SHRINK_CENTER);
			} else {
				ERROR_LOG(COMMON, "Unable to get buffer.");
			}
		}
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