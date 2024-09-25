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

#include "ppsspp_config.h"
#include <vector>
#include <string>
#include <cstring>

#include "Core/Config.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/File/FileUtil.h"

#if !PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(SWITCH)
#include <dlfcn.h>
#endif

#if PPSSPP_PLATFORM(ANDROID) && PPSSPP_ARCH(ARM64)
#include "File/AndroidStorage.h"

#include <adrenotools/driver.h>
#endif

namespace PPSSPP_VK {
#if !PPSSPP_PLATFORM(IOS_APP_STORE)
PFN_vkCreateInstance vkCreateInstance;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2;
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
PFN_vkBindBufferMemory2 vkBindBufferMemory2;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkBindImageMemory2 vkBindImageMemory2;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetDeviceBufferMemoryRequirements vkGetDeviceBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetDeviceImageMemoryRequirements vkGetDeviceImageMemoryRequirements;
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
#if defined(VK_USE_PLATFORM_METAL_EXT)
PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR;
PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
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

PFN_vkCreateDebugUtilsMessengerEXT   vkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT	 vkDestroyDebugUtilsMessengerEXT;
PFN_vkCmdBeginDebugUtilsLabelEXT	 vkCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT		 vkCmdEndDebugUtilsLabelEXT;
PFN_vkCmdInsertDebugUtilsLabelEXT	 vkCmdInsertDebugUtilsLabelEXT;
PFN_vkSetDebugUtilsObjectNameEXT     vkSetDebugUtilsObjectNameEXT;
PFN_vkSetDebugUtilsObjectTagEXT      vkSetDebugUtilsObjectTagEXT;

// Assorted other extensions.
PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
PFN_vkCreateRenderPass2 vkCreateRenderPass2;

PFN_vkWaitForPresentKHR vkWaitForPresentKHR;
PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE;
PFN_vkGetRefreshCycleDurationGOOGLE vkGetRefreshCycleDurationGOOGLE;
#endif
} // namespace PPSSPP_VK

using namespace PPSSPP_VK;

#if PPSSPP_PLATFORM(IOS_APP_STORE)
// Statically linked MoltenVK
#elif PPSSPP_PLATFORM(SWITCH)
typedef void *VulkanLibraryHandle;
static VulkanLibraryHandle vulkanLibrary;
#define dlsym(x, y) nullptr
#elif PPSSPP_PLATFORM(WINDOWS)
typedef HINSTANCE VulkanLibraryHandle;
static VulkanLibraryHandle vulkanLibrary;
#define dlsym(x, y) GetProcAddress(x, y)
#else
typedef void *VulkanLibraryHandle;
static VulkanLibraryHandle vulkanLibrary;
#endif

bool g_vulkanAvailabilityChecked = false;
bool g_vulkanMayBeAvailable = false;

static PFN_vkVoidFunction LoadInstanceFunc(VkInstance instance, const char *name) {
	PFN_vkVoidFunction funcPtr = vkGetInstanceProcAddr(instance, name);
	if (!funcPtr) {
		INFO_LOG(Log::G3D, "Missing function (instance): %s", name);
	}
	return funcPtr;
}
#define LOAD_INSTANCE_FUNC(instance, x) x = (PFN_ ## x)LoadInstanceFunc(instance, #x);

