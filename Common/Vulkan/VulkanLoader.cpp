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
#include <string>

#include "base/logging.h"
#include "base/basictypes.h"
#include "base/NativeApp.h"

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
PFN_vkGetFenceStatus vkGetFenceStatus;
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

// Used frequently together
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
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdPushConstants vkCmdPushConstants;

// Every frame to a few times per frame
PFN_vkWaitForFences vkWaitForFences;
PFN_vkResetFences vkResetFences;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
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

// Rare or not used
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
PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;

PFN_vkCreateDebugUtilsMessengerEXT   vkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT	 vkDestroyDebugUtilsMessengerEXT;
PFN_vkCmdBeginDebugUtilsLabelEXT	 vkCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT		 vkCmdEndDebugUtilsLabelEXT;
PFN_vkCmdInsertDebugUtilsLabelEXT	 vkCmdInsertDebugUtilsLabelEXT;
PFN_vkSetDebugUtilsObjectNameEXT     vkSetDebugUtilsObjectNameEXT;
PFN_vkSetDebugUtilsObjectTagEXT      vkSetDebugUtilsObjectTagEXT;

PFN_vkGetMemoryHostPointerPropertiesEXT vkGetMemoryHostPointerPropertiesEXT;
PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR;
PFN_vkGetImageMemoryRequirements2KHR vkGetImageMemoryRequirements2KHR;
PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;

#ifdef _WIN32
static HINSTANCE vulkanLibrary;
#define dlsym(x, y) GetProcAddress(x, y)
#else
static void *vulkanLibrary;
#endif
const char *VulkanResultToString(VkResult res);

bool g_vulkanAvailabilityChecked = false;
bool g_vulkanMayBeAvailable = false;

#define LOAD_INSTANCE_FUNC(instance, x) x = (PFN_ ## x)vkGetInstanceProcAddr(instance, #x); if (!x) {ILOG("Missing (instance): %s", #x);}
#define LOAD_DEVICE_FUNC(instance, x) x = (PFN_ ## x)vkGetDeviceProcAddr(instance, #x); if (!x) {ILOG("Missing (device): %s", #x);}
#define LOAD_GLOBAL_FUNC(x) x = (PFN_ ## x)dlsym(vulkanLibrary, #x); if (!x) {ILOG("Missing (global): %s", #x);}

#define LOAD_GLOBAL_FUNC_LOCAL(lib, x) (PFN_ ## x)dlsym(lib, #x);

static const char *device_name_blacklist[] = {
	"NVIDIA:SHIELD Tablet K1",
};

static const char *so_names[] = {
	"libvulkan.so",
#if !defined(__ANDROID__)
	"libvulkan.so.1",
#endif
};

void VulkanSetAvailable(bool available) {
	g_vulkanAvailabilityChecked = true;
	g_vulkanMayBeAvailable = available;
}

