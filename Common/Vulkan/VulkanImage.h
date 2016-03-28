#pragma once

#include "Common/Vulkan/VulkanContext.h"

class VulkanDeviceAllocator;

// Wrapper around what you need to use a texture.
// Not very optimal - if you have many small textures you should use other strategies.
class VulkanTexture {
public:
	VulkanTexture(VulkanContext *vulkan, VulkanDeviceAllocator *allocator = nullptr)
		: vulkan_(vulkan), image(VK_NULL_HANDLE), mem(VK_NULL_HANDLE), view(VK_NULL_HANDLE),
		tex_width(0), tex_height(0), numMips_(1), format_(VK_FORMAT_UNDEFINED),
		mappableImage(VK_NULL_HANDLE), mappableMemory(VK_NULL_HANDLE), needStaging(false),
		allocator_(allocator), offset_(0) {
		memset(&mem_reqs, 0, sizeof(mem_reqs));
	}
	~VulkanTexture() {
		Destroy();
	}

	// Simple usage - no cleverness, no mipmaps.
	// Always call Create, Lock, Unlock. Unlock performs the upload if necessary.
	// Can later Lock and Unlock again. This cannot change the format. Create cannot
	// be called a second time without recreating the texture object until Destroy has
	// been called.
	VkResult Create(int w, int h, VkFormat format);
	uint8_t *Lock(int level, int *rowPitch);
	void Unlock();

	// Fast uploads from buffer. Mipmaps supported.
	// Usage must at least include VK_IMAGE_USAGE_TRANSFER_DST_BIT in order to use UploadMip.
	// When using UploadMip, initialLayout should be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
	bool CreateDirect(int w, int h, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, const VkComponentMapping *mapping = nullptr);
	void UploadMip(int mip, int mipWidth, int mipHeight, VkBuffer buffer, uint32_t offset, size_t rowLength);  // rowLength is in pixels
	void EndCreate();
	int GetNumMips() const { return numMips_; }
	void Destroy();

	// Used in image copies, etc.
	VkImage GetImage() const { return image; }

	// Used for sampling, generally.
	VkImageView GetImageView() const { return view; }

	int32_t GetWidth() const { return tex_width; }
	int32_t GetHeight() const { return tex_height; }

private:
	void CreateMappableImage();
	void Wipe();

	VulkanContext *vulkan_;
	VkImage image;
	VkDeviceMemory mem;
	VkImageView view;
	int32_t tex_width, tex_height, numMips_;
	VkFormat format_;
	VkImage mappableImage;
	VkDeviceMemory mappableMemory;
	VkMemoryRequirements mem_reqs;
	VulkanDeviceAllocator *allocator_;
	size_t offset_;
	bool needStaging;
};
