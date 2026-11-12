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

#include <cstring>

#include "Common/VR/VRVulkan.h"
#include "Common/VR/VRBase.h"
#include "Common/GPU/Vulkan/VulkanPresentation.h"

#ifdef XR_USE_GRAPHICS_API_VULKAN

#include "Common/Log.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/VR/VRFramebuffer.h"
#include "Common/VR/VRInput.h"
#include "Common/VR/VRRenderer.h"

static std::vector<std::string> SplitExtensionList(const std::string &list) {
	std::vector<std::string> result;
	size_t start = 0;
	while (start < list.size()) {
		size_t end = list.find(' ', start);
		if (end == std::string::npos) {
			end = list.size();
		}
		if (end > start) {
			result.push_back(list.substr(start, end - start));
		}
		start = end + 1;
	}
	return result;
}

// The runtime's extension queries return one space-separated string.
static std::vector<std::string> GetExtensionList(const char *funcName) {
	engine_t *engine = VR_GetEngine();
	PFN_xrGetVulkanInstanceExtensionsKHR func = nullptr;  // The device one has the same signature.
	if (XR_FAILED(xrGetInstanceProcAddr(engine->appState.Instance, funcName, (PFN_xrVoidFunction *)&func)) || !func) {
		return {};
	}
	uint32_t size = 0;
	if (XR_FAILED(func(engine->appState.Instance, engine->appState.SystemId, 0, &size, nullptr)) || size == 0) {
		return {};
	}
	std::string buffer(size, '\0');
	if (XR_FAILED(func(engine->appState.Instance, engine->appState.SystemId, size, &size, &buffer[0]))) {
		return {};
	}
	buffer.resize(strlen(buffer.c_str()));
	return SplitExtensionList(buffer);
}

bool VR_VulkanSupported() {
	return VR_GetEngine()->appState.Instance != XR_NULL_HANDLE && VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_VULKAN);
}

bool VR_VulkanUnsupported() {
	return VR_GetEngine()->appState.Instance != XR_NULL_HANDLE && !VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_VULKAN);
}

std::vector<std::string> VR_GetVulkanInstanceExtensions() {
	std::vector<std::string> list = GetExtensionList("xrGetVulkanInstanceExtensionsKHR");
	for (const std::string &ext : list) {
		INFO_LOG(Log::G3D, "OpenXR wants Vulkan instance extension %s", ext.c_str());
	}
	return list;
}

const std::vector<const char *> &VR_GetVulkanDeviceExtensions() {
	static std::vector<std::string> storage;
	static std::vector<const char *> pointers;
	storage = GetExtensionList("xrGetVulkanDeviceExtensionsKHR");
	pointers.clear();
	for (const std::string &ext : storage) {
		INFO_LOG(Log::G3D, "OpenXR wants Vulkan device extension %s", ext.c_str());
		pointers.push_back(ext.c_str());
	}
	return pointers;
}

int VR_GetVulkanPhysicalDevice(VulkanContext *vulkan) {
	engine_t *engine = VR_GetEngine();
	PFN_xrGetVulkanGraphicsDeviceKHR getDevice = nullptr;
	if (XR_FAILED(xrGetInstanceProcAddr(engine->appState.Instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&getDevice)) || !getDevice) {
		return -1;
	}
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	if (XR_FAILED(getDevice(engine->appState.Instance, engine->appState.SystemId, vulkan->GetInstance(), &physicalDevice))) {
		return -1;
	}
	for (int i = 0; i < vulkan->GetNumPhysicalDevices(); i++) {
		if (vulkan->GetPhysicalDevice(i) == physicalDevice) {
			return i;
		}
	}
	return -1;
}

static bool CreateSession(VulkanContext *vulkan, std::string *error) {
	engine_t *engine = VR_GetEngine();
	if (engine->appState.Session != XR_NULL_HANDLE) {
		return true;
	}

	// Required before xrCreateSession, even when we don't care about the answer.
	PFN_xrGetVulkanGraphicsRequirementsKHR getRequirements = nullptr;
	OXR(xrGetInstanceProcAddr(engine->appState.Instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getRequirements));
	if (!getRequirements) {
		*error = "No xrGetVulkanGraphicsRequirementsKHR";
		return false;
	}
	XrGraphicsRequirementsVulkanKHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
	OXR(getRequirements(engine->appState.Instance, engine->appState.SystemId, &requirements));
	INFO_LOG(Log::G3D, "OpenXR Vulkan API version: min %d.%d, max %d.%d",
		(int)XR_VERSION_MAJOR(requirements.minApiVersionSupported), (int)XR_VERSION_MINOR(requirements.minApiVersionSupported),
		(int)XR_VERSION_MAJOR(requirements.maxApiVersionSupported), (int)XR_VERSION_MINOR(requirements.maxApiVersionSupported));

	XrGraphicsBindingVulkanKHR &binding = engine->appState.VulkanBinding;
	binding = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
	binding.instance = vulkan->GetInstance();
	binding.physicalDevice = vulkan->GetCurrentPhysicalDevice();
	binding.device = vulkan->GetDevice();
	binding.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();
	binding.queueIndex = 0;

	VR_EnterVR(engine);
	if (engine->appState.Session == XR_NULL_HANDLE) {
		*error = "xrCreateSession failed";
		return false;
	}
	IN_VRInit(engine);
	return true;
}

