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

#include "Common/Vulkan/VulkanLoader.h"
#include <vector>
#include "base/logging.h"
#include "base/basictypes.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif

PFN_vkCreateInstance vkCreateInstance;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkCreateDevice vkCreateDevice;
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueueWaitIdle vkQueueWaitIdle;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkMapMemory vkMapMemory;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
PFN_vkQueueBindSparse vkQueueBindSparse;
PFN_vkCreateFence vkCreateFence;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkResetFences vkResetFences;
PFN_vkGetFenceStatus vkGetFenceStatus;
PFN_vkWaitForFences vkWaitForFences;
PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkCreateEvent vkCreateEvent;
PFN_vkDestroyEvent vkDestroyEvent;
PFN_vkGetEventStatus vkGetEventStatus;
PFN_vkSetEvent vkSetEvent;
PFN_vkResetEvent vkResetEvent;
PFN_vkCreateQueryPool vkCreateQueryPool;
PFN_vkDestroyQueryPool vkDestroyQueryPool;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkCreateBufferView vkCreateBufferView;
PFN_vkDestroyBufferView vkDestroyBufferView;
PFN_vkCreateImage vkCreateImage;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyShaderModule vkDestroyShaderModule;
PFN_vkCreatePipelineCache vkCreatePipelineCache;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
PFN_vkMergePipelineCaches vkMergePipelineCaches;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkCreateComputePipelines vkCreateComputePipelines;
PFN_vkDestroyPipeline vkDestroyPipeline;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkCreateSampler vkCreateSampler;
PFN_vkDestroySampler vkDestroySampler;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
PFN_vkResetDescriptorPool vkResetDescriptorPool;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkDestroyRenderPass vkDestroyRenderPass;
PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;

// Used frequently
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdPushConstants vkCmdPushConstants;

// Every frame to a few times per frame
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkCmdSetEvent vkCmdSetEvent;
PFN_vkCmdResetEvent vkCmdResetEvent;
PFN_vkCmdWaitEvents vkCmdWaitEvents;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdBlitImage vkCmdBlitImage;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;

PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
PFN_vkCmdDispatch vkCmdDispatch;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
PFN_vkCmdFillBuffer vkCmdFillBuffer;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
PFN_vkCmdResolveImage vkCmdResolveImage;
PFN_vkCmdBeginQuery vkCmdBeginQuery;
PFN_vkCmdEndQuery vkCmdEndQuery;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
PFN_vkCmdNextSubpass vkCmdNextSubpass;
PFN_vkCmdExecuteCommands vkCmdExecuteCommands;

#ifdef __ANDROID__
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#elif defined(_WIN32)
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#endif

PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

// WSI extension.
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR vkQueuePresentKHR;

// And the DEBUG_REPORT extension. We dynamically load this.
PFN_vkCreateDebugReportCallbackEXT dyn_vkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT dyn_vkDestroyDebugReportCallbackEXT;


#ifdef _WIN32
static HINSTANCE vulkanLibrary;
#define dlsym(x, y) GetProcAddress(x, y)
#else
static void *vulkanLibrary;
#endif

bool g_vulkanAvailabilityChecked = false;
bool g_vulkanMayBeAvailable = false;

#define LOAD_INSTANCE_FUNC(instance, x) x = (PFN_ ## x)vkGetInstanceProcAddr(instance, #x); if (!x) {ILOG("Missing (instance): %s", #x);}
#define LOAD_DEVICE_FUNC(instance, x) x = (PFN_ ## x)vkGetDeviceProcAddr(instance, #x); if (!x) {ILOG("Missing (device): %s", #x);}
#define LOAD_GLOBAL_FUNC(x) x = (PFN_ ## x)dlsym(vulkanLibrary, #x); if (!x) {ILOG("Missing (global): %s", #x);}

#define LOAD_GLOBAL_FUNC_LOCAL(lib, x) (PFN_ ## x)dlsym(lib, #x);

static const char *so_names[] = {
	"libvulkan.so",
	"libvulkan.so.1",
};

void VulkanSetAvailable(bool available) {
	g_vulkanAvailabilityChecked = true;
	g_vulkanMayBeAvailable = available;
}

