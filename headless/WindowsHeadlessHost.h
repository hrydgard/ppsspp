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

#include "headless/HeadlessHost.h"
#include <thread>

#undef HEADLESSHOST_CLASS
#define HEADLESSHOST_CLASS WindowsHeadlessHost

#include "Common/CommonWindows.h"

class WindowsHeadlessHost : public HeadlessHost
{
public:
	bool InitGraphics(std::string *error_message, GraphicsContext **ctx, GPUCore core) override;
	void ShutdownGraphics() override;

	void SwapBuffers() override;

	void SendDebugOutput(const std::string &output) override;

protected:
	enum class RenderThreadState {
		IDLE,
		START_REQUESTED,
		STARTING,
		START_FAILED,
		STARTED,
		STOP_REQUESTED,
		STOPPING,
		STOPPED,
	};

	HWND hWnd;
	HDC hDC;
	HGLRC hRC;
	volatile RenderThreadState threadState_ = RenderThreadState::IDLE;
};
