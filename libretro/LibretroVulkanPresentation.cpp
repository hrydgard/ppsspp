#include "libretro/LibretroVulkanPresentation.h"

#include "Common/GPU/Vulkan/VulkanContext.h"

using namespace PPSSPP_VK;

LibretroVulkanPresentation::LibretroVulkanPresentation(retro_hw_render_interface_vulkan *vulkan, VkFormat format, VkExtent2D extent)
	: vulkan_(vulkan), format_(format), extent_(extent) {
}

uint32_t LibretroVulkanPresentation::ImageCountFromMask(uint32_t mask) {
	uint32_t count = 0;
	while (mask) {
		count++;
		mask >>= 1;
	}
	return count;
}

bool LibretroVulkanPresentation::IsValidImageIndex(uint32_t imageIndex) const {
	return imageIndex < images_.size() && imageIndex < 32 && (syncIndexMask_ & (1u << imageIndex)) != 0;
}

bool LibretroVulkanPresentation::Create(VulkanContext *context) {
	dedicatedAllocation_ = context->Extensions().KHR_dedicated_allocation;

	syncIndexMask_ = vulkan_->get_sync_index_mask(vulkan_->handle);
	uint32_t count = ImageCountFromMask(syncIndexMask_);
	auto fail = [this, context]() {
		Destroy(context);
		return false;
	};

	VkDevice device = context->GetDevice();
	images_.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		Image &img = images_[i];

		VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = format_;
		info.extent = { extent_.width, extent_.height, 1 };
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(device, &info, nullptr, &img.image) != VK_SUCCESS) {
			return fail();
		}

		VkMemoryRequirements memreq;
		vkGetImageMemoryRequirements(device, img.image, &memreq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memreq.size;

		VkMemoryDedicatedAllocateInfoKHR dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
		if (dedicatedAllocation_) {
			alloc.pNext = &dedicated;
			dedicated.image = img.image;
		}

		if (!context->MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex)) {
			return fail();
		}
		if (vkAllocateMemory(device, &alloc, nullptr, &img.memory) != VK_SUCCESS) {
			return fail();
		}
		if (vkBindImageMemory(device, img.image, img.memory, 0) != VK_SUCCESS) {
			return fail();
		}

		img.retroImage.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		img.retroImage.create_info.image = img.image;
		img.retroImage.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		img.retroImage.create_info.format = format_;
		img.retroImage.create_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		img.retroImage.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		img.retroImage.create_info.subresourceRange.layerCount = 1;
		img.retroImage.create_info.subresourceRange.levelCount = 1;
		if (vkCreateImageView(device, &img.retroImage.create_info, nullptr, &img.retroImage.image_view) != VK_SUCCESS) {
			return fail();
		}
		img.retroImage.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	return true;
}

void LibretroVulkanPresentation::Destroy(VulkanContext *context) {
	VkDevice device = context->GetDevice();
	for (Image &img : images_) {
		if (img.retroImage.image_view) {
			vkDestroyImageView(device, img.retroImage.image_view, nullptr);
		}
		if (img.image) {
			vkDestroyImage(device, img.image, nullptr);
		}
		if (img.memory) {
			vkFreeMemory(device, img.memory, nullptr);
		}
	}
	images_.clear();
	syncIndexMask_ = 0;
	std::lock_guard<std::mutex> lock(mutex_);
	presentPending_ = false;
	condVar_.notify_all();
}

bool LibretroVulkanPresentation::NeedsRecreate() const {
	return vulkan_->get_sync_index_mask(vulkan_->handle) != syncIndexMask_;
}

bool LibretroVulkanPresentation::Recreate(VulkanContext *context) {
	Destroy(context);
	return Create(context);
}

VkResult LibretroVulkanPresentation::AcquireNextImage(VulkanContext *vulkan, VkSemaphore signalSemaphore, uint32_t *imageIndex) {
	// Unlike a real swapchain, RetroArch doesn't signal signalSemaphore for us here - PrepareSubmit()
	// strips any wait on it before the real vkQueueSubmit, matching the old hack's behavior.
	if (vulkan_->get_sync_index_mask(vulkan_->handle) != syncIndexMask_) {
		return VK_ERROR_OUT_OF_DATE_KHR;
	}
	vulkan_->wait_sync_index(vulkan_->handle);
	*imageIndex = vulkan_->get_sync_index(vulkan_->handle);
	if (!IsValidImageIndex(*imageIndex)) {
		return VK_ERROR_OUT_OF_DATE_KHR;
	}
	return VK_SUCCESS;
}

VkResult LibretroVulkanPresentation::QueuePresent(VulkanContext *vulkan, VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) {
	if (vulkan_->get_sync_index_mask(vulkan_->handle) != syncIndexMask_ || !IsValidImageIndex(imageIndex)) {
		return VK_ERROR_OUT_OF_DATE_KHR;
	}
	std::unique_lock<std::mutex> lock(mutex_);
	vulkan_->set_image(vulkan_->handle, &images_[imageIndex].retroImage, 0, nullptr, vulkan_->queue_index);
	return VK_SUCCESS;
}

void LibretroVulkanPresentation::BeginPresent() {
	std::lock_guard<std::mutex> lock(mutex_);
	presentPending_ = true;
}

void LibretroVulkanPresentation::EndPresent() {
	std::lock_guard<std::mutex> lock(mutex_);
	presentPending_ = false;
	condVar_.notify_all();
}

void LibretroVulkanPresentation::LockQueue() {
	vulkan_->lock_queue(vulkan_->handle);
}

void LibretroVulkanPresentation::UnlockQueue() {
	vulkan_->unlock_queue(vulkan_->handle);
}

void LibretroVulkanPresentation::PrepareSubmit(VkSubmitInfo &submitInfo) {
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;
}

void LibretroVulkanPresentation::WaitForPresentation() {
	std::unique_lock<std::mutex> lock(mutex_);
	condVar_.wait(lock, [this] { return !presentPending_; });
}