static PFN_vkVoidFunction LoadInstanceFuncCore(VkInstance instance, const char *name, const char *extName, u32 min_core, u32 vulkanInstanceApiVersion) {
	PFN_vkVoidFunction funcPtr = vkGetInstanceProcAddr(instance, vulkanInstanceApiVersion >= min_core ? name : extName);
	if (vulkanInstanceApiVersion >= min_core && !funcPtr) {
		// Try the ext name.
		funcPtr = vkGetInstanceProcAddr(instance, extName);
	}
	if (!funcPtr) {
		INFO_LOG(Log::G3D, "Missing (instance): %s (%s)", name, extName);
	}
	return funcPtr;
}
#define LOAD_INSTANCE_FUNC_CORE(instance, x, ext_x, min_core) \
    x = (PFN_ ## x)LoadInstanceFuncCore(instance, #x, #ext_x, min_core, vulkanInstanceApiVersion);

static PFN_vkVoidFunction LoadDeviceFunc(VkDevice device, const char *name) {
	PFN_vkVoidFunction funcPtr = vkGetDeviceProcAddr(device, name);
	if (!funcPtr) {
		INFO_LOG(Log::G3D, "Missing function (device): %s", name);
	}
	return funcPtr;
}
#define LOAD_DEVICE_FUNC(device, x) x = (PFN_ ## x)LoadDeviceFunc(device, #x);

static PFN_vkVoidFunction LoadDeviceFuncCore(VkDevice device, const char *name, const char *extName, u32 min_core, u32 vulkanDeviceApiVersion) {
	PFN_vkVoidFunction funcPtr = vkGetDeviceProcAddr(device, vulkanDeviceApiVersion >= min_core ? name : extName);
	if (vulkanDeviceApiVersion >= min_core && !funcPtr) {
		funcPtr = vkGetDeviceProcAddr(device, extName);
	}
	if (!funcPtr) {
		INFO_LOG(Log::G3D, "Missing (device): %s (%s)", name, extName);
	}
	return funcPtr;
}
#define LOAD_DEVICE_FUNC_CORE(device, x, ext_x, min_core) \
    x = (PFN_ ## x)LoadDeviceFuncCore(device, #x, #ext_x, min_core, vulkanDeviceApiVersion);

#define LOAD_GLOBAL_FUNC(x) x = (PFN_ ## x)dlsym(vulkanLibrary, #x); if (!x) {INFO_LOG(Log::G3D,"Missing (global): %s", #x);}
#define LOAD_GLOBAL_FUNC_LOCAL(lib, x) (PFN_ ## x)dlsym(lib, #x);

static const char * const device_name_blacklist[] = {
	"NVIDIA:SHIELD Tablet K1",
	"SDL:Horizon",
	"motorola:moto g54 5G",  // See issue #18681 / #17825
};

#ifndef _WIN32
static const char * const so_names[] = {
#if PPSSPP_PLATFORM(IOS_APP_STORE)
#elif PPSSPP_PLATFORM(IOS)
	"@executable_path/Frameworks/libMoltenVK.dylib",
	"MoltenVK",
#elif PPSSPP_PLATFORM(MAC)
	"@executable_path/../Frameworks/libMoltenVK.dylib",
	"MoltenVK",
#else
	"libvulkan.so",
#if !defined(__ANDROID__)
	"libvulkan.so.1",
#endif
#endif
};
#endif

#if !PPSSPP_PLATFORM(IOS_APP_STORE)
static VulkanLibraryHandle VulkanLoadLibrary(std::string *errorString) {
#if PPSSPP_PLATFORM(SWITCH)
	// Always unavailable, for now.
	return nullptr;
#elif PPSSPP_PLATFORM(UWP)
	return nullptr;
#elif PPSSPP_PLATFORM(MACOS) && PPSSPP_ARCH(AMD64)
	// Disable Vulkan on Mac/x86. Too many configurations that don't work with MoltenVK
	// for whatever reason.
	return nullptr;
#elif PPSSPP_PLATFORM(WINDOWS)
	return LoadLibrary(L"vulkan-1.dll");
#else
	void *lib = nullptr;

#if PPSSPP_PLATFORM(ANDROID) && PPSSPP_ARCH(ARM64)
	if (!g_Config.sCustomDriver.empty() && g_Config.sCustomDriver != "Default") {
		const Path driverPath = g_Config.internalDataDirectory / "drivers" / g_Config.sCustomDriver;

		json::JsonReader meta = json::JsonReader((driverPath / "meta.json").c_str());
		if (meta.ok()) {
			std::string driverLibName = meta.root().get("libraryName")->value.toString();

			Path tempDir = g_Config.internalDataDirectory / "temp";
			Path fileRedirectDir = g_Config.internalDataDirectory / "vk_file_redirect";

			File::CreateDir(tempDir);
			File::CreateDir(fileRedirectDir);

			lib = adrenotools_open_libvulkan(
				RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_FILE_REDIRECT | ADRENOTOOLS_DRIVER_CUSTOM,
				(std::string(tempDir.c_str()) + "/").c_str(), g_nativeLibDir.c_str(),
				(std::string(driverPath.c_str()) + "/").c_str(), driverLibName.c_str(),
				(std::string(fileRedirectDir.c_str()) + "/").c_str(), nullptr);
			if (!lib) {
				ERROR_LOG(Log::G3D, "Failed to load custom driver with AdrenoTools ('%s')", g_Config.sCustomDriver.c_str());
			} else {
				INFO_LOG(Log::G3D, "Vulkan library loaded with AdrenoTools ('%s')", g_Config.sCustomDriver.c_str());
			}
		}
	}
#endif

	if (!lib) {
		for (int i = 0; i < ARRAY_SIZE(so_names); i++) {
			lib = dlopen(so_names[i], RTLD_NOW | RTLD_LOCAL);
			if (lib) {
				INFO_LOG(Log::G3D, "Vulkan library loaded ('%s')", so_names[i]);
				break;
			}
		}
	}
	return lib;
#endif
}
#endif

#if !PPSSPP_PLATFORM(IOS_APP_STORE)
static void VulkanFreeLibrary(VulkanLibraryHandle &h) {
	if (h) {
#if PPSSPP_PLATFORM(SWITCH)
		// Can't load, and can't free.
#elif PPSSPP_PLATFORM(WINDOWS)
		FreeLibrary(h);
#else
		dlclose(h);
#endif
		h = nullptr;
	}
}
#endif

void VulkanSetAvailable(bool available) {
	INFO_LOG(Log::G3D, "Setting Vulkan availability to true");
	g_vulkanAvailabilityChecked = true;
	g_vulkanMayBeAvailable = available;
}

bool VulkanMayBeAvailable() {
#if PPSSPP_PLATFORM(IOS)
	g_vulkanAvailabilityChecked = true;
	// MoltenVK does no longer seem to support iOS <= 12, despite what the docs say.
	g_vulkanMayBeAvailable = System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 13;
	return g_vulkanMayBeAvailable;
#else
	// Unsupported in VR at the moment
	if (IsVREnabled()) {
		return false;
	}

	if (g_vulkanAvailabilityChecked) {
		return g_vulkanMayBeAvailable;
	}

	std::string name = System_GetProperty(SYSPROP_NAME);
	for (const char *blacklisted_name : device_name_blacklist) {
		if (!strcmp(name.c_str(), blacklisted_name)) {
			INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Device blacklisted ('%s')", name.c_str());
			g_vulkanAvailabilityChecked = true;
			g_vulkanMayBeAvailable = false;
			return false;
		}
	}
	INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Device allowed ('%s')", name.c_str());

	std::string errorStr;
	VulkanLibraryHandle lib = VulkanLoadLibrary(&errorStr);
	if (!lib) {
		INFO_LOG(Log::G3D, "Vulkan loader: Library not available: %s", errorStr.c_str());
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
#elif defined(VK_USE_PLATFORM_METAL_EXT)
	const char * const platformSurfaceExtension = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
#else
	const char *platformSurfaceExtension = 0;
#endif

	if (!localEnumerateInstanceExtensionProperties || !localCreateInstance || !localEnumerate || !localDestroyInstance || !localGetPhysicalDeviceProperties) {
		WARN_LOG(Log::G3D, "VulkanMayBeAvailable: Function pointer missing, bailing");
		goto bail;
	}

	INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Enumerating instance extensions");
	res = localEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, nullptr);
	// Maximum paranoia.
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "Enumerating VK extensions failed (%s)", VulkanResultToString(res));
		goto bail;
	}
	if (instanceExtCount == 0) {
		ERROR_LOG(Log::G3D, "No VK instance extensions - won't be able to present.");
		goto bail;
	}
	INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Instance extension count: %d", instanceExtCount);
	instanceExts.resize(instanceExtCount);
	res = localEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, instanceExts.data());
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "Enumerating VK extensions failed (%s)", VulkanResultToString(res));
		goto bail;
	}
	for (const auto &iter : instanceExts) {
		INFO_LOG(Log::G3D, "VulkanMaybeAvailable: Instance extension found: %s (%08x)", iter.extensionName, iter.specVersion);
		if (platformSurfaceExtension && !strcmp(iter.extensionName, platformSurfaceExtension)) {
			INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Found platform surface extension '%s'", platformSurfaceExtension);
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
			ERROR_LOG(Log::G3D, "Platform surface extension not found");
			goto bail;
		}
	} else {
		if (!surfaceExtensionFound) {
			ERROR_LOG(Log::G3D, "Surface extension not found");
			goto bail;
		}
	}

	// This can't happen unless the driver is double-reporting a surface extension.
	if (ci.enabledExtensionCount > 2) {
		ERROR_LOG(Log::G3D, "Unexpected number of enabled instance extensions");
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
	INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Calling vkCreateInstance");
	res = localCreateInstance(&ci, nullptr, &instance);
	if (res != VK_SUCCESS) {
		instance = nullptr;
		ERROR_LOG(Log::G3D, "VulkanMayBeAvailable: Failed to create vulkan instance (%s)", VulkanResultToString(res));
		goto bail;
	}
	INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Vulkan test instance created successfully.");
	res = localEnumerate(instance, &physicalDeviceCount, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "VulkanMayBeAvailable: Failed to count physical devices (%s)", VulkanResultToString(res));
		goto bail;
	}
	if (physicalDeviceCount == 0) {
		ERROR_LOG(Log::G3D, "VulkanMayBeAvailable: No physical Vulkan devices (count = 0).");
		goto bail;
	}
	devices.resize(physicalDeviceCount);
	res = localEnumerate(instance, &physicalDeviceCount, devices.data());
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "VulkanMayBeAvailable: Failed to enumerate physical devices (%s)", VulkanResultToString(res));
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
			INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Eligible device found: '%s'", props.deviceName);
			break;
		default:
			INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Ineligible device found and ignored: '%s'", props.deviceName);
			break;
		}
		// TODO: Should also check queuefamilyproperties for a GRAPHICS queue family? Oh well.
	}

	if (!anyGood) {
		WARN_LOG(Log::G3D, "VulkanMayBeAvailable: Found Vulkan API, but no good Vulkan device!");
		g_vulkanMayBeAvailable = false;
	} else {
		INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Found working Vulkan API!");
		g_vulkanMayBeAvailable = true;
	}

