#pragma once

#include "VulkanLoader.h"

class VulkanContext;
class VulkanDeviceAllocator;

VK_DEFINE_HANDLE(VmaAllocation);

class VulkanBarrierBatch;

struct TextureCopyBatch {
	std::vector<VkBufferImageCopy> copies;
	VkBuffer buffer = VK_NULL_HANDLE;
	void reserve(size_t mips) { copies.reserve(mips); }
	bool empty() const { return copies.empty(); }
};

// Wrapper around what you need to use a texture.
// ALWAYS use an allocator when calling CreateDirect.
class VulkanTexture {
public:
	VulkanTexture(VulkanContext *vulkan, const char *tag);
	~VulkanTexture() {
		Destroy();
	}

	// Fast uploads from buffer. Mipmaps supported.
	// Usage must at least include VK_IMAGE_USAGE_TRANSFER_DST_BIT in order to use UploadMip.
	// When using UploadMip, initialLayout should be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
	bool CreateDirect(int w, int h, int depth, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage, VulkanBarrierBatch *barrierBatch, const VkComponentMapping *mapping = nullptr);
	void ClearMip(VkCommandBuffer cmd, int mip, uint32_t value);

	// Can also be used to copy individual levels of a 3D texture.
	// If possible, will just add to the batch instead of submitting a copy.
	void CopyBufferToMipLevel(VkCommandBuffer cmd, TextureCopyBatch *copyBatch, int mip, int mipWidth, int mipHeight, int depthLayer, VkBuffer buffer, uint32_t offset, size_t rowLength);  // rowLength is in pixels
	void FinishCopyBatch(VkCommandBuffer cmd, TextureCopyBatch *copyBatch);

	void GenerateMips(VkCommandBuffer cmd, int firstMipToGenerate, bool fromCompute);
	void EndCreate(VkCommandBuffer cmd, bool vertexTexture, VkPipelineStageFlags prevStage, VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// For updating levels after creation. Careful with the timelines!
	void PrepareForTransferDst(VkCommandBuffer cmd, int levels);
	void RestoreAfterTransferDst(int levels, VulkanBarrierBatch *barriers);

	// When loading mips from compute shaders, you need to pass VK_IMAGE_LAYOUT_GENERAL to the above function.
	// In addition, ignore UploadMip and GenerateMip, and instead use GetViewForMip. Make sure to delete the returned views when used.
	VkImageView CreateViewForMip(int mip);

	void Destroy();

	const char *Tag() const {
		return tag_;
	}

	// Used in image copies, etc.
	VkImage GetImage() const { return image_; }

	// Used for sampling, generally.
	VkImageView GetImageView() const { return view_; }

	// For use with some shaders, we might want to view it as a single entry array for convenience.
	VkImageView GetImageArrayView() const { return arrayView_; }

	int32_t GetWidth() const { return width_; }
	int32_t GetHeight() const { return height_; }
	int32_t GetNumMips() const { return numMips_; }
	VkFormat GetFormat() const { return format_; }

private:
	void Wipe();

	VulkanContext *vulkan_;
	VkImage image_ = VK_NULL_HANDLE;
	VkImageView view_ = VK_NULL_HANDLE;
	VkImageView arrayView_ = VK_NULL_HANDLE;
	VmaAllocation allocation_ = VK_NULL_HANDLE;

	int16_t width_ = 0;
	int16_t height_ = 0;
	int16_t numMips_ = 1;
	int16_t depth_ = 1;

	VkFormat format_ = VK_FORMAT_UNDEFINED;
	char tag_[64];
};
