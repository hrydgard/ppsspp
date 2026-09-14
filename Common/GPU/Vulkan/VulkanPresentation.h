#pragma once

#include "Common/GPU/Vulkan/VulkanLoader.h"

class VulkanContext;

// Pluggable replacement for VulkanContext's normal real-swapchain path (InitSwapchain()/GetSwapchain()),
// for host environments that hand PPSSPP a way to render frames without a real VK_KHR_swapchain - for
// example a libretro frontend, which owns the actual presentation surface itself and instead wants each
// finished frame handed back through its own callback.
//
// When VulkanContext::GetPresentation() is null (the default), every consumer of this interface falls back
// to the normal real-swapchain code path unchanged. Setting a VulkanPresentation via
// VulkanContext::SetPresentation() opts into this interface instead.
class VulkanPresentation {
public:
	virtual ~VulkanPresentation() {}

	virtual void Destroy(VulkanContext *vulkan) = 0;

	// Some presentation backends can change their image set while the device remains alive. The
	// render manager checks this at a safe frame boundary before recording new work.
	virtual bool NeedsRecreate() const { return false; }
	// Called after the render manager has stopped submission, waited for the queue, and destroyed
	// its backbuffer views. The backend must rebuild any images exposed through GetImage().
	virtual bool Recreate(VulkanContext *) { return true; }

	// Mirrors vkAcquireNextImageKHR: waits for the next image to become available, and signals
	// signalSemaphore once it's safe to render into it.
	virtual VkResult AcquireNextImage(VulkanContext *vulkan, VkSemaphore signalSemaphore, uint32_t *imageIndex) = 0;
	// Mirrors vkQueuePresentKHR: hands the finished image back, waiting on waitSemaphore first.
	virtual VkResult QueuePresent(VulkanContext *vulkan, VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) = 0;

	// Called around a queued presentation. Hosts that hand an image to another thread can use this
	// to keep the caller from invoking its video callback until QueuePresent has completed.
	virtual void BeginPresent() {}
	virtual void EndPresent() {}

	virtual uint32_t GetImageCount() const = 0;
	virtual VkImage GetImage(uint32_t index) const = 0;
	virtual VkExtent2D GetExtent() const = 0;
	virtual VkFormat GetFormat() const = 0;

	// The layout backbuffer images should be left in after rendering, since there's no real presentation
	// engine to hand them to via a VK_IMAGE_LAYOUT_PRESENT_SRC_KHR transition.
	virtual VkImageLayout GetPresentLayout() const = 0;

	// Some hosts (e.g. libretro) share a single VkQueue between the frontend and the core, and require
	// exclusive access to be taken around every vkQueueSubmit/vkQueueWaitIdle. No-ops by default.
	virtual void LockQueue() {}
	virtual void UnlockQueue() {}

	// Called right before every real vkQueueSubmit, in case the backend needs to adjust it - for example,
	// a host that doesn't let semaphores cross the frontend/core boundary meaningfully may need to strip
	// them out here. No-op by default.
	virtual void PrepareSubmit(VkSubmitInfo &submitInfo) {}
};
