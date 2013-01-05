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

#include "WindowsHeadlessHost.h"

#include <stdio.h>
#include <windows.h>
#include <io.h>

#include "gfx_es2/gl_state.h"
#include "gfx/gl_common.h"
#include "gfx/gl_lost_manager.h"
#include "file/vfs.h"
#include "file/zip_read.h"

const bool WINDOW_VISIBLE = false;
const int WINDOW_WIDTH = 480;
const int WINDOW_HEIGHT = 272;

typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALFARPROC)(int value);
PFNWGLSWAPINTERVALFARPROC wglSwapIntervalEXT = NULL;

HWND CreateHiddenWindow()
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
		"PPSSPPHeadless",
		NULL,
	};
	RegisterClassEx(&wndClass);

	DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	return CreateWindowEx(0, "PPSSPPHeadless", "PPSSPPHeadless", style, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, NULL, NULL);
}

void SetVSync(int value)
{
	const char *extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (!strstr(extensions, "WGL_EXT_swap_control"))
		return;

	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALFARPROC) wglGetProcAddress("wglSwapIntervalEXT");
	if (wglSwapIntervalEXT != NULL)
		wglSwapIntervalEXT(value);
}

void WindowsHeadlessHost::LoadNativeAssets()
{
	// Native is kinda talkative, but that's annoying in our case.
	out = _fdopen(_dup(_fileno(stdout)), "wt");
	freopen("NUL", "wt", stdout);

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));

	gl_lost_manager_init();

	// See SendDebugOutput() for how things get back on track.
}

void WindowsHeadlessHost::SendDebugOutput(const std::string &output)
{
	fprintf_s(out, "%s", output.c_str());
	OutputDebugString(output.c_str());
}

void WindowsHeadlessHost::InitGL()
{
	glOkay = false;
	hWnd = CreateHiddenWindow();

	if (WINDOW_VISIBLE)
	{
		ShowWindow(hWnd, TRUE);
		SetFocus(hWnd);
	}

	int pixelFormat;

	static PIXELFORMATDESCRIPTOR pfd = {0};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 16;
	pfd.iLayerType = PFD_MAIN_PLANE;

#define ENFORCE(x, msg) { if (!(x)) { fprintf(stderr, msg); return; } }

	ENFORCE(hDC = GetDC(hWnd), "Unable to create DC.");
	ENFORCE(pixelFormat = ChoosePixelFormat(hDC, &pfd), "Unable to match pixel format.");
	ENFORCE(SetPixelFormat(hDC, pixelFormat, &pfd), "Unable to set pixel format.");
	ENFORCE(hRC = wglCreateContext(hDC), "Unable to create GL context.");
	ENFORCE(wglMakeCurrent(hDC, hRC), "Unable to activate GL context.");

	SetVSync(0);

	glewInit();
	glstate.Initialize();

	LoadNativeAssets();

	if (ResizeGL())
		glOkay = true;
}

void WindowsHeadlessHost::ShutdownGL()
{
	if (hRC)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(hRC);
		hRC = NULL;
	}

	if (hDC)
		ReleaseDC(hWnd, hDC);
	hDC = NULL;
	DestroyWindow(hWnd);
	hWnd = NULL;
}

bool WindowsHeadlessHost::ResizeGL()
{
	if (!hWnd)
		return false;

	RECT rc;
	GetWindowRect(hWnd, &rc);

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, WINDOW_WIDTH, WINDOW_HEIGHT, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	return true;
}

void WindowsHeadlessHost::BeginFrame()
{

}

void WindowsHeadlessHost::EndFrame()
{
	SwapBuffers(hDC);
}
