// Copyright (c) 2015- PPSSPP Project.

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


// Initializing a Vulkan context is quite a complex task!
// That's not really a strange thing though - you really do have control over everything,
// and everything needs to be specified. There are no nebulous defaults.

// We create a swapchain, and two framebuffers that we can point to two of the images 
// we got from the swap chain. These will be used as backbuffers.
//
// We also create a depth buffer. The swap chain will not allocate one for us so we need
// to manage the memory for it ourselves.
// The depth buffer will not really be used unless we do "non-buffered" rendering, which will happen 
// directly to one of the backbuffers.
// 
// Render pass usage
//
// In normal buffered rendering mode, we do not begin the "UI" render pass until after we have rendered
// a frame of PSP graphics. The render pass that we will use then will be the simple "uiPass" that does not
// bother attaching the depth buffer, and discards all input (no need to even bother clearing as we will
// draw over the whole backbuffer anyway).
//
// However, in non-buffered, we will have to use the depth buffer, and we must begin the rendering pass
// before we start rendering PSP graphics, and end it only after we have completed rendering the UI on top.
// We will also use clearing.
//
// So it all turns into a single rendering pass, which might be good for performance on some GPUs, but it
// will complicate things a little.
// 
// In a first iteration, we will not distinguish between these two cases - we will always create a depth buffer
// and use the same render pass configuration (clear to black). However, we can later change this so we switch
// to a non-clearing render pass in buffered mode, which might be a tiny bit faster.

#include <cassert>
#include <crtdbg.h>
#include <sstream>

#include "Core/Config.h"
#include "Core/System.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"

#include "base/stringutil.h"
#include "thin3d/thin3d.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/VulkanRenderManager.h"
#include "util/text/parsers.h"
#include "Windows/GPU/WindowsVulkanContext.h"

#ifdef _DEBUG
static const bool g_validate_ = true;
#else
static const bool g_validate_ = false;
#endif

static VulkanContext *g_Vulkan;

static VulkanLogOptions g_LogOptions;

bool WindowsVulkanContext::Init(HINSTANCE hInst, HWND hWnd, std::string *error_message) {
	*error_message = "N/A";

	if (g_Vulkan) {
		*error_message = "Already initialized";
		return false;
	}

	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	Version gitVer(PPSSPP_GIT_VERSION);
	g_Vulkan = new VulkanContext();
	if (g_Vulkan->InitError().size()) {
		*error_message = g_Vulkan->InitError();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}
	// int vulkanFlags = VULKAN_FLAG_PRESENT_FIFO_RELAXED;
	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = VULKAN_FLAG_PRESENT_MAILBOX;
	if (g_validate_) {
		info.flags |= VULKAN_FLAG_VALIDATE;
	}
	if (VK_SUCCESS != g_Vulkan->CreateInstance(info)) {
		*error_message = g_Vulkan->InitError();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}
	int deviceNum = g_Vulkan->GetPhysicalDeviceByName(g_Config.sVulkanDevice);
	if (deviceNum < 0) {
		deviceNum = g_Vulkan->GetBestPhysicalDevice();
		g_Config.sVulkanDevice = g_Vulkan->GetPhysicalDeviceProperties(deviceNum).deviceName;
	}
	g_Vulkan->ChooseDevice(deviceNum);
	if (g_Vulkan->EnableDeviceExtension(VK_NV_DEDICATED_ALLOCATION_EXTENSION_NAME)) {
		supportsDedicatedAlloc_ = true;
	}
	if (g_Vulkan->CreateDevice() != VK_SUCCESS) {
		*error_message = g_Vulkan->InitError();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}
	if (g_validate_) {
		int bits = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		g_Vulkan->InitDebugMsgCallback(&Vulkan_Dbg, bits, &g_LogOptions);
	}
	g_Vulkan->InitSurface(WINDOWSYSTEM_WIN32, (void *)hInst, (void *)hWnd);
	if (!g_Vulkan->InitObjects()) {
		*error_message = g_Vulkan->InitError();
		Shutdown();
		return false;
	}

	bool splitSubmit = g_Config.bGfxDebugSplitSubmit;

	draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, splitSubmit);
	SetGPUBackend(GPUBackend::VULKAN);
	bool success = draw_->CreatePresets();
	assert(success);  // Doesn't fail, we include the compiler.
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	if (!renderManager->HasBackbuffers()) {
		Shutdown();
		return false;
	}
	return true;
}

void WindowsVulkanContext::Shutdown() {
	if (draw_)
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	delete draw_;
	draw_ = nullptr;

	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->DestroyObjects();
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyDebugMsgCallback();
	g_Vulkan->DestroyInstance();

	delete g_Vulkan;
	g_Vulkan = nullptr;

	finalize_glslang();
}

void WindowsVulkanContext::Resize() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroyObjects();

	g_Vulkan->ReinitSurface();

	g_Vulkan->InitObjects();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}

void *WindowsVulkanContext::GetAPIContext() {
	return g_Vulkan;
}
