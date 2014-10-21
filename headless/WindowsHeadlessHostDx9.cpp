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

#include "WindowsHeadlessHostDx9.h"
#include "Compare.h"

#include <stdio.h>
#include "Common/CommonWindows.h"
#include <io.h>

#include "base/logging.h"
#include "thin3d/d3dx9_loader.h"
#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/helper/fbo.h"
#include "file/vfs.h"
#include "file/zip_read.h"

const bool WINDOW_VISIBLE = false;
const int WINDOW_WIDTH = 480;
const int WINDOW_HEIGHT = 272;

HWND DxCreateWindow()
{
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
		_T("PPSSPPHeadless"),
		NULL,
	};
	RegisterClassEx(&wndClass);

	RECT wr = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};    // set the size, but not the position
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);    // adjust the size

	DWORD style = WS_OVERLAPPEDWINDOW | (WINDOW_VISIBLE ? WS_VISIBLE : 0);
	return CreateWindowEx(0, _T("PPSSPPHeadless"), _T("PPSSPPHeadless"), style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, NULL, NULL);
}

bool WindowsHeadlessHostDx9::InitGraphics(std::string *error_message)
{
	LoadD3DX9Dynamic();
	hWnd = DxCreateWindow();

	if (WINDOW_VISIBLE)
	{
		ShowWindow(hWnd, TRUE);
		SetFocus(hWnd);
	}

	DX9::DirectxInit(hWnd);

	LoadNativeAssets();

	
	DX9::pD3Ddevice->BeginScene();   

	return true;
}

void WindowsHeadlessHostDx9::ShutdownGraphics()
{
	DX9::DestroyShaders();
	DX9::fbo_shutdown();
	DX9::pD3Ddevice->EndScene();
	DX9::pD3Ddevice->Release();
	DX9::pD3Ddevice = NULL;
	hWnd = NULL;
}

bool WindowsHeadlessHostDx9::ResizeGL()
{

	return true;
}

void WindowsHeadlessHostDx9::SwapBuffers()
{	
	MSG msg;
	PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
	TranslateMessage(&msg);
	DispatchMessage(&msg);

	DX9::pD3Ddevice->EndScene(); 
	DX9::pD3Ddevice->Present(0, 0, 0, 0);
	DX9::pD3Ddevice->BeginScene();   
}