bool VulkanMayBeAvailable() {
	if (g_vulkanAvailabilityChecked) {
		return g_vulkanMayBeAvailable;
	}

	std::string name = System_GetProperty(SYSPROP_NAME);
	for (const char *blacklisted_name : device_name_blacklist) {
		if (!strcmp(name.c_str(), blacklisted_name)) {
			ILOG("VulkanMayBeAvailable: Device blacklisted ('%s')", name.c_str());
			g_vulkanAvailabilityChecked = true;
			g_vulkanMayBeAvailable = false;
			return false;
		}
	}
	ILOG("VulkanMayBeAvailable: Device allowed ('%s')", name.c_str());

#ifndef _WIN32
	void *lib = nullptr;
	for (int i = 0; i < ARRAY_SIZE(so_names); i++) {
		lib = dlopen(so_names[i], RTLD_NOW | RTLD_LOCAL);
		if (lib) {
			ILOG("VulkanMayBeAvailable: Library loaded ('%s')", so_names[i]);
			break;
		}
	}
#else
	// LoadLibrary etc
	HINSTANCE lib = LoadLibrary(L"vulkan-1.dll");
#endif
	if (!lib) {
		ILOG("Vulkan loader: Library not available");
		g_vulkanAvailabilityChecked = true;
		g_vulkanMayBeAvailable = false;
		return false;
	}

	// Do a hyper minimal initialization and teardown to figure out if there's any chance
	// that any sort of Vulkan will be usable.
	PFN_vkEnumerateInstanceExtensionProperties localEnumerateInstanceExtensionProperties = LOAD_GLOBAL_FUNC_LOCAL(lib, vkEnumerateInstanceExtensionProperties);
	PFN_vkCreateInstance localCreateInstance = LOAD_GLOBAL_FUNC_LOCAL(lib, vkCreateInstance);
	PFN_vkEnumeratePhysicalDevices localEnumerate = LOAD_GLOBAL_FUNC_LOCAL(lib, vkEnumeratePhysicalDevices);
	PFN_vkDestroyInstance localDestroyInstance = LOAD_GLOBAL_FUNC_LOCAL(lib, vkDestroyInstance);
	PFN_vkGetPhysicalDeviceProperties localGetPhysicalDeviceProperties = LOAD_GLOBAL_FUNC_LOCAL(lib, vkGetPhysicalDeviceProperties);

	// Need to predeclare all this because of the gotos...
	VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	VkApplicationInfo info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	std::vector<VkPhysicalDevice> devices;
	bool anyGood = false;
	const char *instanceExtensions[10]{};
	VkInstance instance = VK_NULL_HANDLE;
	VkResult res = VK_SUCCESS;
	uint32_t physicalDeviceCount = 0;
	uint32_t instanceExtCount = 0;
	bool surfaceExtensionFound = false;
	bool platformSurfaceExtensionFound = false;
	std::vector<VkExtensionProperties> instanceExts;
	ci.enabledExtensionCount = 0;  // Should have been reset by struct initialization anyway, just paranoia.

#ifdef _WIN32
	const char * const platformSurfaceExtension = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined(__ANDROID__)
	const char *platformSurfaceExtension = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#else
	const char *platformSurfaceExtension = 0;
#endif

	if (!localEnumerateInstanceExtensionProperties || !localCreateInstance || !localEnumerate || !localDestroyInstance || !localGetPhysicalDeviceProperties) {
		WLOG("VulkanMayBeAvailable: Function pointer missing, bailing");
		goto bail;
	}

	ILOG("VulkanMayBeAvailable: Enumerating instance extensions");
	res = localEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, nullptr);
	// Maximum paranoia.
	if (res != VK_SUCCESS) {
		ELOG("Enumerating VK extensions failed (%s)", VulkanResultToString(res));
		goto bail;
	}
	if (instanceExtCount == 0) {
		ELOG("No VK instance extensions - won't be able to present.");
		goto bail;
	}
	ILOG("VulkanMayBeAvailable: Instance extension count: %d", instanceExtCount);
	instanceExts.resize(instanceExtCount);
	res = localEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, instanceExts.data());
	if (res != VK_SUCCESS) {
		ELOG("Enumerating VK extensions failed (%s)", VulkanResultToString(res));
		goto bail;
	}
	for (auto iter : instanceExts) {
		ILOG("VulkanMaybeAvailable: Instance extension found: %s (%08x)", iter.extensionName, iter.specVersion);
		if (platformSurfaceExtension && !strcmp(iter.extensionName, platformSurfaceExtension)) {
			ILOG("VulkanMayBeAvailable: Found platform surface extension '%s'", platformSurfaceExtension);
			instanceExtensions[ci.enabledExtensionCount++] = platformSurfaceExtension;
			platformSurfaceExtensionFound = true;
			break;
		} else if (!strcmp(iter.extensionName, VK_KHR_SURFACE_EXTENSION_NAME)) {
			instanceExtensions[ci.enabledExtensionCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
			surfaceExtensionFound = true;
		}
	}
	if (platformSurfaceExtension) {
		if (!platformSurfaceExtensionFound || !surfaceExtensionFound) {
			ELOG("Platform surface extension not found");
			goto bail;
		}
	} else {
		if (!surfaceExtensionFound) {
			ELOG("Surface extension not found");
			goto bail;
		}
	}

	// This can't happen unless the driver is double-reporting a surface extension.
	if (ci.enabledExtensionCount > 2) {
		ELOG("Unexpected number of enabled instance extensions");
		goto bail;
	}

	ci.ppEnabledExtensionNames = instanceExtensions;
	ci.enabledLayerCount = 0;
	info.apiVersion = VK_API_VERSION_1_0;
	info.applicationVersion = 1;
	info.engineVersion = 1;
	info.pApplicationName = "VulkanChecker";
	info.pEngineName = "VulkanCheckerEngine";
	ci.pApplicationInfo = &info;
	ci.flags = 0;
	ILOG("VulkanMayBeAvailable: Calling vkCreateInstance");
	res = localCreateInstance(&ci, nullptr, &instance);
	if (res != VK_SUCCESS) {
		instance = nullptr;
		ELOG("VulkanMayBeAvailable: Failed to create vulkan instance (%s)", VulkanResultToString(res));
		goto bail;
	}
	ILOG("VulkanMayBeAvailable: Vulkan test instance created successfully.");
	res = localEnumerate(instance, &physicalDeviceCount, nullptr);
	if (res != VK_SUCCESS) {
		ELOG("VulkanMayBeAvailable: Failed to count physical devices (%s)", VulkanResultToString(res));
		goto bail;
	}
	if (physicalDeviceCount == 0) {
		ELOG("VulkanMayBeAvailable: No physical Vulkan devices (count = 0).");
		goto bail;
	}
	devices.resize(physicalDeviceCount);
	res = localEnumerate(instance, &physicalDeviceCount, devices.data());
	if (res != VK_SUCCESS) {
		ELOG("VulkanMayBeAvailable: Failed to enumerate physical devices (%s)", VulkanResultToString(res));
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
			ILOG("VulkanMayBeAvailable: Ineligible device found and ignored: '%s'", props.deviceName);
			break;
		}
		// TODO: Should also check queuefamilyproperties for a GRAPHICS queue family? Oh well.
	}

	if (!anyGood) {
		WLOG("VulkanMayBeAvailable: Found Vulkan API, but no good Vulkan device!");
		g_vulkanMayBeAvailable = false;
	} else {
		ILOG("VulkanMayBeAvailable: Found working Vulkan API!");
		g_vulkanMayBeAvailable = true;
	}

