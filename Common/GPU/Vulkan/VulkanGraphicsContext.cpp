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

#include "Common/DbgNew.h"
#include <sstream>

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/FrameTiming.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanPresentation.h"

#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "GPU/Vulkan/VulkanUtil.h"

#ifdef _DEBUG
static const bool g_validate_ = true;
#else
static const bool g_validate_ = false;
#endif

using namespace PPSSPP_VK;

// Stands in for the swapchain in offscreen mode: a few images that frames are rendered into and left
// in. Acquiring and presenting only signal and wait on the frame's semaphores, with empty submits, so
// the rest of the frame synchronization works as with a real swapchain.
class VulkanOffscreenPresentation : public VulkanPresentation {
public:
	VulkanOffscreenPresentation(VkFormat format, VkExtent2D extent) : format_(format), extent_(extent) {}

	bool Create(VulkanContext *vulkan) {
		VkDevice device = vulkan->GetDevice();
		for (Image &img : images_) {
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = format_;
			info.extent = { extent_.width, extent_.height, 1 };
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (vkCreateImage(device, &info, nullptr, &img.image) != VK_SUCCESS)
				return false;

			VkMemoryRequirements memreq;
			vkGetImageMemoryRequirements(device, img.image, &memreq);
			VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			alloc.allocationSize = memreq.size;
			if (!vulkan->MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex))
				return false;
			if (vkAllocateMemory(device, &alloc, nullptr, &img.memory) != VK_SUCCESS)
				return false;
			if (vkBindImageMemory(device, img.image, img.memory, 0) != VK_SUCCESS)
				return false;
		}
		return true;
	}

	void Destroy(VulkanContext *vulkan) override {
		VkDevice device = vulkan->GetDevice();
		for (Image &img : images_) {
			if (img.image)
				vkDestroyImage(device, img.image, nullptr);
			if (img.memory)
				vkFreeMemory(device, img.memory, nullptr);
			img = {};
		}
	}

	VkResult AcquireNextImage(VulkanContext *vulkan, VkSemaphore signalSemaphore, uint32_t *imageIndex) override {
		// The queue runs in order, so an image is free again by the time later work reaches it.
		*imageIndex = next_;
		next_ = (next_ + 1) % IMAGE_COUNT;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &signalSemaphore;
		return vkQueueSubmit(vulkan->GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
	}

	VkResult QueuePresent(VulkanContext *vulkan, VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) override {
		// Nothing to show it on, so just consume the semaphore.
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &waitSemaphore;
		submit.pWaitDstStageMask = &stage;
		return vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
	}

	uint32_t GetImageCount() const override { return IMAGE_COUNT; }
	VkImage GetImage(uint32_t index) const override { return images_[index].image; }
	VkExtent2D GetExtent() const override { return extent_; }
	VkFormat GetFormat() const override { return format_; }
	VkImageLayout GetPresentLayout() const override { return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }

private:
	static constexpr int IMAGE_COUNT = 2;
	struct Image {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	};
	Image images_[IMAGE_COUNT];
	VkFormat format_;
	VkExtent2D extent_;
	uint32_t next_ = 0;
};

bool VulkanGraphicsContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	*errorMessage = "N/A";
	_dbg_assert_(deviceName);

	if (vulkan_) {
		*errorMessage = "Already initialized";
		return false;
	}

	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		*errorMessage = "Failed to load Vulkan driver library: ";
		(*errorMessage) += errorStr;
		return false;
	}

	vulkan_ = new VulkanContext();

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (VK_SUCCESS != vulkan_->CreateInstance(info)) {
		*errorMessage = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}
	int deviceNum = vulkan_->GetPhysicalDeviceByName(*deviceName);
	if (deviceNum < 0) {
		deviceNum = vulkan_->GetBestPhysicalDevice();
		if (!deviceName->empty()) {
			*deviceName = vulkan_->GetPhysicalDeviceProperties(deviceNum).properties.deviceName;
		}
	}

	if (vulkan_->CreateDevice(deviceNum) != VK_SUCCESS) {
		*errorMessage = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}
	// Normally the queue is picked along with the surface, which must be able to present from it.
	if (offscreenWidth_ > 0 && !vulkan_->ChooseGraphicsQueueWithoutSurface()) {
		*errorMessage = "No graphics queue";
		return false;
	}
	return true;
}