bail:
	g_vulkanAvailabilityChecked = true;
	if (instance) {
		INFO_LOG(Log::G3D, "VulkanMayBeAvailable: Destroying instance");
		localDestroyInstance(instance, nullptr);
	}
	if (lib) {
		VulkanFreeLibrary(lib);
	}
	if (!g_vulkanMayBeAvailable) {
		WARN_LOG(Log::G3D, "Vulkan with working device not detected.");
	}
	return g_vulkanMayBeAvailable;
#endif
}

bool VulkanLoad(std::string *errorStr) {
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	INFO_LOG(Log::G3D, "iOS: Vulkan doesn't need loading");
	return true;
#else

	if (!vulkanLibrary) {
		vulkanLibrary = VulkanLoadLibrary(errorStr);
		if (!vulkanLibrary) {
			return false;
		}
	}

	LOAD_GLOBAL_FUNC(vkCreateInstance);
	LOAD_GLOBAL_FUNC(vkGetInstanceProcAddr);
	LOAD_GLOBAL_FUNC(vkGetDeviceProcAddr);

	LOAD_GLOBAL_FUNC(vkEnumerateInstanceVersion);
	LOAD_GLOBAL_FUNC(vkEnumerateInstanceExtensionProperties);
	LOAD_GLOBAL_FUNC(vkEnumerateInstanceLayerProperties);

	if (vkCreateInstance && vkGetInstanceProcAddr && vkGetDeviceProcAddr && vkEnumerateInstanceExtensionProperties && vkEnumerateInstanceLayerProperties) {
		INFO_LOG(Log::G3D, "VulkanLoad: Base functions loaded.");
		// NOTE: It's ok if vkEnumerateInstanceVersion is missing.
		return true;
	} else {
		*errorStr = "Failed to load Vulkan base functions";
		ERROR_LOG(Log::G3D, "VulkanLoad: %s", errorStr->c_str());
		VulkanFreeLibrary(vulkanLibrary);
		return false;
	}
#endif
}