bail:
	g_vulkanAvailabilityChecked = true;
	if (instance) {
		ILOG("VulkanMayBeAvailable: Destroying instance");
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
			if (vulkanLibrary) {
				ILOG("VulkanLoad: Found library '%s'", so_names[i]);
				break;
			}
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

	if (vkCreateInstance && vkGetInstanceProcAddr && vkGetDeviceProcAddr && vkEnumerateInstanceExtensionProperties && vkEnumerateInstanceLayerProperties) {
		WLOG("VulkanLoad: Base functions loaded.");
		return true;
	} else {
		ELOG("VulkanLoad: Failed to load Vulkan base functions.");
#ifndef _WIN32
		dlclose(vulkanLibrary);
#else
		FreeLibrary(vulkanLibrary);
#endif
		vulkanLibrary = nullptr;
		return false;
	}
}

void VulkanLoadInstanceFunctions(VkInstance instance, const VulkanDeviceExtensions &enabledExtensions) {
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
	LOAD_INSTANCE_FUNC(instance, vkDeviceWaitIdle);

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

	if (enabledExtensions.KHR_get_physical_device_properties2) {
		LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceProperties2KHR);
		LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceFeatures2KHR);
	}

	if (enabledExtensions.EXT_debug_report) {
		LOAD_INSTANCE_FUNC(instance, vkCreateDebugReportCallbackEXT);
		LOAD_INSTANCE_FUNC(instance, vkDestroyDebugReportCallbackEXT);
	}

	if (enabledExtensions.EXT_debug_utils) {
		LOAD_INSTANCE_FUNC(instance, vkCreateDebugUtilsMessengerEXT);
		LOAD_INSTANCE_FUNC(instance, vkDestroyDebugUtilsMessengerEXT);
		LOAD_INSTANCE_FUNC(instance, vkCmdBeginDebugUtilsLabelEXT);
		LOAD_INSTANCE_FUNC(instance, vkCmdEndDebugUtilsLabelEXT);
		LOAD_INSTANCE_FUNC(instance, vkCmdInsertDebugUtilsLabelEXT);
		LOAD_INSTANCE_FUNC(instance, vkSetDebugUtilsObjectNameEXT);
		LOAD_INSTANCE_FUNC(instance, vkSetDebugUtilsObjectTagEXT);
	}

	WLOG("Vulkan instance functions loaded.");
}