bool VulkanMayBeAvailable() {
	if (g_vulkanAvailabilityChecked)
		return g_vulkanMayBeAvailable;
#ifndef _WIN32
	void *lib = nullptr;
	for (int i = 0; i < ARRAY_SIZE(so_names); i++) {
		lib = dlopen(so_names[i], RTLD_NOW | RTLD_LOCAL);
		if (lib) {
			break;
		}
	}
#else
	// LoadLibrary etc
	HINSTANCE lib = LoadLibrary(L"vulkan-1.dll");
#endif
	if (!lib) {
		g_vulkanAvailabilityChecked = true;
		g_vulkanMayBeAvailable = false;
		return false;
	}

	// Do a hyper minimal initialization and teardown to figure out if there's any chance
	// that any sort of Vulkan will be usable.
	PFN_vkCreateInstance localCreateInstance = LOAD_GLOBAL_FUNC_LOCAL(lib, vkCreateInstance);
	PFN_vkEnumeratePhysicalDevices localEnumerate = LOAD_GLOBAL_FUNC_LOCAL(lib, vkEnumeratePhysicalDevices);
	PFN_vkDestroyInstance localDestroyInstance = LOAD_GLOBAL_FUNC_LOCAL(lib, vkDestroyInstance);
	PFN_vkGetPhysicalDeviceProperties localGetPhysicalDeviceProperties = LOAD_GLOBAL_FUNC_LOCAL(lib, vkGetPhysicalDeviceProperties);

	// Need to predeclare all this because of the gotos...
	VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	VkApplicationInfo info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	std::vector<VkPhysicalDevice> devices;
	bool anyGood = false;
	const char *instanceExtensions[1]{};
	VkInstance instance = VK_NULL_HANDLE;
	VkResult res;
	uint32_t physicalDeviceCount = 0;

	if (!localCreateInstance || !localEnumerate || !localDestroyInstance || !localGetPhysicalDeviceProperties)
		goto bail;

#ifdef _WIN32
	instanceExtensions[0] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
	ci.enabledExtensionCount = 1;
#elif defined(__ANDROID__)
	instanceExtensions[0] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
	ci.enabledExtensionCount = 1;
#endif

	ci.ppEnabledExtensionNames = instanceExtensions;
	ci.enabledLayerCount = 0;
	info.apiVersion = VK_API_VERSION_1_0;
	info.applicationVersion = 1;
	info.engineVersion = 1;
	info.pApplicationName = "VulkanChecker";
	info.pEngineName = "VulkanChecker";
	ci.pApplicationInfo = &info;
	ci.flags = 0;
	res = localCreateInstance(&ci, nullptr, &instance);
	if (res != VK_SUCCESS) {
		instance = nullptr;
		ELOG("Failed to create vulkan instance.");
		goto bail;
	}
	ILOG("Vulkan test instance created successfully.");
	res = localEnumerate(instance, &physicalDeviceCount, nullptr);
	if (res != VK_SUCCESS) {
		ELOG("Failed to count physical devices.");
		goto bail;
	}
	if (physicalDeviceCount == 0) {
		ELOG("No physical Vulkan devices.");
		goto bail;
	}
	devices.resize(physicalDeviceCount);
	res = localEnumerate(instance, &physicalDeviceCount, devices.data());
	if (res != VK_SUCCESS) {
		ELOG("Failed to enumerate physical devices.");
		goto bail;
	}
	anyGood = false;
	for (auto device : devices) {
		VkPhysicalDeviceProperties props;
		localGetPhysicalDeviceProperties(device, &props);
		switch (props.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			anyGood = true;
			break;
		default:
			break;
		}
		// TODO: Should also check queuefamilyproperties for a GRAPHICS queue family? Oh well.
	}

	if (!anyGood) {
		WLOG("Found Vulkan API, but no good Vulkan device!");
		g_vulkanMayBeAvailable = false;
	} else {
		ILOG("Found working Vulkan API!");
		g_vulkanMayBeAvailable = true;
	}

bail:
	g_vulkanAvailabilityChecked = true;
	if (instance) {
		localDestroyInstance(instance, nullptr);
	}
	if (lib) {
#ifndef _WIN32
		dlclose(lib);
#else
		FreeLibrary(lib);
#endif
	} else {
		ELOG("Vulkan with working device not detected.");
	}
	return g_vulkanMayBeAvailable;
}