void VulkanLoadInstanceFunctions(VkInstance instance, const VulkanExtensions &enabledExtensions, uint32_t vulkanInstanceApiVersion) {
#if !PPSSPP_PLATFORM(IOS_APP_STORE)
	INFO_LOG(Log::G3D, "Loading Vulkan instance functions. Instance API version: %08x (%d.%d.%d)", vulkanInstanceApiVersion, VK_API_VERSION_MAJOR(vulkanInstanceApiVersion), VK_API_VERSION_MINOR(vulkanInstanceApiVersion), VK_API_VERSION_PATCH(vulkanInstanceApiVersion));
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
#elif defined(VK_USE_PLATFORM_METAL_EXT)
	LOAD_INSTANCE_FUNC(instance, vkCreateMetalSurfaceEXT);
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
	LOAD_INSTANCE_FUNC(instance, vkCreateXlibSurfaceKHR);
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	LOAD_INSTANCE_FUNC(instance, vkCreateWaylandSurfaceKHR);
#endif
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
	LOAD_INSTANCE_FUNC(instance, vkCreateDisplayPlaneSurfaceKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceDisplayPropertiesKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetDisplayModePropertiesKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetDisplayPlaneSupportedDisplaysKHR);
	LOAD_INSTANCE_FUNC(instance, vkGetDisplayPlaneCapabilitiesKHR);
#endif

	LOAD_INSTANCE_FUNC(instance, vkDestroySurfaceKHR);

	if (enabledExtensions.KHR_get_physical_device_properties2) {
		LOAD_INSTANCE_FUNC_CORE(instance, vkGetPhysicalDeviceProperties2, vkGetPhysicalDeviceProperties2KHR, VK_API_VERSION_1_1);
		LOAD_INSTANCE_FUNC_CORE(instance, vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2KHR, VK_API_VERSION_1_1);
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

	INFO_LOG(Log::G3D, "Vulkan instance functions loaded.");
#endif
}

// On some implementations, loading functions (that have Device as their first parameter) via vkGetDeviceProcAddr may
// increase performance - but then these function pointers will only work on that specific device. Thus, this loader is not very
// good for multi-device - not likely we'll ever try that anyway though.
void VulkanLoadDeviceFunctions(VkDevice device, const VulkanExtensions &enabledExtensions, uint32_t vulkanDeviceApiVersion) {
#if !PPSSPP_PLATFORM(IOS_APP_STORE)
	INFO_LOG(Log::G3D, "Loading Vulkan device functions. Device API version: %08x (%d.%d.%d)", vulkanDeviceApiVersion, VK_API_VERSION_MAJOR(vulkanDeviceApiVersion), VK_API_VERSION_MINOR(vulkanDeviceApiVersion), VK_API_VERSION_PATCH(vulkanDeviceApiVersion));

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
	LOAD_DEVICE_FUNC(device, vkBindBufferMemory2);
	LOAD_DEVICE_FUNC(device, vkBindImageMemory);
	LOAD_DEVICE_FUNC(device, vkBindImageMemory2);
	LOAD_DEVICE_FUNC(device, vkGetBufferMemoryRequirements);
	LOAD_DEVICE_FUNC(device, vkGetBufferMemoryRequirements2);
	LOAD_DEVICE_FUNC(device, vkGetImageMemoryRequirements);
	LOAD_DEVICE_FUNC(device, vkGetImageMemoryRequirements2);
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

	if (enabledExtensions.KHR_present_wait) {
		LOAD_DEVICE_FUNC(device, vkWaitForPresentKHR);
	}
	if (enabledExtensions.GOOGLE_display_timing) {
		LOAD_DEVICE_FUNC(device, vkGetPastPresentationTimingGOOGLE);
		LOAD_DEVICE_FUNC(device, vkGetRefreshCycleDurationGOOGLE);
	}
	if (enabledExtensions.KHR_dedicated_allocation) {
		LOAD_DEVICE_FUNC_CORE(device, vkGetBufferMemoryRequirements2, vkGetBufferMemoryRequirements2KHR, VK_API_VERSION_1_1);
		LOAD_DEVICE_FUNC_CORE(device, vkGetImageMemoryRequirements2, vkGetImageMemoryRequirements2KHR, VK_API_VERSION_1_1);
	}
	if (enabledExtensions.KHR_create_renderpass2) {
		LOAD_DEVICE_FUNC_CORE(device, vkCreateRenderPass2, vkCreateRenderPass2KHR, VK_API_VERSION_1_2);
	}
	if (enabledExtensions.KHR_maintenance4) {
		LOAD_DEVICE_FUNC_CORE(device, vkGetDeviceBufferMemoryRequirements, vkGetDeviceBufferMemoryRequirementsKHR, VK_API_VERSION_1_3);
		LOAD_DEVICE_FUNC_CORE(device, vkGetDeviceImageMemoryRequirements, vkGetDeviceImageMemoryRequirementsKHR, VK_API_VERSION_1_3);
	}
#endif
}

void VulkanFree() {
#if !PPSSPP_PLATFORM(IOS_APP_STORE)
	VulkanFreeLibrary(vulkanLibrary);
#endif
}

const char *VulkanResultToString(VkResult res) {
	static char temp[128]{};
	switch (res) {
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY_KHR";
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR";
	case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
	case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN (-13)";
	case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
	case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
	case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
	case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
	case VK_ERROR_NOT_PERMITTED_KHR: return "VK_ERROR_NOT_PERMITTED_KHR";
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
	case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
	case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
	case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
	case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
	default:
		// This isn't thread safe, but this should be rare, and at worst, we'll get a jumble of two messages.
		snprintf(temp, sizeof(temp), "VK_ERROR_???: 0x%08x", (u32)res);
		return temp;
	}
}