// On some implementations, loading functions (that have Device as their first parameter) via vkGetDeviceProcAddr may
// increase performance - but then these function pointers will only work on that specific device. Thus, this loader is not very
// good for multi-device - not likely we'll ever try that anyway though.
void VulkanLoadDeviceFunctions(VkDevice device, const VulkanDeviceExtensions &enabledExtensions) {
	WLOG("Vulkan device functions loaded.");

	LOAD_DEVICE_FUNC(device, vkQueueSubmit);
	LOAD_DEVICE_FUNC(device, vkQueueWaitIdle);
	LOAD_DEVICE_FUNC(device, vkAllocateMemory);
	LOAD_DEVICE_FUNC(device, vkFreeMemory);
	LOAD_DEVICE_FUNC(device, vkMapMemory);
	LOAD_DEVICE_FUNC(device, vkUnmapMemory);
	LOAD_DEVICE_FUNC(device, vkFlushMappedMemoryRanges);
	LOAD_DEVICE_FUNC(device, vkInvalidateMappedMemoryRanges);
	LOAD_DEVICE_FUNC(device, vkGetDeviceMemoryCommitment);
	LOAD_DEVICE_FUNC(device, vkBindBufferMemory);
	LOAD_DEVICE_FUNC(device, vkBindImageMemory);
	LOAD_DEVICE_FUNC(device, vkGetBufferMemoryRequirements);
	LOAD_DEVICE_FUNC(device, vkGetImageMemoryRequirements);
	LOAD_DEVICE_FUNC(device, vkGetImageSparseMemoryRequirements);
	LOAD_DEVICE_FUNC(device, vkGetPhysicalDeviceSparseImageFormatProperties);
	LOAD_DEVICE_FUNC(device, vkQueueBindSparse);
	LOAD_DEVICE_FUNC(device, vkCreateFence);
	LOAD_DEVICE_FUNC(device, vkDestroyFence);
	LOAD_DEVICE_FUNC(device, vkResetFences);
	LOAD_DEVICE_FUNC(device, vkGetFenceStatus);
	LOAD_DEVICE_FUNC(device, vkWaitForFences);
	LOAD_DEVICE_FUNC(device, vkCreateSemaphore);
	LOAD_DEVICE_FUNC(device, vkDestroySemaphore);
	LOAD_DEVICE_FUNC(device, vkCreateEvent);
	LOAD_DEVICE_FUNC(device, vkDestroyEvent);
	LOAD_DEVICE_FUNC(device, vkGetEventStatus);
	LOAD_DEVICE_FUNC(device, vkSetEvent);
	LOAD_DEVICE_FUNC(device, vkResetEvent);
	LOAD_DEVICE_FUNC(device, vkCreateQueryPool);
	LOAD_DEVICE_FUNC(device, vkDestroyQueryPool);
	LOAD_DEVICE_FUNC(device, vkGetQueryPoolResults);
	LOAD_DEVICE_FUNC(device, vkCreateBuffer);
	LOAD_DEVICE_FUNC(device, vkDestroyBuffer);
	LOAD_DEVICE_FUNC(device, vkCreateBufferView);
	LOAD_DEVICE_FUNC(device, vkDestroyBufferView);
	LOAD_DEVICE_FUNC(device, vkCreateImage);
	LOAD_DEVICE_FUNC(device, vkDestroyImage);
	LOAD_DEVICE_FUNC(device, vkGetImageSubresourceLayout);
	LOAD_DEVICE_FUNC(device, vkCreateImageView);
	LOAD_DEVICE_FUNC(device, vkDestroyImageView);
	LOAD_DEVICE_FUNC(device, vkCreateShaderModule);
	LOAD_DEVICE_FUNC(device, vkDestroyShaderModule);
	LOAD_DEVICE_FUNC(device, vkCreatePipelineCache);
	LOAD_DEVICE_FUNC(device, vkDestroyPipelineCache);
	LOAD_DEVICE_FUNC(device, vkGetPipelineCacheData);
	LOAD_DEVICE_FUNC(device, vkMergePipelineCaches);
	LOAD_DEVICE_FUNC(device, vkCreateGraphicsPipelines);
	LOAD_DEVICE_FUNC(device, vkCreateComputePipelines);
	LOAD_DEVICE_FUNC(device, vkDestroyPipeline);
	LOAD_DEVICE_FUNC(device, vkCreatePipelineLayout);
	LOAD_DEVICE_FUNC(device, vkDestroyPipelineLayout);
	LOAD_DEVICE_FUNC(device, vkCreateSampler);
	LOAD_DEVICE_FUNC(device, vkDestroySampler);
	LOAD_DEVICE_FUNC(device, vkCreateDescriptorSetLayout);
	LOAD_DEVICE_FUNC(device, vkDestroyDescriptorSetLayout);
	LOAD_DEVICE_FUNC(device, vkCreateDescriptorPool);
	LOAD_DEVICE_FUNC(device, vkDestroyDescriptorPool);
	LOAD_DEVICE_FUNC(device, vkResetDescriptorPool);
	LOAD_DEVICE_FUNC(device, vkAllocateDescriptorSets);
	LOAD_DEVICE_FUNC(device, vkFreeDescriptorSets);
	LOAD_DEVICE_FUNC(device, vkUpdateDescriptorSets);
	LOAD_DEVICE_FUNC(device, vkCreateFramebuffer);
	LOAD_DEVICE_FUNC(device, vkDestroyFramebuffer);
	LOAD_DEVICE_FUNC(device, vkCreateRenderPass);
	LOAD_DEVICE_FUNC(device, vkDestroyRenderPass);
	LOAD_DEVICE_FUNC(device, vkGetRenderAreaGranularity);
	LOAD_DEVICE_FUNC(device, vkCreateCommandPool);
	LOAD_DEVICE_FUNC(device, vkDestroyCommandPool);
	LOAD_DEVICE_FUNC(device, vkResetCommandPool);
	LOAD_DEVICE_FUNC(device, vkAllocateCommandBuffers);
	LOAD_DEVICE_FUNC(device, vkFreeCommandBuffers);
	LOAD_DEVICE_FUNC(device, vkBeginCommandBuffer);
	LOAD_DEVICE_FUNC(device, vkEndCommandBuffer);
	LOAD_DEVICE_FUNC(device, vkResetCommandBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdBindPipeline);
	LOAD_DEVICE_FUNC(device, vkCmdSetViewport);
	LOAD_DEVICE_FUNC(device, vkCmdSetScissor);
	LOAD_DEVICE_FUNC(device, vkCmdSetLineWidth);
	LOAD_DEVICE_FUNC(device, vkCmdSetDepthBias);
	LOAD_DEVICE_FUNC(device, vkCmdSetBlendConstants);
	LOAD_DEVICE_FUNC(device, vkCmdSetDepthBounds);
	LOAD_DEVICE_FUNC(device, vkCmdSetStencilCompareMask);
	LOAD_DEVICE_FUNC(device, vkCmdSetStencilWriteMask);
	LOAD_DEVICE_FUNC(device, vkCmdSetStencilReference);
	LOAD_DEVICE_FUNC(device, vkCmdBindDescriptorSets);
	LOAD_DEVICE_FUNC(device, vkCmdBindIndexBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdBindVertexBuffers);
	LOAD_DEVICE_FUNC(device, vkCmdDraw);
	LOAD_DEVICE_FUNC(device, vkCmdDrawIndexed);
	LOAD_DEVICE_FUNC(device, vkCmdDrawIndirect);
	LOAD_DEVICE_FUNC(device, vkCmdDrawIndexedIndirect);
	LOAD_DEVICE_FUNC(device, vkCmdDispatch);
	LOAD_DEVICE_FUNC(device, vkCmdDispatchIndirect);
	LOAD_DEVICE_FUNC(device, vkCmdCopyBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdCopyImage);
	LOAD_DEVICE_FUNC(device, vkCmdBlitImage);
	LOAD_DEVICE_FUNC(device, vkCmdCopyBufferToImage);
	LOAD_DEVICE_FUNC(device, vkCmdCopyImageToBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdUpdateBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdFillBuffer);
	LOAD_DEVICE_FUNC(device, vkCmdClearColorImage);
	LOAD_DEVICE_FUNC(device, vkCmdClearDepthStencilImage);
	LOAD_DEVICE_FUNC(device, vkCmdClearAttachments);
	LOAD_DEVICE_FUNC(device, vkCmdResolveImage);
	LOAD_DEVICE_FUNC(device, vkCmdSetEvent);
	LOAD_DEVICE_FUNC(device, vkCmdResetEvent);
	LOAD_DEVICE_FUNC(device, vkCmdWaitEvents);
	LOAD_DEVICE_FUNC(device, vkCmdPipelineBarrier);
	LOAD_DEVICE_FUNC(device, vkCmdBeginQuery);
	LOAD_DEVICE_FUNC(device, vkCmdEndQuery);
	LOAD_DEVICE_FUNC(device, vkCmdResetQueryPool);
	LOAD_DEVICE_FUNC(device, vkCmdWriteTimestamp);
	LOAD_DEVICE_FUNC(device, vkCmdCopyQueryPoolResults);
	LOAD_DEVICE_FUNC(device, vkCmdPushConstants);
	LOAD_DEVICE_FUNC(device, vkCmdBeginRenderPass);
	LOAD_DEVICE_FUNC(device, vkCmdNextSubpass);
	LOAD_DEVICE_FUNC(device, vkCmdEndRenderPass);
	LOAD_DEVICE_FUNC(device, vkCmdExecuteCommands);

	if (enabledExtensions.EXT_external_memory_host) {
		LOAD_DEVICE_FUNC(device, vkGetMemoryHostPointerPropertiesEXT);
	}
	if (enabledExtensions.KHR_dedicated_allocation) {
		LOAD_DEVICE_FUNC(device, vkGetBufferMemoryRequirements2KHR);
		LOAD_DEVICE_FUNC(device, vkGetImageMemoryRequirements2KHR);
	}
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
