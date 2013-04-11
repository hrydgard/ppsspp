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
#include "Compare.h"

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
	VFSRegister("", new DirectoryAssetReader("../Windows/assets/"));
	VFSRegister("", new DirectoryAssetReader("../Windows/"));

	gl_lost_manager_init();

	// See SendDebugOutput() for how things get back on track.
}

void WindowsHeadlessHost::SendDebugOutput(const std::string &output)
{
	fprintf_s(out, "%s", output.c_str());
	OutputDebugString(output.c_str());
}

void WindowsHeadlessHost::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h)
{
	// We ignore the current framebuffer parameters and just grab the full screen.
	const static int FRAME_WIDTH = 512;
	const static int FRAME_HEIGHT = 272;
	u8 *pixels = new u8[FRAME_WIDTH * FRAME_HEIGHT * 4];

	// TODO: Maybe this code should be moved into GLES_GPU.
	glReadBuffer(GL_FRONT);
	glReadPixels(0, 0, FRAME_WIDTH, FRAME_HEIGHT, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	std::string error;
	double errors = CompareScreenshot(pixels, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH, comparisonScreenshot, error);
	if (errors < 0)
		fprintf_s(out, "%s\n", error.c_str());

	if (errors > 0)
	{
		fprintf_s(out, "Screenshot error: %f%%\n", errors * 100.0f);

		// Lazy, just read in the original header to output the failed screenshot.
		u8 header[14 + 40] = {0};
		FILE *bmp = fopen(comparisonScreenshot.c_str(), "rb");
		if (bmp)
		{
			fread(&header, sizeof(header), 1, bmp);
			fclose(bmp);
		}

		FILE *saved = fopen("__testfailure.bmp", "wb");
		if (saved)
		{
			fwrite(&header, sizeof(header), 1, saved);
			fwrite(pixels, sizeof(u32), FRAME_WIDTH * FRAME_HEIGHT, saved);
			fclose(saved);

			fprintf_s(out, "Actual output written to: __testfailure.bmp\n");
		}
	}

	delete [] pixels;
}

void WindowsHeadlessHost::SetComparisonScreenshot(const std::string &filename)
{
	comparisonScreenshot = filename;
}

bool WindowsHeadlessHost::InitGL(std::string *error_message)
{
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

#define ENFORCE(x, msg) { if (!(x)) { fprintf(stderr, msg); *error_message = msg; return false; } }

	ENFORCE(hDC = GetDC(hWnd), "Unable to create DC.");
	ENFORCE(pixelFormat = ChoosePixelFormat(hDC, &pfd), "Unable to match pixel format.");
	ENFORCE(SetPixelFormat(hDC, pixelFormat, &pfd), "Unable to set pixel format.");
	ENFORCE(hRC = wglCreateContext(hDC), "Unable to create GL context.");
	ENFORCE(wglMakeCurrent(hDC, hRC), "Unable to activate GL context.");

	SetVSync(0);

	glewInit();
	glstate.Initialize();

	LoadNativeAssets();

	return ResizeGL();
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

	glstate.viewport.set(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glstate.viewport.restore();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, WINDOW_WIDTH, WINDOW_HEIGHT, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	return true;
}

void WindowsHeadlessHost::SwapBuffers()
{
	::SwapBuffers(hDC);
}
