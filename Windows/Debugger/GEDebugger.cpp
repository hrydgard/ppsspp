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

#include "native/base/mutex.h"
#include "Windows/Debugger/GEDebugger.h"
#include "Windows/WindowsHost.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

static bool attached = false;
// TODO
static bool textureCaching = true;
static recursive_mutex pauseLock;
static condition_variable pauseWait;

static bool breakNext = false;

CGEDebugger::CGEDebugger(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDEBUGGER, _hInstance, _hParent) {
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
			breakNext = true;
			break;

		case IDC_GEDBG_RESUME:
			// TODO: detach?  Should probably have separate UI, or just on activate?
			breakNext = false;
			pauseWait.notify_one();
			break;
		}
	}

	return FALSE;
}

// The below WindowsHost methods are called on the GPU thread.

bool WindowsHost::GPUDebuggingActive() {
	return attached;
}

void WindowsHost::GPUNotifyCommand(u32 pc) {
	if (breakNext) {
		auto info = gpuDebug->DissassembleOp(pc);
		NOTICE_LOG(HLE, "waiting at %08x, %s", pc, info.desc.c_str());
		lock_guard guard(pauseLock);
		pauseWait.wait(pauseLock);
	}
}

void WindowsHost::GPUNotifyDisplay(u32 framebuf, u32 stride, int format) {
}

void WindowsHost::GPUNotifyDraw() {
}

void WindowsHost::GPUNotifyTextureAttachment(u32 addr) {
}

bool WindowsHost::GPUAllowTextureCache(u32 addr) {
	return textureCaching;
}