bool VulkanLoad() {
	if (!vulkanLibrary) {
#ifndef _WIN32
		for (int i = 0; i < ARRAY_SIZE(so_names); i++) {
			vulkanLibrary = dlopen(so_names[i], RTLD_NOW | RTLD_LOCAL);
			if (vulkanLibrary)
				break;
		}
#else
		// LoadLibrary etc
		vulkanLibrary = LoadLibrary(L"vulkan-1.dll");
#endif
		if (!vulkanLibrary) {
			return false;
		}
	}

	LOAD_GLOBAL_FUNC(vkCreateInstance);
	LOAD_GLOBAL_FUNC(vkGetInstanceProcAddr);
	LOAD_GLOBAL_FUNC(vkGetDeviceProcAddr);

	LOAD_GLOBAL_FUNC(vkEnumerateInstanceExtensionProperties);
	LOAD_GLOBAL_FUNC(vkEnumerateInstanceLayerProperties);

	WLOG("Vulkan base functions loaded.");
	return true;
}

void VulkanLoadInstanceFunctions(VkInstance instance) {
	// OK, let's use the above functions to get the rest.
	LOAD_INSTANCE_FUNC(instance, vkDestroyInstance);
	LOAD_INSTANCE_FUNC(instance, vkEnumeratePhysicalDevices);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceFeatures);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceFormatProperties);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceImageFormatProperties);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceProperties);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceQueueFamilyProperties);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceMemoryProperties);
	LOAD_INSTANCE_FUNC(instance, vkCreateDevice);
	LOAD_INSTANCE_FUNC(instance, vkDestroyDevice);
	LOAD_INSTANCE_FUNC(instance, vkEnumerateDeviceExtensionProperties);
	LOAD_INSTANCE_FUNC(instance, vkEnumerateDeviceLayerProperties);
	LOAD_INSTANCE_FUNC(instance, vkGetDeviceQueue);
	LOAD_INSTANCE_FUNC(instance, vkQueueSubmit);
	LOAD_INSTANCE_FUNC(instance, vkQueueWaitIdle);
	LOAD_INSTANCE_FUNC(instance, vkDeviceWaitIdle);
	LOAD_INSTANCE_FUNC(instance, vkAllocateMemory);
	LOAD_INSTANCE_FUNC(instance, vkFreeMemory);
	LOAD_INSTANCE_FUNC(instance, vkMapMemory);
	LOAD_INSTANCE_FUNC(instance, vkUnmapMemory);
	LOAD_INSTANCE_FUNC(instance, vkFlushMappedMemoryRanges);
	LOAD_INSTANCE_FUNC(instance, vkInvalidateMappedMemoryRanges);
	LOAD_INSTANCE_FUNC(instance, vkGetDeviceMemoryCommitment);
	LOAD_INSTANCE_FUNC(instance, vkBindBufferMemory);
	LOAD_INSTANCE_FUNC(instance, vkBindImageMemory);
	LOAD_INSTANCE_FUNC(instance, vkGetBufferMemoryRequirements);
	LOAD_INSTANCE_FUNC(instance, vkGetImageMemoryRequirements);
	LOAD_INSTANCE_FUNC(instance, vkGetImageSparseMemoryRequirements);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceSparseImageFormatProperties);
	LOAD_INSTANCE_FUNC(instance, vkQueueBindSparse);
	LOAD_INSTANCE_FUNC(instance, vkCreateFence);
	LOAD_INSTANCE_FUNC(instance, vkDestroyFence);
	LOAD_INSTANCE_FUNC(instance, vkResetFences);
	LOAD_INSTANCE_FUNC(instance, vkGetFenceStatus);
	LOAD_INSTANCE_FUNC(instance, vkWaitForFences);
	LOAD_INSTANCE_FUNC(instance, vkCreateSemaphore);
	LOAD_INSTANCE_FUNC(instance, vkDestroySemaphore);
	LOAD_INSTANCE_FUNC(instance, vkCreateEvent);
	LOAD_INSTANCE_FUNC(instance, vkDestroyEvent);
	LOAD_INSTANCE_FUNC(instance, vkGetEventStatus);
	LOAD_INSTANCE_FUNC(instance, vkSetEvent);
	LOAD_INSTANCE_FUNC(instance, vkResetEvent);
	LOAD_INSTANCE_FUNC(instance, vkCreateQueryPool);
	LOAD_INSTANCE_FUNC(instance, vkDestroyQueryPool);
	LOAD_INSTANCE_FUNC(instance, vkGetQueryPoolResults);
	LOAD_INSTANCE_FUNC(instance, vkCreateBuffer);
	LOAD_INSTANCE_FUNC(instance, vkDestroyBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCreateBufferView);
	LOAD_INSTANCE_FUNC(instance, vkDestroyBufferView);
	LOAD_INSTANCE_FUNC(instance, vkCreateImage);
	LOAD_INSTANCE_FUNC(instance, vkDestroyImage);
	LOAD_INSTANCE_FUNC(instance, vkGetImageSubresourceLayout);
	LOAD_INSTANCE_FUNC(instance, vkCreateImageView);
	LOAD_INSTANCE_FUNC(instance, vkDestroyImageView);
	LOAD_INSTANCE_FUNC(instance, vkCreateShaderModule);
	LOAD_INSTANCE_FUNC(instance, vkDestroyShaderModule);
	LOAD_INSTANCE_FUNC(instance, vkCreatePipelineCache);
	LOAD_INSTANCE_FUNC(instance, vkDestroyPipelineCache);
	LOAD_INSTANCE_FUNC(instance, vkGetPipelineCacheData);
	LOAD_INSTANCE_FUNC(instance, vkMergePipelineCaches);
	LOAD_INSTANCE_FUNC(instance, vkCreateGraphicsPipelines);
	LOAD_INSTANCE_FUNC(instance, vkCreateComputePipelines);
	LOAD_INSTANCE_FUNC(instance, vkDestroyPipeline);
	LOAD_INSTANCE_FUNC(instance, vkCreatePipelineLayout);
	LOAD_INSTANCE_FUNC(instance, vkDestroyPipelineLayout);
	LOAD_INSTANCE_FUNC(instance, vkCreateSampler);
	LOAD_INSTANCE_FUNC(instance, vkDestroySampler);
	LOAD_INSTANCE_FUNC(instance, vkCreateDescriptorSetLayout);
	LOAD_INSTANCE_FUNC(instance, vkDestroyDescriptorSetLayout);
	LOAD_INSTANCE_FUNC(instance, vkCreateDescriptorPool);
	LOAD_INSTANCE_FUNC(instance, vkDestroyDescriptorPool);
	LOAD_INSTANCE_FUNC(instance, vkResetDescriptorPool);
	LOAD_INSTANCE_FUNC(instance, vkAllocateDescriptorSets);
	LOAD_INSTANCE_FUNC(instance, vkFreeDescriptorSets);
	LOAD_INSTANCE_FUNC(instance, vkUpdateDescriptorSets);
	LOAD_INSTANCE_FUNC(instance, vkCreateFramebuffer);
	LOAD_INSTANCE_FUNC(instance, vkDestroyFramebuffer);
	LOAD_INSTANCE_FUNC(instance, vkCreateRenderPass);
	LOAD_INSTANCE_FUNC(instance, vkDestroyRenderPass);
	LOAD_INSTANCE_FUNC(instance, vkGetRenderAreaGranularity);
	LOAD_INSTANCE_FUNC(instance, vkCreateCommandPool);
	LOAD_INSTANCE_FUNC(instance, vkDestroyCommandPool);
	LOAD_INSTANCE_FUNC(instance, vkResetCommandPool);
	LOAD_INSTANCE_FUNC(instance, vkAllocateCommandBuffers);
	LOAD_INSTANCE_FUNC(instance, vkFreeCommandBuffers);
	LOAD_INSTANCE_FUNC(instance, vkBeginCommandBuffer);
	LOAD_INSTANCE_FUNC(instance, vkEndCommandBuffer);
	LOAD_INSTANCE_FUNC(instance, vkResetCommandBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdBindPipeline);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetViewport);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetScissor);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetLineWidth);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetDepthBias);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetBlendConstants);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetDepthBounds);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetStencilCompareMask);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetStencilWriteMask);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetStencilReference);
	LOAD_INSTANCE_FUNC(instance, vkCmdBindDescriptorSets);
	LOAD_INSTANCE_FUNC(instance, vkCmdBindIndexBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdBindVertexBuffers);
	LOAD_INSTANCE_FUNC(instance, vkCmdDraw);
	LOAD_INSTANCE_FUNC(instance, vkCmdDrawIndexed);
	LOAD_INSTANCE_FUNC(instance, vkCmdDrawIndirect);
	LOAD_INSTANCE_FUNC(instance, vkCmdDrawIndexedIndirect);
	LOAD_INSTANCE_FUNC(instance, vkCmdDispatch);
	LOAD_INSTANCE_FUNC(instance, vkCmdDispatchIndirect);
	LOAD_INSTANCE_FUNC(instance, vkCmdCopyBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdCopyImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdBlitImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdCopyBufferToImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdCopyImageToBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdUpdateBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdFillBuffer);
	LOAD_INSTANCE_FUNC(instance, vkCmdClearColorImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdClearDepthStencilImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdClearAttachments);
	LOAD_INSTANCE_FUNC(instance, vkCmdResolveImage);
	LOAD_INSTANCE_FUNC(instance, vkCmdSetEvent);
	LOAD_INSTANCE_FUNC(instance, vkCmdResetEvent);
	LOAD_INSTANCE_FUNC(instance, vkCmdWaitEvents);
	LOAD_INSTANCE_FUNC(instance, vkCmdPipelineBarrier);
	LOAD_INSTANCE_FUNC(instance, vkCmdBeginQuery);
	LOAD_INSTANCE_FUNC(instance, vkCmdEndQuery);
	LOAD_INSTANCE_FUNC(instance, vkCmdResetQueryPool);
	LOAD_INSTANCE_FUNC(instance, vkCmdWriteTimestamp);
	LOAD_INSTANCE_FUNC(instance, vkCmdCopyQueryPoolResults);
	LOAD_INSTANCE_FUNC(instance, vkCmdPushConstants);
	LOAD_INSTANCE_FUNC(instance, vkCmdBeginRenderPass);
	LOAD_INSTANCE_FUNC(instance, vkCmdNextSubpass);
	LOAD_INSTANCE_FUNC(instance, vkCmdEndRenderPass);
	LOAD_INSTANCE_FUNC(instance, vkCmdExecuteCommands);

	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceSurfacePresentModesKHR);

	LOAD_INSTANCE_FUNC(instance, vkCreateSwapchainKHR);
	LOAD_INSTANCE_FUNC(instance, vkDestroySwapchainKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetSwapchainImagesKHR);
	LOAD_INSTANCE_FUNC(instance, vkAcquireNextImageKHR);
	LOAD_INSTANCE_FUNC(instance, vkQueuePresentKHR);