bool VulkanGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	if (offscreenWidth_ > 0) {
		auto presentation = std::make_unique<VulkanOffscreenPresentation>(VK_FORMAT_B8G8R8A8_UNORM, VkExtent2D{ (uint32_t)offscreenWidth_, (uint32_t)offscreenHeight_ });
		if (!presentation->Create(vulkan_)) {
			presentation->Destroy(vulkan_);
			*errorMessage = "Failed to create the offscreen images";
			return false;
		}
		vulkan_->SetPresentation(std::move(presentation));
	} else {
		// Don't proceed on failure - without a surface there's no present mode, no swapchain and no queue,
		// so everything below would just fail in more confusing ways further down (it used to assert deep
		// inside the thin3d context constructor). Let the caller fall back to another backend instead.
		VkResult res = vulkan_->InitSurface(winsys, data1, data2);
		if (res != VK_SUCCESS) {
			*errorMessage = vulkan_->InitError();
			if (errorMessage->empty()) {
				*errorMessage = StringFromFormat("Failed to initialize Vulkan surface: %s", VulkanResultToString(res));
			}
			return false;
		}
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}

	draw_ = Draw::T3DCreateVulkanContext(vulkan_, useMultiThreading);

	if (offscreenWidth_ == 0) {
		VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);

#ifdef VK_EXT_full_screen_exclusive
		vulkan_->SetFullScreenExclusiveMode(g_Config.bFullScreen && g_Config.bAllowFullScreenExclusive
			? VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT
			: VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT);
#endif

		if (!vulkan_->InitSwapchain(presentMode)) {
			*errorMessage = vulkan_->InitError();
			return false;
		}
	}

	SetGPUBackend(GPUBackend::VULKAN, vulkan_->GetPhysicalDeviceProperties().properties.deviceName);
	bool success = draw_->CreatePresets();
	_assert_msg_(success, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	renderManager_ = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	if (!renderManager_->HasBackbuffers()) {
		// WTF?
		_dbg_assert_(false);
		return false;
	}
	return true;
}

void VulkanGraphicsContext::ShutdownSurface() {
	if (draw_) {
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	}

	delete draw_;
	draw_ = nullptr;

	vulkan_->WaitUntilQueueIdle();
	if (VulkanPresentation *presentation = vulkan_->GetPresentation()) {
		presentation->Destroy(vulkan_);
		vulkan_->SetPresentation(nullptr);
	} else {
		vulkan_->DestroySwapchain();
		vulkan_->DestroySurface();
	}
}

void VulkanGraphicsContext::ShutdownAPI() {
	vulkan_->DestroyDevice();
	vulkan_->DestroyInstance();

	delete vulkan_;
	vulkan_ = nullptr;
	renderManager_ = nullptr;

	finalize_glslang();
}

void VulkanGraphicsContext::Resize() {
	if (offscreenWidth_ > 0) {
		// The images have a fixed size.
		return;
	}
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);

#ifdef VK_EXT_full_screen_exclusive
	vulkan_->SetFullScreenExclusiveMode(g_Config.bFullScreen && g_Config.bAllowFullScreenExclusive
		? VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT
		: VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT);
#endif

	vulkan_->InitSwapchain(presentMode);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
}

void VulkanGraphicsContext::Poll() {
	// Check for existing swapchain to avoid issues during shutdown.
	if (vulkan_->IsSwapchainInited() && renderManager_->NeedsSwapchainRecreate()) {
		Resize();
	} else if (vulkan_->IsSwapchainInited() && windowRestored_) {
		Resize();
		windowRestored_ = false;
	}
}

void *VulkanGraphicsContext::GetAPIContext() {
	return vulkan_;
}
