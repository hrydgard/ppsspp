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
#include "GPU/Directx9/helper/global.h"
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

	DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	return CreateWindowEx(0, _T("PPSSPPHeadless"), _T("PPSSPPHeadless"), style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, NULL, NULL);
}

void WindowsHeadlessHostDx9::LoadNativeAssets()
{
	// Native is kinda talkative, but that's annoying in our case.
	out = _fdopen(_dup(_fileno(stdout)), "wt");
	freopen("NUL", "wt", stdout);

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));
	VFSRegister("", new DirectoryAssetReader("../Windows/assets/"));
	VFSRegister("", new DirectoryAssetReader("../Windows/"));

	// See SendDebugOutput() for how things get back on track.
}

void WindowsHeadlessHostDx9::SendDebugOutput(const std::string &output)
{
	fwrite(output.data(), sizeof(char), output.length(), out);
	OutputDebugStringUTF8(output.c_str());
}

void WindowsHeadlessHostDx9::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h)
{
	
}

void WindowsHeadlessHostDx9::SetComparisonScreenshot(const std::string &filename)
{
	comparisonScreenshot = filename;
}

bool WindowsHeadlessHostDx9::InitGL(std::string *error_message)
{
	hWnd = DxCreateWindow();

	ShowWindow(hWnd, TRUE);
	SetFocus(hWnd);

	DX9::DirectxInit(hWnd);

	LoadNativeAssets();

	
	DX9::pD3Ddevice->BeginScene();   

	return true;
}

void WindowsHeadlessHostDx9::ShutdownGL()
{
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