#ifdef _WIN32
	LOAD_INSTANCE_FUNC(instance, vkCreateWin32SurfaceKHR);
#elif defined(__ANDROID__)
	LOAD_INSTANCE_FUNC(instance, vkCreateAndroidSurfaceKHR);
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
	LOAD_INSTANCE_FUNC(instance, vkCreateXlibSurfaceKHR);
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	LOAD_INSTANCE_FUNC(instance, vkCreateWaylandSurfaceKHR);
#endif

	LOAD_INSTANCE_FUNC(instance, vkDestroySurfaceKHR);

	dyn_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	dyn_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");

	WLOG("Vulkan instance functions loaded.");
}

// On some implementations, loading functions (that have Device as their first parameter) via vkGetDeviceProcAddr may
// increase performance - but then these function pointers will only work on that specific device. Thus, this loader is not very
// good for multi-device.
void VulkanLoadDeviceFunctions(VkDevice device) {
	WLOG("Vulkan device functions loaded.");
	// TODO: Move more functions VulkanLoadInstanceFunctions to here.
	LOAD_DEVICE_FUNC(device, vkCreateSwapchainKHR);
	LOAD_DEVICE_FUNC(device, vkDestroySwapchainKHR);
	LOAD_DEVICE_FUNC(device, vkGetSwapchainImagesKHR);
	LOAD_DEVICE_FUNC(device, vkAcquireNextImageKHR);
	LOAD_DEVICE_FUNC(device, vkQueuePresentKHR);
}

void VulkanFree() {
	if (vulkanLibrary) {
#ifdef _WIN32
		FreeLibrary(vulkanLibrary);
#else
		dlclose(vulkanLibrary);
#endif
		vulkanLibrary = nullptr;
	}
}
