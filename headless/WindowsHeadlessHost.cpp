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

#include "ppsspp_config.h"
#include "headless/WindowsHeadlessHost.h"

#include "Common/CommonWindows.h"
#include "Common/GPU/GraphicsContext.h"

const bool WINDOW_VISIBLE = false;

void *CreateHiddenWindow(int w, int h, GPUBackend backend, WindowDesc *desc) {
	static WNDCLASSEX wndClass = {
		sizeof(WNDCLASSEX),
		CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
		DefWindowProc,
		0,
		0,
		NULL,
		NULL,
		LoadCursor(NULL, IDC_ARROW),
		(HBRUSH) GetStockObject(BLACK_BRUSH),
		NULL,
		L"PPSSPPHeadless",
		NULL,
	};
	RegisterClassEx(&wndClass);

	DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	HWND wnd = CreateWindowEx(0, L"PPSSPPHeadless", L"PPSSPPHeadless", style, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, NULL, NULL);

	if (WINDOW_VISIBLE) {
		ShowWindow(wnd, TRUE);
		SetFocus(wnd);
	}
	desc->data1 = GetModuleHandle(NULL);  // easy way to get the HINSTANCE.
	desc->data2 = wnd;
	desc->winsys = WindowSystem::WINDOWSYSTEM_WIN32;
	return static_cast<void *>(wnd);
}

void DestroyHiddenWindow(void *window, WindowDesc desc) {
	if (window) {
		DestroyWindow(static_cast<HWND>(window));
		UnregisterClass(L"PPSSPPHeadless", static_cast<HINSTANCE>(desc.data1));
	}
}
