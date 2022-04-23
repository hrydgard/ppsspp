#pragma once

#include <string>
#include "VulkanLoader.h"

class VulkanContext;
class VulkanDeviceAllocator;

VK_DEFINE_HANDLE(VmaAllocation);

// Wrapper around what you need to use a texture.
// ALWAYS use an allocator when calling CreateDirect.
class VulkanTexture {
public:
	VulkanTexture(VulkanContext *vulkan)
		: vulkan_(vulkan) {
	}
	~VulkanTexture() {
		Destroy();
	}

	// Fast uploads from buffer. Mipmaps supported.
	// Usage must at least include VK_IMAGE_USAGE_TRANSFER_DST_BIT in order to use UploadMip.
	// When using UploadMip, initialLayout should be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
	bool CreateDirect(VkCommandBuffer cmd, int w, int h, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, const VkComponentMapping *mapping = nullptr);
	void ClearMip(VkCommandBuffer cmd, int mip, uint32_t value);
	void UploadMip(VkCommandBuffer cmd, int mip, int mipWidth, int mipHeight, VkBuffer buffer, uint32_t offset, size_t rowLength);  // rowLength is in pixels

	void GenerateMips(VkCommandBuffer cmd, int firstMipToGenerate, bool fromCompute);
	void EndCreate(VkCommandBuffer cmd, bool vertexTexture, VkPipelineStageFlags prevStage, VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// When loading mips from compute shaders, you need to pass VK_IMAGE_LAYOUT_GENERAL to the above function.
	// In addition, ignore UploadMip and GenerateMip, and instead use GetViewForMip. Make sure to delete the returned views when used.
	VkImageView CreateViewForMip(int mip);

	void Destroy();

	void SetTag(const char *tag) {
		tag_ = tag;
	}
	const std::string &Tag() const {
		return tag_;
	}
	void Touch() {}

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
	VmaAllocation allocation_ = VK_NULL_HANDLE;

	int32_t width_ = 0;
	int32_t height_ = 0;
	int32_t numMips_ = 1;
	VkFormat format_ = VK_FORMAT_UNDEFINED;
	size_t offset_ = 0;
	std::string tag_;
};
