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

#include "WindowsHeadlessHost.h"
#include "Compare.h"

#include "Common/FileUtil.h"
#include "Common/CommonWindows.h"

#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#include "Windows/GPU/WindowsGLContext.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/GPU/WindowsVulkanContext.h"

#include "base/logging.h"
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

void WindowsHeadlessHost::SendOrCollectDebugOutput(const std::string &data)
{
	if (PSP_CoreParameter().printfEmuLog)
		SendDebugOutput(data);
	else if (PSP_CoreParameter().collectEmuLog)
		*PSP_CoreParameter().collectEmuLog += data;
	else
		DEBUG_LOG(COMMON, "%s", data.c_str());
}

void WindowsHeadlessHost::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h)
{
	// Only if we're actually comparing.
	if (comparisonScreenshot.empty()) {
		return;
	}

	// We ignore the current framebuffer parameters and just grab the full screen.
	const static u32 FRAME_STRIDE = 512;
	const static u32 FRAME_WIDTH = 480;
	const static u32 FRAME_HEIGHT = 272;

	GPUDebugBuffer buffer;
	gpuDebug->GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_RENDER);
	const std::vector<u32> pixels = TranslateDebugBufferToCompare(&buffer, 512, 272);

	std::string error;
	double errors = CompareScreenshot(pixels, FRAME_STRIDE, FRAME_WIDTH, FRAME_HEIGHT, comparisonScreenshot, error);
	if (errors < 0)
		SendOrCollectDebugOutput(error);

	if (errors > 0)
	{
		char temp[256];
		sprintf_s(temp, "Screenshot error: %f%%\n", errors * 100.0f);
		SendOrCollectDebugOutput(temp);
	}

	if (errors > 0 && !teamCityMode)
	{
		// Lazy, just read in the original header to output the failed screenshot.
		u8 header[14 + 40] = {0};
		FILE *bmp = File::OpenCFile(comparisonScreenshot, "rb");
		if (bmp)
		{
			fread(&header, sizeof(header), 1, bmp);
			fclose(bmp);
		}

		FILE *saved = File::OpenCFile("__testfailure.bmp", "wb");
		if (saved)
		{
			fwrite(&header, sizeof(header), 1, saved);
			fwrite(pixels.data(), sizeof(u32), FRAME_STRIDE * FRAME_HEIGHT, saved);
			fclose(saved);

			SendOrCollectDebugOutput("Actual output written to: __testfailure.bmp\n");
		}
	}
}

void WindowsHeadlessHost::SetComparisonScreenshot(const std::string &filename)
{
	comparisonScreenshot = filename;
}

bool WindowsHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	hWnd = CreateHiddenWindow();

	if (WINDOW_VISIBLE) {
		ShowWindow(hWnd, TRUE);
		SetFocus(hWnd);
	}

	WindowsGraphicsContext *graphicsContext = nullptr;
	switch (gpuCore_) {
	case GPUCORE_NULL:
	case GPUCORE_GLES:
	case GPUCORE_SOFTWARE:
		graphicsContext = new WindowsGLContext();
		break;

	case GPUCORE_DIRECTX9:
		graphicsContext = new D3D9Context();
		break;

	case GPUCORE_DIRECTX11:
		return false;

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

	if (gpuCore_ == GPUCORE_GLES) {
		// TODO: Do we need to do this here?
		CheckGLExtensions();
	}

	LoadNativeAssets();

	return true;
}

void WindowsHeadlessHost::ShutdownGraphics() {
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
