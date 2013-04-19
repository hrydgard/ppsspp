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

#pragma once

#include "StubHost.h"

#undef HEADLESSHOST_CLASS
#define HEADLESSHOST_CLASS WindowsHeadlessHost

#include <windows.h>

// TODO: Get rid of this junk
class WindowsHeadlessHost : public HeadlessHost
{
public:
	virtual bool InitGL(std::string *error_message);
	virtual void ShutdownGL();

	virtual void SwapBuffers();

	virtual void SendDebugOutput(const std::string &output);
	virtual void SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h);
	virtual void SetComparisonScreenshot(const std::string &filename);

private:
	bool ResizeGL();
	void LoadNativeAssets();

	HWND hWnd;
	HDC hDC;
	HGLRC hRC;
	FILE *out;
	std::string comparisonScreenshot;
};