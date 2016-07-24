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
#include <sstream>

#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"

#include "base/stringutil.h"
#include "thin3d/thin3d.h"
#include "util/text/parsers.h"
#include "Windows/GPU/WindowsVulkanContext.h"

extern const char *PPSSPP_GIT_VERSION;

#ifdef _DEBUG
static const bool g_validate_ = true;
#else
static const bool g_validate_ = false;
#endif

static VulkanContext *g_Vulkan;

struct VulkanLogOptions {
	bool breakOnWarning;
	bool breakOnError;
	bool msgBoxOnError;
};
static VulkanLogOptions g_LogOptions;

const char *ObjTypeToString(VkDebugReportObjectTypeEXT type) {
	switch (type) {
	case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return "Instance";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return "PhysicalDevice";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return "Device";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return "Queue";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return "CommandBuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return "DeviceMemory";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return "Buffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return "BufferView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return "Image";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return "ImageView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return "ShaderModule";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return "Pipeline";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return "PipelineLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return "Sampler";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return "DescriptorSet";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return "DescriptorSetLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return "DescriptorPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return "Fence";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return "Semaphore";
	case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT: return "Event";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return "QueryPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return "Framebuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return "RenderPass";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return "PipelineCache";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return "SurfaceKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return "SwapChainKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return "CommandPool";
		default: return "";
	}
}

static VkBool32 VKAPI_CALL Vulkan_Dbg(VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void *pUserData) {
	const VulkanLogOptions *options = (const VulkanLogOptions *)pUserData;
	std::ostringstream message;

	if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		message << "ERROR: ";
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		message << "WARNING: ";
	} else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
		message << "PERFORMANCE WARNING: ";
	} else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
		message << "INFO: ";
	} else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
		message << "DEBUG: ";
	}
	message << "[" << pLayerPrefix << "] " << ObjTypeToString(objType) << " Code " << msgCode << " : " << pMsg << "\n";

	// layout barrier. TODO: This one I should fix.
	if (msgCode == 7 && startsWith(pMsg, "Cannot submit cmd buffer"))
		return false;
	if (msgCode == 7 && startsWith(pMsg, "Cannot copy from an image"))
		return false;
	if (msgCode == 7 && startsWith(pMsg, "You cannot transition the layout"))
		return false;
	//if (msgCode == 43 && startsWith(pMsg, "At Draw time the active render"))
	//	return false;
	if (msgCode == 44 && startsWith(pMsg, "At Draw time the active render"))
		return false;

#ifdef _WIN32
	OutputDebugStringA(message.str().c_str());
	if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		if (options->breakOnError) {
			DebugBreak();
		}
		if (options->msgBoxOnError) {
			MessageBoxA(NULL, message.str().c_str(), "Alert", MB_OK);
		}
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		if (options->breakOnWarning) {
			DebugBreak();
		}
	}
#else
	std::cout << message;
#endif

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}

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
	g_Vulkan = new VulkanContext("PPSSPP", gitVer.ToInteger(), (g_validate_ ? VULKAN_FLAG_VALIDATE : 0) | VULKAN_FLAG_PRESENT_MAILBOX);
	if (g_Vulkan->CreateDevice(0) != VK_SUCCESS) {
		*error_message = g_Vulkan->InitError();
		return false;
	}
	if (g_validate_) {
		int bits = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		g_Vulkan->InitDebugMsgCallback(&Vulkan_Dbg, bits, &g_LogOptions);
	}
	g_Vulkan->InitSurfaceWin32(hInst, hWnd);
	g_Vulkan->InitObjects(true);

	return true;
}

void WindowsVulkanContext::Shutdown() {
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->DestroyObjects();
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyDebugMsgCallback();
	delete g_Vulkan;
	g_Vulkan = nullptr;

	finalize_glslang();
}

Thin3DContext *WindowsVulkanContext::CreateThin3DContext() {
	return T3DCreateVulkanContext(g_Vulkan);
}

void WindowsVulkanContext::SwapBuffers() {
}

void WindowsVulkanContext::Resize() {
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->DestroyObjects();

	g_Vulkan->ReinitSurfaceWin32();
	g_Vulkan->InitObjects(true);
}

void WindowsVulkanContext::SwapInterval(int interval) {
}

void *WindowsVulkanContext::GetAPIContext() {
	return g_Vulkan;
}
