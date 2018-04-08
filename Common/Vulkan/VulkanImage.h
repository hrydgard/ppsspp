#pragma once

#include "Common/Vulkan/VulkanContext.h"

class VulkanDeviceAllocator;

// Wrapper around what you need to use a texture.
// Not very optimal - if you have many small textures you should use other strategies.
class VulkanTexture {
public:
	VulkanTexture(VulkanContext *vulkan, VulkanDeviceAllocator *allocator)
		: vulkan_(vulkan), allocator_(allocator) {
	}
	~VulkanTexture() {
		Destroy();
	}

	// Fast uploads from buffer. Mipmaps supported.
	// Usage must at least include VK_IMAGE_USAGE_TRANSFER_DST_BIT in order to use UploadMip.
	// When using UploadMip, initialLayout should be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
	bool CreateDirect(VkCommandBuffer cmd, int w, int h, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, const VkComponentMapping *mapping = nullptr);
	void UploadMip(VkCommandBuffer cmd, int mip, int mipWidth, int mipHeight, VkBuffer buffer, uint32_t offset, size_t rowLength);  // rowLength is in pixels
	void GenerateMip(VkCommandBuffer cmd, int mip);
	void EndCreate(VkCommandBuffer cmd, bool vertexTexture = false);

	void Destroy();

	void SetTag(const std::string &tag) {
		tag_ = tag;
	}
	std::string Tag() const {
		return tag_;
	}
	void Touch();

	// Used in image copies, etc.
	VkImage GetImage() const { return image_; }

	// Used for sampling, generally.
	VkImageView GetImageView() const { return view_; }

	int32_t GetWidth() const { return width_; }
	int32_t GetHeight() const { return height_; }
	int32_t GetNumMips() const { return numMips_; }
	VkFormat GetFormat() const { return format_; }

private:
	void Wipe();

	VulkanContext *vulkan_;
	VkImage image_ = VK_NULL_HANDLE;
	VkImageView view_ = VK_NULL_HANDLE;
	VkDeviceMemory mem_ = VK_NULL_HANDLE;
	int32_t width_ = 0;
	int32_t height_ = 0;
	int32_t numMips_ = 1;
	VkFormat format_ = VK_FORMAT_UNDEFINED;
	VulkanDeviceAllocator *allocator_;
	size_t offset_ = 0;
	std::string tag_;
};
