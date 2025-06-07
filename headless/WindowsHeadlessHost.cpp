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
#include <cstdio>

#include "headless/WindowsHeadlessHost.h"

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"

#include "Common/CommonWindows.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"

#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#if PPSSPP_API(ANY_GL)
#include "Windows/GPU/WindowsGLContext.h"
#endif
#include "Windows/GPU/D3D11Context.h"
#include "Windows/GPU/WindowsVulkanContext.h"

const bool WINDOW_VISIBLE = false;
const int WINDOW_WIDTH = 480;
const int WINDOW_HEIGHT = 272;

HWND CreateHiddenWindow() {
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
	return CreateWindowEx(0, L"PPSSPPHeadless", L"PPSSPPHeadless", style, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, NULL, NULL);
}

void WindowsHeadlessHost::SendDebugOutput(const std::string &output) {
	if (writeDebugOutput_)
		fwrite(output.data(), sizeof(char), output.length(), stdout);
	OutputDebugStringUTF8(output.c_str());
}

bool WindowsHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx, GPUCore core) {
	hWnd = CreateHiddenWindow();
	gpuCore_ = core;

	if (WINDOW_VISIBLE) {
		ShowWindow(hWnd, TRUE);
		SetFocus(hWnd);
	}

	WindowsGraphicsContext *graphicsContext = nullptr;
	bool needRenderThread = false;
	switch (gpuCore_) {
	case GPUCORE_GLES:
#if PPSSPP_API(ANY_GL)
	case GPUCORE_SOFTWARE:
		graphicsContext = new WindowsGLContext();
		needRenderThread = true;
		break;
#endif
	case GPUCORE_DIRECTX11:
		graphicsContext = new D3D11Context();
		break;

	case GPUCORE_VULKAN:
		graphicsContext = new WindowsVulkanContext();
		break;
	default:
		_assert_(false);
		break;
	}

	if (graphicsContext->Init(NULL, hWnd, error_message)) {
		*ctx = graphicsContext;
		gfx_ = graphicsContext;
	} else {
		delete graphicsContext;
		*ctx = nullptr;
		gfx_ = nullptr;
		return false;
	}

	if (needRenderThread) {
		std::thread th([&]{
			while (threadState_ == RenderThreadState::IDLE)
				sleep_ms(1, "render-thread-idle-poll");
			threadState_ = RenderThreadState::STARTING;

			std::string err;
			if (!gfx_->InitFromRenderThread(&err)) {
				threadState_ = RenderThreadState::START_FAILED;
				return;
			}
			gfx_->ThreadStart();
			threadState_ = RenderThreadState::STARTED;

			while (threadState_ != RenderThreadState::STOP_REQUESTED) {
				if (!gfx_->ThreadFrame()) {
					break;
				}
			}

			threadState_ = RenderThreadState::STOPPING;
			gfx_->ThreadEnd();
			gfx_->ShutdownFromRenderThread();
			threadState_ = RenderThreadState::STOPPED;
		});
		th.detach();
	}

	if (needRenderThread) {
		threadState_ = RenderThreadState::START_REQUESTED;
		while (threadState_ == RenderThreadState::START_REQUESTED || threadState_ == RenderThreadState::STARTING)
			sleep_ms(1, "render-thread-start-poll");

		return threadState_ == RenderThreadState::STARTED;
	}

	return true;
}

void WindowsHeadlessHost::ShutdownGraphics() {
	gfx_->StopThread();
	while (threadState_ != RenderThreadState::STOPPED && threadState_ != RenderThreadState::IDLE)
		sleep_ms(1, "render-thread-stop-poll");

	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
	DestroyWindow(hWnd);
	hWnd = NULL;
}

void WindowsHeadlessHost::SwapBuffers() {}
