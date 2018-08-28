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

#include <stdio.h>

#include "headless/WindowsHeadlessHost.h"

#include "Common/FileUtil.h"
#include "Common/CommonWindows.h"

#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#include "Windows/GPU/WindowsGLContext.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/GPU/D3D11Context.h"
#include "Windows/GPU/WindowsVulkanContext.h"

#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "file/vfs.h"
#include "file/zip_read.h"

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
		_T("PPSSPPHeadless"),
		NULL,
	};
	RegisterClassEx(&wndClass);

	DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	return CreateWindowEx(0, _T("PPSSPPHeadless"), _T("PPSSPPHeadless"), style, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, NULL, NULL);
}

void WindowsHeadlessHost::LoadNativeAssets()
{
	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));
	VFSRegister("", new DirectoryAssetReader("../Windows/assets/"));
	VFSRegister("", new DirectoryAssetReader("../Windows/"));
}

void WindowsHeadlessHost::SendDebugOutput(const std::string &output)
{
	fwrite(output.data(), sizeof(char), output.length(), stdout);
	OutputDebugStringUTF8(output.c_str());
}

bool WindowsHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	hWnd = CreateHiddenWindow();

	if (WINDOW_VISIBLE) {
		ShowWindow(hWnd, TRUE);
		SetFocus(hWnd);
	}

	WindowsGraphicsContext *graphicsContext = nullptr;
	bool needRenderThread = false;
	switch (gpuCore_) {
	case GPUCORE_NULL:
	case GPUCORE_GLES:
	case GPUCORE_SOFTWARE:
		graphicsContext = new WindowsGLContext();
		needRenderThread = true;
		break;

	case GPUCORE_DIRECTX9:
		graphicsContext = new D3D9Context();
		break;

	case GPUCORE_DIRECTX11:
		graphicsContext = new D3D11Context();
		break;

	case GPUCORE_VULKAN:
		graphicsContext = new WindowsVulkanContext();
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
				sleep_ms(1);
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
				gfx_->SwapBuffers();
			}

			threadState_ = RenderThreadState::STOPPING;
			gfx_->ThreadEnd();
			gfx_->ShutdownFromRenderThread();
			threadState_ = RenderThreadState::STOPPED;
		});
		th.detach();
	}

	LoadNativeAssets();

	if (needRenderThread) {
		threadState_ = RenderThreadState::START_REQUESTED;
		while (threadState_ == RenderThreadState::START_REQUESTED || threadState_ == RenderThreadState::STARTING)
			sleep_ms(1);

		return threadState_ == RenderThreadState::STARTED;
	}

	return true;
}

void WindowsHeadlessHost::ShutdownGraphics() {
	gfx_->StopThread();
	while (threadState_ != RenderThreadState::STOPPED && threadState_ != RenderThreadState::IDLE)
		sleep_ms(1);

	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
	DestroyWindow(hWnd);
	hWnd = NULL;
}

void WindowsHeadlessHost::SwapBuffers() {
	if (gpuCore_ == GPUCORE_DIRECTX9) {
		MSG msg;
		PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	gfx_->SwapBuffers();
}
