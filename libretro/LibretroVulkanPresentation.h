#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>

// Must come before <libretro_vulkan.h>: this is what defines VK_USE_PLATFORM_WIN32_KHR (etc.) before
// the first inclusion of ext/vulkan/vulkan.h - since that header is include-guarded, whichever include
// reaches it first determines whether the platform-specific declarations (e.g.
// PFN_vkCreateWin32SurfaceKHR) exist for the rest of this translation unit.
#include "Common/GPU/Vulkan/VulkanPresentation.h"

#define VK_NO_PROTOTYPES
#include <libretro_vulkan.h>

// Implements VulkanContext's pluggable presentation backend (see VulkanPresentation.h) against
// libretro's retro_hw_render_interface_vulkan: instead of a real VK_KHR_swapchain (which doesn't exist in
// this model - RetroArch owns the real presentation surface itself), PPSSPP renders into its own rotating
// set of VkImages, sized to match RetroArch's sync index mask, and hands each finished frame back via
// set_image() rather than vkQueuePresentKHR.
class LibretroVulkanPresentation : public VulkanPresentation {
public:
	// vulkan must remain valid for the lifetime of this object - it's owned by RetroArch, not us.
	LibretroVulkanPresentation(retro_hw_render_interface_vulkan *vulkan, VkFormat format, VkExtent2D extent);

	bool Create(VulkanContext *context);
	void Destroy(VulkanContext *context) override;
	bool NeedsRecreate() const override;
	bool Recreate(VulkanContext *context) override;

	VkResult AcquireNextImage(VulkanContext *vulkan, VkSemaphore signalSemaphore, uint32_t *imageIndex) override;
	VkResult QueuePresent(VulkanContext *vulkan, VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) override;
	void BeginPresent() override;
	void EndPresent() override;

	uint32_t GetImageCount() const override { return (uint32_t)images_.size(); }
	VkImage GetImage(uint32_t index) const override { return images_[index].image; }
	VkExtent2D GetExtent() const override { return extent_; }
	VkFormat GetFormat() const override { return format_; }

	VkImageLayout GetPresentLayout() const override { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

	void LockQueue() override;
	void UnlockQueue() override;
	// RetroArch's shared VkQueue model doesn't let semaphores cross the frontend/core boundary
	// meaningfully - strip them before the real vkQueueSubmit.
	void PrepareSubmit(VkSubmitInfo &submitInfo) override;

	// Called from LibretroVulkanContext::SwapBuffers(), mirroring the old free-function
	// vk_libretro_wait_for_presentation().
	void WaitForPresentation();

private:
	struct Image {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		retro_vulkan_image retroImage{};
	};

	retro_hw_render_interface_vulkan *vulkan_;
	VkFormat format_;
	VkExtent2D extent_;
	bool dedicatedAllocation_ = false;
	uint32_t syncIndexMask_ = 0;

	std::vector<Image> images_;

	std::mutex mutex_;
	std::condition_variable condVar_;
	bool presentPending_ = false;

	static uint32_t ImageCountFromMask(uint32_t mask);
	bool IsValidImageIndex(uint32_t imageIndex) const;
};
