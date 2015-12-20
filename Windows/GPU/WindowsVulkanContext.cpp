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

#include <assert.h>
#include <crtdbg.h>

#define VK_PROTOTYPES

#include "ext/vulkan/vulkan.h"
#include "ext/native/thin3d/VulkanContext.h"

#include "thin3d/thin3d.h"
#include "Windows/GPU/WindowsVulkanContext.h"

const bool g_validate_ = true;
VulkanContext *g_Vulkan;

bool WindowsVulkanContext::Init(HINSTANCE hInst, HWND hWnd, std::string *error_message) {
	*error_message = "N/A";

	if (g_Vulkan) {
		*error_message = "Already initialized";
		return false;
	}

	g_Vulkan = new VulkanContext("PPSSPP", 0);

	g_Vulkan->InitObjects(hInst, hWnd, true);

	_CrtCheckMemory();

	VkClearValue clearVal[2];
	memset(clearVal, 0, sizeof(clearVal));
	clearVal[0].color.float32[0] = 0.5f;
	g_Vulkan->BeginSurfaceRenderPass(clearVal);
	return true;
}

void WindowsVulkanContext::Shutdown() {
	g_Vulkan->EndSurfaceRenderPass();

	g_Vulkan->DestroyObjects();
	delete g_Vulkan;
	g_Vulkan = nullptr;
}

Thin3DContext *WindowsVulkanContext::CreateThin3DContext() {
	return T3DCreateVulkanContext(g_Vulkan);
}

void WindowsVulkanContext::SwapBuffers() {
	g_Vulkan->EndSurfaceRenderPass();

	VkClearValue clearVal[2];
	memset(clearVal, 0, sizeof(clearVal));
	clearVal[0].color.float32[0] = 0.5f;
	g_Vulkan->BeginSurfaceRenderPass(clearVal);
}

void WindowsVulkanContext::Resize() {
	/*
	g_Vulkan->DestroyObjects();

	g_Vulkan->WaitUntilQueueIdle();

	g_Vulkan->InitObjects(g_Vulkan)
	*/
}

void WindowsVulkanContext::SwapInterval(int interval) {
}
