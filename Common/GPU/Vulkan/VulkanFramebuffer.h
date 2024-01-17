#pragma once

#include "Common/Common.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

class VKRRenderPass;
class VulkanBarrierBatch;

// Pipelines need to be created for the right type of render pass.
// TODO: Rename to RenderPassFlags?
// When you add more flags, don't forget to update rpTypeDebugNames[].
enum class RenderPassType {
	DEFAULT = 0,
	// These eight are organized so that bit 0 is DEPTH and bit 1 is INPUT and bit 2 is MULTIVIEW, so
	// they can be OR-ed together in MergeRPTypes.
	HAS_DEPTH = 1,
	MULTIVIEW = 2,
	MULTISAMPLE = 4,

	// This is the odd one out, and gets special handling in MergeRPTypes.
	// If this flag is set, none of the other flags can be set.
	// For the backbuffer we can always use CLEAR/DONT_CARE, so bandwidth cost for a depth channel is negligible
	// so we don't bother with a non-depth version.
	BACKBUFFER = 8,

	TYPE_COUNT = BACKBUFFER + 1,
};
ENUM_CLASS_BITOPS(RenderPassType);

// Simple independent framebuffer image.
struct VKRImage {
	// These four are "immutable".
	VkImage image;

	VkImageView rtView;  // Used for rendering to, and readbacks of stencil. 2D if single layer, 2D_ARRAY if multiple. Includes both depth and stencil if depth/stencil.

	// This is for texturing all layers at once. If aspect is depth/stencil, does not include stencil.
	VkImageView texAllLayersView;

	// If it's a layered image (for stereo), this is two 2D views of it, to make it compatible with shaders that don't yet support stereo.
	// If there's only one layer, layerViews[0] only is initialized.
	VkImageView texLayerViews[2]{};

	VmaAllocation alloc;
	VkFormat format;
	VkSampleCountFlagBits sampleCount;

	// This one is used by QueueRunner's Perform functions to keep track. CANNOT be used anywhere else due to sync issues.
	VkImageLayout layout;

	int numLayers;

	// For debugging.
	std::string tag;

	void Delete(VulkanContext *vulkan);
};

class VKRFramebuffer {
public:
	VKRFramebuffer(VulkanContext *vk, VulkanBarrierBatch *barriers, VkCommandBuffer initCmd, VKRRenderPass *compatibleRenderPass, int _width, int _height, int _numLayers, int _multiSampleLevel, bool createDepthStencilBuffer, const char *tag);
	~VKRFramebuffer();

	VkFramebuffer Get(VKRRenderPass *compatibleRenderPass, RenderPassType rpType);

	int width = 0;
	int height = 0;
	int numLayers = 0;
	VkSampleCountFlagBits sampleCount;

	VKRImage color{};  // color.image is always there.
	VKRImage depth{};  // depth.image is allowed to be VK_NULL_HANDLE.

	// These are only initialized and used if numSamples > 1.
	VKRImage msaaColor{};
	VKRImage msaaDepth{};

	const char *Tag() const {
		return tag_.c_str();
	}

	void UpdateTag(const char *newTag);

	bool HasDepth() const {
		return depth.image != VK_NULL_HANDLE;
	}

	VkImageView GetRTView() {
		if (sampleCount == VK_SAMPLE_COUNT_1_BIT) {
			return color.rtView;
		} else {
			return msaaColor.rtView;
		}
	}

	VulkanContext *Vulkan() const { return vulkan_; }
private:
	static void CreateImage(VulkanContext *vulkan, VulkanBarrierBatch *barriers, VkCommandBuffer cmd, VKRImage &img, int width, int height, int numLayers, VkSampleCountFlagBits sampleCount, VkFormat format, VkImageLayout initialLayout, bool color, const char *tag);

	VkFramebuffer framebuf[(size_t)RenderPassType::TYPE_COUNT]{};

	VulkanContext *vulkan_;
	std::string tag_;
};

inline bool RenderPassTypeHasDepth(RenderPassType type) {
	return (type & RenderPassType::HAS_DEPTH) || type == RenderPassType::BACKBUFFER;
}

inline bool RenderPassTypeHasMultiView(RenderPassType type) {
	return (type & RenderPassType::MULTIVIEW) != 0;
}

inline bool RenderPassTypeHasMultisample(RenderPassType type) {
	return (type & RenderPassType::MULTISAMPLE) != 0;
}

VkSampleCountFlagBits MultiSampleLevelToFlagBits(int count);

// Must be the same order as Draw::RPAction
enum class VKRRenderPassLoadAction : uint8_t {
	KEEP,  // default. avoid when possible.
	CLEAR,
	DONT_CARE,
};

enum class VKRRenderPassStoreAction : uint8_t {
	STORE,  // default. avoid when possible.
	DONT_CARE,
};

struct RPKey {
	// Only render-pass-compatibility-volatile things can be here.
	VKRRenderPassLoadAction colorLoadAction;
	VKRRenderPassLoadAction depthLoadAction;
	VKRRenderPassLoadAction stencilLoadAction;
	VKRRenderPassStoreAction colorStoreAction;
	VKRRenderPassStoreAction depthStoreAction;
	VKRRenderPassStoreAction stencilStoreAction;
};

class VKRRenderPass {
public:
	VKRRenderPass(const RPKey &key) : key_(key) {}

	VkRenderPass Get(VulkanContext *vulkan, RenderPassType rpType, VkSampleCountFlagBits sampleCount);
	void Destroy(VulkanContext *vulkan) {
		for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
			if (pass[i]) {
				vulkan->Delete().QueueDeleteRenderPass(pass[i]);
			}
		}
	}

private:
	// TODO: Might be better off with a hashmap once the render pass type count grows really large..
	VkRenderPass pass[(size_t)RenderPassType::TYPE_COUNT]{};
	VkSampleCountFlagBits sampleCounts[(size_t)RenderPassType::TYPE_COUNT]{};
	RPKey key_;
};

const char *GetRPTypeName(RenderPassType rpType);