static VkFormat ToUnormFormat(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
	default: return format;
	}
}

// The XR swapchain in place of the main swapchain. Acquire and present signal and wait on the
// frame's semaphores with empty submits (like the offscreen presentation), around the XR calls.
class OpenXRVulkanPresentation : public VulkanPresentation {
public:
	bool Create() {
		engine_ = VR_GetEngine();
		VR_InitRenderer(engine_);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
		fb_ = &engine_->appState.Renderer.FrameBuffer;
		return fb_->ColorSwapChain.Handle != XR_NULL_HANDLE && fb_->TextureSwapChainLength > 0;
	}

	void Destroy(VulkanContext *vulkan) override {
		if (!engine_) {
			return;
		}
		// End a frame that's still open, while its swapchain exists.
		VR_FinishFrame(engine_);
		VR_DestroyRenderer(engine_);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, false);
		engine_ = nullptr;
		fb_ = nullptr;
	}

	VkResult AcquireNextImage(VulkanContext *vulkan, VkSemaphore signalSemaphore, uint32_t *imageIndex) override {
		if (!VR_AcquireVulkanImage(engine_, imageIndex)) {
			// No frame to draw into (the session isn't running). Processed but not presented.
			return VK_NOT_READY;
		}
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &signalSemaphore;
		VR_LockQueue();
		VkResult res = vkQueueSubmit(vulkan->GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
		VR_UnlockQueue();
		return res;
	}

	VkResult QueuePresent(VulkanContext *vulkan, VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) override {
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &waitSemaphore;
		submit.pWaitDstStageMask = &stage;
		VR_LockQueue();
		VkResult res = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
		VR_UnlockQueue();
		// The rendering is submitted, so the runtime can have the image.
		VR_ReleaseVulkanImage(engine_);
		return res;
	}

	// Emu thread, from Present().
	void BeginPresent() override {
		VR_NotifyPresentQueued();
	}
	// Render thread, after the present (or the skipped one).
	void EndPresent() override {
		VR_FinishFrame(engine_);
	}

	void LockQueue() override { VR_LockQueue(); }
	void UnlockQueue() override { VR_UnlockQueue(); }

	uint32_t GetImageCount() const override { return fb_->TextureSwapChainLength; }
	VkImage GetImage(uint32_t index) const override { return (VkImage)ovrFramebuffer_GetVulkanImage(fb_, index); }
	VkExtent2D GetExtent() const override { return VkExtent2D{ (uint32_t)fb_->Width, (uint32_t)fb_->Height }; }
	VkFormat GetFormat() const override { return ToUnormFormat((VkFormat)fb_->SwapchainFormat); }
	uint32_t GetImageLayers() const override { return (uint32_t)fb_->ArraySize; }
	// What OpenXR expects a released color image to be in.
	VkImageLayout GetPresentLayout() const override { return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }

private:
	engine_t *engine_ = nullptr;
	ovrFramebuffer *fb_ = nullptr;
};

std::unique_ptr<VulkanPresentation> VR_CreateVulkanPresentation(VulkanContext *vulkan, std::string *error) {
	if (!CreateSession(vulkan, error)) {
		return nullptr;
	}
	auto presentation = std::make_unique<OpenXRVulkanPresentation>();
	if (!presentation->Create()) {
		presentation->Destroy(vulkan);
		*error = "Failed to create the XR swapchain";
		return nullptr;
	}
	return presentation;
}

void VR_DestroyVulkanSession() {
	engine_t *engine = VR_GetEngine();
	VR_LeaveVR(engine);
	engine->appState.VulkanBinding = {};
}

#else

bool VR_VulkanSupported() {
	return false;
}

bool VR_VulkanUnsupported() {
	return true;
}

std::vector<std::string> VR_GetVulkanInstanceExtensions() {
	return {};
}

const std::vector<const char *> &VR_GetVulkanDeviceExtensions() {
	static const std::vector<const char *> empty;
	return empty;
}

int VR_GetVulkanPhysicalDevice(VulkanContext *vulkan) {
	return -1;
}

std::unique_ptr<VulkanPresentation> VR_CreateVulkanPresentation(VulkanContext *vulkan, std::string *error) {
	*error = "No OpenXR Vulkan support in this build";
	return nullptr;
}

void VR_DestroyVulkanSession() {}

#endif
