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

#include <crtdbg.h>
#include <sstream>

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/Data/Text/Parsers.h"
#include "Windows/GPU/WindowsVulkanContext.h"

#ifdef _DEBUG
static const bool g_validate_ = true;
#else
static const bool g_validate_ = false;
#endif

static uint32_t FlagsFromConfig() {
	uint32_t flags = 0;
	flags = g_Config.bVSync ? VULKAN_FLAG_PRESENT_FIFO : VULKAN_FLAG_PRESENT_MAILBOX;
	if (g_validate_) {
		flags |= VULKAN_FLAG_VALIDATE;
	}
	return flags;
}

bool WindowsVulkanContext::Init(HINSTANCE hInst, HWND hWnd, std::string *error_message) {
	*error_message = "N/A";

	if (vulkan_) {
		*error_message = "Already initialized";
		return false;
	}

	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	Version gitVer(PPSSPP_GIT_VERSION);

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		*error_message = "Failed to load Vulkan driver library: ";
		(*error_message) += errorStr;
		return false;
	}

	vulkan_ = new VulkanContext();

	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = FlagsFromConfig();
	if (VK_SUCCESS != vulkan_->CreateInstance(info)) {
		*error_message = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}
	int deviceNum = vulkan_->GetPhysicalDeviceByName(g_Config.sVulkanDevice);
	if (deviceNum < 0) {
		deviceNum = vulkan_->GetBestPhysicalDevice();
		if (!g_Config.sVulkanDevice.empty())
			g_Config.sVulkanDevice = vulkan_->GetPhysicalDeviceProperties(deviceNum).properties.deviceName;
	}

	vulkan_->ChooseDevice(deviceNum);
	if (vulkan_->CreateDevice() != VK_SUCCESS) {
		*error_message = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

	vulkan_->InitSurface(WINDOWSYSTEM_WIN32, (void *)hInst, (void *)hWnd);
	if (!vulkan_->InitSwapchain()) {
		*error_message = vulkan_->InitError();
		Shutdown();
		return false;
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}

	draw_ = Draw::T3DCreateVulkanContext(vulkan_, useMultiThreading);
	SetGPUBackend(GPUBackend::VULKAN, vulkan_->GetPhysicalDeviceProperties(deviceNum).properties.deviceName);
	bool success = draw_->CreatePresets();
	_assert_msg_(success, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	renderManager_ = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	if (!renderManager_->HasBackbuffers()) {
		Shutdown();
		return false;
	}
	return true;
}

void WindowsVulkanContext::Shutdown() {
	if (draw_)
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	delete draw_;
	draw_ = nullptr;

	vulkan_->WaitUntilQueueIdle();
	vulkan_->DestroySwapchain();
	vulkan_->DestroySurface();
	vulkan_->DestroyDevice();
	vulkan_->DestroyInstance();

	delete vulkan_;
	vulkan_ = nullptr;
	renderManager_ = nullptr;

	finalize_glslang();
}

void WindowsVulkanContext::Resize() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	vulkan_->DestroySwapchain();
	vulkan_->UpdateFlags(FlagsFromConfig());
	vulkan_->InitSwapchain();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
}

void WindowsVulkanContext::Poll() {
	// Check for existing swapchain to avoid issues during shutdown.
	if (vulkan_->GetSwapchain() && renderManager_->NeedsSwapchainRecreate()) {
		Resize();
	}
}

void *WindowsVulkanContext::GetAPIContext() {
	return vulkan_;
}
