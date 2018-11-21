// Copyright (c) 2016- PPSSPP Project.

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

#include <string>
#include <cassert>
#include <sstream>

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"
#include "base/logging.h"

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

VkBool32 VKAPI_CALL Vulkan_Dbg(VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void *pUserData) {
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

	if (msgCode == 2)  // Useless perf warning ("Vertex attribute at location X not consumed by vertex shader")
		return false;
	if (msgCode == 64)  // Another useless perf warning that will be seen less and less as we optimize -  vkCmdClearAttachments() issued on command buffer object 0x00000195296C6D40 prior to any Draw Cmds. It is recommended you use RenderPass LOAD_OP_CLEAR on Attachments prior to any Draw.
		return false;
	if (msgCode == 5)
		return false;  // Not exactly a false positive, see https://github.com/KhronosGroup/glslang/issues/1418
	if (msgCode == 0 && strstr(pMsg, "vertexPipelineStoresAndAtomics"))  // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/73
		return false;
#ifdef _WIN32
	std::string msg = message.str();
	OutputDebugStringA(msg.c_str());
	if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		if (options->breakOnError && IsDebuggerPresent()) {
			DebugBreak();
		}
		if (options->msgBoxOnError) {
			MessageBoxA(NULL, message.str().c_str(), "Alert", MB_OK);
		}
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		if (options->breakOnWarning && IsDebuggerPresent()) {
			DebugBreak();
		}
	}
#else
	ILOG("%s", message.str().c_str());
#endif

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}
