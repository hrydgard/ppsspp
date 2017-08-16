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

#include <cstdio>
#include <vector>
#include <string>
#include <map>
#include <assert.h>

#include "Common/Vulkan/SPIRVDisasm.h"

#include "base/logging.h"
#include "base/display.h"
#include "base/stringutil.h"
#include "image/zim_load.h"
#include "math/lin/matrix4x4.h"
#include "math/dataconv.h"
#include "thin3d/thin3d.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Vulkan/VulkanRenderManager.h"

// We use a simple descriptor set for all rendering: 1 sampler, 1 texture, 1 UBO binding point.
// binding 0 - uniform data
// binding 1 - sampler
//
// Vertex data lives in a separate namespace (location = 0, 1, etc)

#include "Common/Vulkan/VulkanLoader.h"

namespace Draw {

// This can actually be replaced with a cast as the values are in the right order.
static const VkCompareOp compToVK[] = {
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
	VK_COMPARE_OP_ALWAYS
};

// So can this.
static const VkBlendOp blendEqToVk[] = {
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX,
};

static const VkBlendFactor blendFactorToVk[] = {
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
	VK_BLEND_FACTOR_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_SRC1_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
	VK_BLEND_FACTOR_SRC1_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
};

static const VkLogicOp logicOpToVK[] = {
	VK_LOGIC_OP_CLEAR,
	VK_LOGIC_OP_SET,
	VK_LOGIC_OP_COPY,
	VK_LOGIC_OP_COPY_INVERTED,
	VK_LOGIC_OP_NO_OP,
	VK_LOGIC_OP_INVERT,
	VK_LOGIC_OP_AND,
	VK_LOGIC_OP_NAND,
	VK_LOGIC_OP_OR,
	VK_LOGIC_OP_NOR,
	VK_LOGIC_OP_XOR,
	VK_LOGIC_OP_EQUIVALENT,
	VK_LOGIC_OP_AND_REVERSE,
	VK_LOGIC_OP_AND_INVERTED,
	VK_LOGIC_OP_OR_REVERSE,
	VK_LOGIC_OP_OR_INVERTED,
};

static const VkPrimitiveTopology primToVK[] = {
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	// Tesselation shader primitive.
	VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
	// The rest are for geometry shaders only.
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
};


static const VkStencilOp stencilOpToVK[8] = {
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_WRAP,
	VK_STENCIL_OP_DECREMENT_AND_WRAP,
};

static inline void Uint8x4ToFloat4(uint32_t u, float f[4]) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
}

class VKBlendState : public BlendState {
public:
	VkPipelineColorBlendStateCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	std::vector<VkPipelineColorBlendAttachmentState> attachments;
};

class VKDepthStencilState : public DepthStencilState {
public:
	VkPipelineDepthStencilStateCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
};

class VKRasterState : public RasterState {
public:
	VKRasterState(VulkanContext *vulkan, const RasterStateDesc &desc) {
		cullFace = desc.cull;
		frontFace = desc.frontFace;
	}
	Facing frontFace;
	CullMode cullFace;

	void ToVulkan(VkPipelineRasterizationStateCreateInfo *info) const {
		memset(info, 0, sizeof(*info));
		info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		info->frontFace = frontFace == Facing::CCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
		switch (cullFace) {
		case CullMode::BACK: info->cullMode = VK_CULL_MODE_BACK_BIT; break;
		case CullMode::FRONT: info->cullMode = VK_CULL_MODE_FRONT_BIT; break;
		case CullMode::FRONT_AND_BACK: info->cullMode = VK_CULL_MODE_FRONT_AND_BACK; break;
		case CullMode::NONE: info->cullMode = VK_CULL_MODE_NONE; break;
		}
		info->polygonMode = VK_POLYGON_MODE_FILL;
		info->lineWidth = 1.0f;
	}
};

VkShaderStageFlagBits StageToVulkan(ShaderStage stage) {
	switch (stage) {
	case ShaderStage::VERTEX: return VK_SHADER_STAGE_VERTEX_BIT;
	case ShaderStage::GEOMETRY: return VK_SHADER_STAGE_GEOMETRY_BIT;
	case ShaderStage::COMPUTE: return VK_SHADER_STAGE_COMPUTE_BIT;
	case ShaderStage::EVALUATION: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	case ShaderStage::CONTROL: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	default:
	case ShaderStage::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
	}
}

// Not registering this as a resource holder, instead the pipeline is registered. It will
// invoke Compile again to recreate the shader then link them together.
class VKShaderModule : public ShaderModule {
public:
	VKShaderModule(ShaderStage stage) : module_(VK_NULL_HANDLE), ok_(false), stage_(stage) {
		vkstage_ = StageToVulkan(stage);
	}
	bool Compile(VulkanContext *vulkan, ShaderLanguage language, const uint8_t *data, size_t size);
	const std::string &GetSource() const { return source_; }
	~VKShaderModule() {
		if (module_) {
			vkDestroyShaderModule(device_, module_, nullptr);
		}
	}
	VkShaderModule Get() const { return module_; }
	ShaderStage GetStage() const override {
		return stage_;
	}

private:
	VkDevice device_;
	VkShaderModule module_;
	VkShaderStageFlagBits vkstage_;
	bool ok_;
	ShaderStage stage_;
	std::string source_;  // So we can recompile in case of context loss.
};

bool VKShaderModule::Compile(VulkanContext *vulkan, ShaderLanguage language, const uint8_t *data, size_t size) {
	// We'll need this to free it later.
	device_ = vulkan->GetDevice();
	this->source_ = (const char *)data;
	std::vector<uint32_t> spirv;
	if (!GLSLtoSPV(vkstage_, source_.c_str(), spirv)) {
		return false;
	}

	// Just for kicks, sanity check the SPIR-V. The disasm isn't perfect
	// but gives you some idea of what's going on.
#if 0
	std::string disasm;
	if (DisassembleSPIRV(spirv, &disasm)) {
		OutputDebugStringA(disasm.c_str());
	}
#endif

	if (vulkan->CreateShaderModule(spirv, &module_)) {
		ok_ = true;
	} else {
		ok_ = false;
	}
	return ok_;
}


inline VkFormat ConvertVertexDataTypeToVk(DataFormat type) {
	switch (type) {
	case DataFormat::R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
	case DataFormat::R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
	case DataFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case DataFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	default: return VK_FORMAT_UNDEFINED;
	}
}

class VKInputLayout : public InputLayout {
public:
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateInfo visc;
};

class VKPipeline : public Pipeline {
public:
	VKPipeline(VulkanContext *vulkan, size_t size) : vulkan_(vulkan) {
		uboSize_ = (int)size;
		ubo_ = new uint8_t[uboSize_];
	}
	~VKPipeline() {
		vulkan_->Delete().QueueDeletePipeline(vkpipeline);
		delete[] ubo_;
	}

	void SetDynamicUniformData(const void *data, size_t size) {
		memcpy(ubo_, data, size);
	}

	// Returns the binding offset, and the VkBuffer to bind.
	size_t PushUBO(VulkanPushBuffer *buf, VulkanContext *vulkan, VkBuffer *vkbuf) {
		return buf->PushAligned(ubo_, uboSize_, vulkan->GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment, vkbuf);
	}

	int GetUniformLoc(const char *name);
	int GetUBOSize() const {
		return uboSize_;
	}
	bool RequiresBuffer() override {
		return false;
	}

	VkPipeline vkpipeline;
	int stride[4]{};
	int dynamicUniformSize = 0;

private:
	VulkanContext *vulkan_;
	uint8_t *ubo_;
	int uboSize_;
};

class VKTexture;
class VKBuffer;
class VKSamplerState;

struct DescriptorSetKey {
	VKTexture *texture_;
	VKSamplerState *sampler_;
	VkBuffer buffer_;

	bool operator < (const DescriptorSetKey &other) const {
		if (texture_ < other.texture_) return true; else if (texture_ > other.texture_) return false;
		if (sampler_ < other.sampler_) return true; else if (sampler_ > other.sampler_) return false;
		if (buffer_ < other.buffer_) return true; else if (buffer_ > other.buffer_) return false;
		return false;
	}
};

class VKTexture : public Texture {
public:
	VKTexture(VulkanContext *vulkan, const TextureDesc &desc)
		: vulkan_(vulkan), format_(desc.format), mipLevels_(desc.mipLevels) {
		Create(desc);
	}

	~VKTexture() {
		Destroy();
	}

	VkImageView GetImageView() { return vkTex_->GetImageView(); }

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data);

	bool Create(const TextureDesc &desc);

	void Destroy() {
		if (vkTex_) {
			vkTex_->Destroy();
			delete vkTex_;
			vkTex_ = nullptr;
		}
	}

	VulkanContext *vulkan_;
	VulkanTexture *vkTex_;

	int mipLevels_;

	DataFormat format_;
};

// Simple independent framebuffer image. Gets its own allocation, we don't have that many framebuffers so it's fine
// to let them have individual non-pooled allocations. Until it's not fine. We'll see.
struct VKImage {
	VkImage image;
	VkImageView view;
	VkDeviceMemory memory;
	VkImageLayout layout;
};

class VKContext : public DrawContext {
public:
	VKContext(VulkanContext *vulkan);
	virtual ~VKContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::GLSL_VULKAN | (uint32_t)ShaderLanguage::SPIRV_VULKAN;
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) override;

	Texture *CreateTexture(const TextureDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) override;
	// color must be 0, for now.
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;

	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override;

	void BindSamplerStates(int start, int count, SamplerState **state) override;
	void BindTextures(int start, int count, Texture **textures) override;
	void BindPipeline(Pipeline *pipeline) override {
		curPipeline_ = (VKPipeline *)pipeline;
	}

	// TODO: Make VKBuffers proper buffers, and do a proper binding model. This is just silly.
	void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) override {
		for (int i = 0; i < count; i++) {
			curVBuffers_[i + start] = (VKBuffer *)buffers[i];
			curVBufferOffsets_[i + start] = offsets ? offsets[i] : 0;
		}
	}
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override {
		curIBuffer_ = (VKBuffer *)indexBuffer;
		curIBufferOffset_ = offset;
	}

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// TODO: Add more sophisticated draws.
	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;

	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	void BeginFrame() override;
	void EndFrame() override;

	void FlushState() override {
		ApplyDynamicState();
	}
	void WaitRenderCompletion(Framebuffer *fbo) override;

	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case APINAME: return "Vulkan";
		case VENDORSTRING: return vulkan_->GetPhysicalDeviceProperties().deviceName;
		case VENDOR: return StringFromFormat("%08x", vulkan_->GetPhysicalDeviceProperties().vendorID);
		case DRIVER: return StringFromFormat("%08x", vulkan_->GetPhysicalDeviceProperties().driverVersion);
		case SHADELANGVERSION: return "N/A";;
		case APIVERSION: 
		{
			uint32_t ver = vulkan_->GetPhysicalDeviceProperties().apiVersion;
			return StringFromFormat("%d.%d.%d", ver >> 22, (ver >> 12) & 0x3ff, ver & 0xfff);
		}
		default: return "?";
		}
	}

	VkDescriptorSet GetOrCreateDescriptorSet(VkBuffer buffer);

	std::vector<std::string> GetFeatureList() const override;
	std::vector<std::string> GetExtensionList() const override;

	uintptr_t GetNativeObject(NativeObject obj) const override {
		switch (obj) {
		case NativeObject::COMPATIBLE_RENDERPASS:
			// Return a representative renderpass.
			if (curRenderPass_ == vulkan_->GetSurfaceRenderPass())
				return (uintptr_t)curRenderPass_;
			else
				return (uintptr_t)renderPasses_[0];
		case NativeObject::CURRENT_RENDERPASS:
			return (uintptr_t)curRenderPass_;
		case NativeObject::RENDERPASS_COMMANDBUFFER:
			return (uintptr_t)cmd_;
		case NativeObject::BOUND_TEXTURE_IMAGEVIEW:
			return (uintptr_t)boundImageView_[0];

		case NativeObject::RENDER_MANAGER:
			return renderManager_;
		default:
			return 0;
		}
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

private:
	void ApplyDynamicState();
	void DirtyDynamicState();

	void EndCurrentRenderpass();
	VkCommandBuffer AllocCmdBuf();

	static void SetupTransitionToTransferSrc(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);
	static void SetupTransitionToTransferDst(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);

	VulkanContext *vulkan_ = nullptr;

	VulkanRenderManager renderManager_;

	VKPipeline *curPipeline_ = nullptr;
	VKBuffer *curVBuffers_[4]{};
	int curVBufferOffsets_[4]{};
	VKBuffer *curIBuffer_ = nullptr;
	int curIBufferOffset_ = 0;

	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;
	VkPipelineCache pipelineCache_;

	inline int RPIndex(RPAction color, RPAction depth) {
		return (int)depth * 3 + (int)color;
	}

	// Renderpasses, all combination of preserving or clearing or dont-care-ing fb contents.
	VkRenderPass renderPasses_[9];

	VkDevice device_;
	VkQueue queue_;
	int queueFamilyIndex_;

	// State to apply at the next draw call if viewportDirty or scissorDirty are true.
	bool viewportDirty_;
	VkViewport viewport_{};
	bool scissorDirty_;
	VkRect2D scissor_;

	int curWidth_ = -1;
	int curHeight_ = -1;

	enum {
		MAX_BOUND_TEXTURES = 1,
		MAX_FRAME_COMMAND_BUFFERS = 256,
	};
	VKTexture *boundTextures_[MAX_BOUND_TEXTURES];
	VKSamplerState *boundSamplers_[MAX_BOUND_TEXTURES];
	VkImageView boundImageView_[MAX_BOUND_TEXTURES];

	struct FrameData {
		VulkanPushBuffer *pushBuffer;
		VkCommandPool cmdPool_;
		VkCommandBuffer cmdBufs[MAX_FRAME_COMMAND_BUFFERS];
		int startCmdBufs_;
		int numCmdBufs;

		// Per-frame descriptor set cache. As it's per frame and reset every frame, we don't need to
		// worry about invalidating descriptors pointing to deleted textures.
		// However! ARM is not a fan of doing it this way.
		std::map<DescriptorSetKey, VkDescriptorSet> descSets_;
		VkDescriptorPool descriptorPool;
	};

	FrameData frame_[VulkanContext::MAX_INFLIGHT_FRAMES]{};

	int frameNum_ = 0;
	VulkanPushBuffer *push_ = nullptr;

	DeviceCaps caps_{};

	VkFramebuffer curFramebuffer_ = VK_NULL_HANDLE;
	VkRenderPass curRenderPass_ = VK_NULL_HANDLE;
	VkCommandBuffer cmd_ = VK_NULL_HANDLE;
};

static int GetBpp(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
		return 32;
	case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
	case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
	case VK_FORMAT_R5G6B5_UNORM_PACK16:
	case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
	case VK_FORMAT_B5G6R5_UNORM_PACK16:
	case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		return 16;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return 32;
	case VK_FORMAT_D16_UNORM:
		return 16;
	default:
		return 0;
	}
}

VkFormat DataFormatToVulkan(DataFormat format) {
	switch (format) {
	case DataFormat::D16: return VK_FORMAT_D16_UNORM;
	case DataFormat::D32F: return VK_FORMAT_D32_SFLOAT;
	case DataFormat::D32F_S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
	case DataFormat::S8: return VK_FORMAT_S8_UINT;
	case DataFormat::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
	case DataFormat::R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
	case DataFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case DataFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
	case DataFormat::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
	case DataFormat::R8G8B8_UNORM: return VK_FORMAT_R8G8B8_UNORM;
	case DataFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case DataFormat::R4G4_UNORM_PACK8: return VK_FORMAT_R4G4_UNORM_PACK8;
	case DataFormat::R4G4B4A4_UNORM_PACK16: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
	case DataFormat::B4G4R4A4_UNORM_PACK16: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
	case DataFormat::R5G5B5A1_UNORM_PACK16: return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
	case DataFormat::B5G5R5A1_UNORM_PACK16: return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
	case DataFormat::R5G6B5_UNORM_PACK16: return VK_FORMAT_R5G6B5_UNORM_PACK16;
	case DataFormat::B5G6R5_UNORM_PACK16: return VK_FORMAT_B5G6R5_UNORM_PACK16;
	case DataFormat::A1R5G5B5_UNORM_PACK16: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
	case DataFormat::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
	case DataFormat::R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
	case DataFormat::R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
	case DataFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;

	case DataFormat::BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case DataFormat::BC2_UNORM_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
	case DataFormat::BC3_UNORM_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
	case DataFormat::BC4_UNORM_BLOCK: return VK_FORMAT_BC4_UNORM_BLOCK;
	case DataFormat::BC4_SNORM_BLOCK: return VK_FORMAT_BC4_SNORM_BLOCK;
	case DataFormat::BC5_UNORM_BLOCK: return VK_FORMAT_BC5_UNORM_BLOCK;
	case DataFormat::BC5_SNORM_BLOCK: return VK_FORMAT_BC5_SNORM_BLOCK;
	case DataFormat::BC6H_SFLOAT_BLOCK: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
	case DataFormat::BC6H_UFLOAT_BLOCK: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case DataFormat::BC7_UNORM_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
	case DataFormat::BC7_SRGB_BLOCK:  return VK_FORMAT_BC7_SRGB_BLOCK;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

inline VkSamplerAddressMode AddressModeToVulkan(Draw::TextureAddressMode mode) {
	switch (mode) {
	case TextureAddressMode::CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case TextureAddressMode::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case TextureAddressMode::REPEAT_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	default:
	case TextureAddressMode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

class VKSamplerState : public SamplerState {
public:
	VKSamplerState(VulkanContext *vulkan, const SamplerStateDesc &desc) : vulkan_(vulkan) {
		VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		s.addressModeU = AddressModeToVulkan(desc.wrapU);
		s.addressModeV = AddressModeToVulkan(desc.wrapV);
		s.addressModeW = AddressModeToVulkan(desc.wrapW);
		s.anisotropyEnable = desc.maxAniso > 1.0f;
		s.magFilter = desc.magFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.minFilter = desc.minFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.mipmapMode = desc.mipFilter == TextureFilter::LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.maxLod = desc.maxLod;
		VkResult res = vkCreateSampler(vulkan_->GetDevice(), &s, nullptr, &sampler_);
		assert(VK_SUCCESS == res);
	}
	~VKSamplerState() {
		vulkan_->Delete().QueueDeleteSampler(sampler_);
	}

	VkSampler GetSampler() { return sampler_; }

private:
	VulkanContext *vulkan_;
	VkSampler sampler_;
};

SamplerState *VKContext::CreateSamplerState(const SamplerStateDesc &desc) {
	return new VKSamplerState(vulkan_, desc);
}

RasterState *VKContext::CreateRasterState(const RasterStateDesc &desc) {
	return new VKRasterState(vulkan_, desc);
}

void VKContext::BindSamplerStates(int start, int count, SamplerState **state) {
	for (int i = start; i < start + count; i++) {
		boundSamplers_[i] = (VKSamplerState *)state[i];
	}
}

enum class TextureState {
	UNINITIALIZED,
	STAGED,
	INITIALIZED,
	PENDING_DESTRUCTION,
};

bool VKTexture::Create(const TextureDesc &desc) {
	// Zero-sized textures not allowed.
	if (desc.width * desc.height * desc.depth == 0)
		return false;
	format_ = desc.format;
	mipLevels_ = desc.mipLevels;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	vkTex_ = new VulkanTexture(vulkan_);
	if (desc.initData.size()) {
		for (int i = 0; i < (int)desc.initData.size(); i++) {
			this->SetImageData(0, 0, 0, width_, height_, depth_, i, 0, desc.initData[i]);
		}
	}
	return true;
}

VKContext::VKContext(VulkanContext *vulkan)
	: viewportDirty_(false), scissorDirty_(false), vulkan_(vulkan), frameNum_(0), caps_{} {
	caps_.anisoSupported = vulkan->GetFeaturesAvailable().samplerAnisotropy != 0;
	caps_.geometryShaderSupported = vulkan->GetFeaturesAvailable().geometryShader != 0;
	caps_.tesselationShaderSupported = vulkan->GetFeaturesAvailable().tessellationShader != 0;
	caps_.multiViewport = vulkan->GetFeaturesAvailable().multiViewport != 0;
	caps_.dualSourceBlend = vulkan->GetFeaturesAvailable().dualSrcBlend != 0;
	caps_.framebufferBlitSupported = true;
	caps_.framebufferCopySupported = true;
	caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
	device_ = vulkan->GetDevice();

	queue_ = vulkan->GetGraphicsQueue();
	queueFamilyIndex_ = vulkan->GetGraphicsQueueFamilyIndex();
	scissor_.offset.x = 0;
	scissor_.offset.y = 0;
	scissor_.extent.width = pixel_xres;
	scissor_.extent.height = pixel_yres;
	viewport_.x = 0;
	viewport_.y = 0;
	viewport_.width = pixel_xres;
	viewport_.height = pixel_yres;
	viewport_.minDepth = 0.0f;
	viewport_.maxDepth = 0.0f;
	memset(boundTextures_, 0, sizeof(boundTextures_));

	VkDescriptorPoolSize dpTypes[2];
	dpTypes[0].descriptorCount = 200;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	dpTypes[1].descriptorCount = 200;
	dpTypes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dp = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dp.flags = 0;   // Don't want to mess around with individually freeing these, let's go dynamic each frame.
	dp.maxSets = 200;  // 200 textures per frame should be enough for the UI...
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);

	VkCommandPoolCreateInfo p = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	p.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	p.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();

	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		VkResult res = vkCreateDescriptorPool(device_, &dp, nullptr, &frame_[i].descriptorPool);
		assert(VK_SUCCESS == res);
		res = vkCreateCommandPool(device_, &p, nullptr, &frame_[i].cmdPool_);
		assert(VK_SUCCESS == res);
		frame_[i].pushBuffer = new VulkanPushBuffer(vulkan_, 1024 * 1024);
	}

	// binding 0 - uniform data
	// binding 1 - combined sampler/image
	VkDescriptorSetLayoutBinding bindings[2];
	bindings[0].descriptorCount = 1;
	bindings[0].pImmutableSamplers = nullptr;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[0].binding = 0;
	bindings[1].descriptorCount = 1;
	bindings[1].pImmutableSamplers = nullptr;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;

	VkDescriptorSetLayoutCreateInfo dsl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dsl.bindingCount = 2;
	dsl.pBindings = bindings;
	VkResult res = vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &descriptorSetLayout_);
	assert(VK_SUCCESS == res);

	VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pl.pPushConstantRanges = nullptr;
	pl.pushConstantRangeCount = 0;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	res = vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);

	pipelineCache_ = vulkan_->CreatePipelineCache();

	// Create a bunch of render pass objects, for normal rendering with a depth buffer,
	// with clearing, without clearing, and dont-care for both depth/stencil and color, so 3*3=9 combos.
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].flags = 0;

	attachments[1].format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference color_reference = {};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_reference = {};
	depth_reference.attachment = 1;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	subpass.pResolveAttachments = NULL;
	subpass.pDepthStencilAttachment = &depth_reference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp.attachmentCount = 2;
	rp.pAttachments = attachments;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 0;
	rp.pDependencies = NULL;

	for (int depth = 0; depth < 3; depth++) {
		switch ((RPAction)depth) {
		case RPAction::CLEAR: attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; break;
		case RPAction::KEEP: attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; break;
		case RPAction::DONT_CARE: attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; break;
		}
		for (int color = 0; color < 3; color++) {
			switch ((RPAction)color) {
			case RPAction::CLEAR: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; break;
			case RPAction::KEEP: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; break;
			case RPAction::DONT_CARE: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; break;
			}
			vkCreateRenderPass(vulkan_->GetDevice(), &rp, nullptr, &renderPasses_[RPIndex((RPAction)color, (RPAction)depth)]);
		}
	}
}

VKContext::~VKContext() {
	for (int i = 0; i < 9; i++) {
		vulkan_->Delete().QueueDeleteRenderPass(renderPasses_[i]);
	}
	// This also destroys all descriptor sets.
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frame_[i].descSets_.clear();
		vulkan_->Delete().QueueDeleteDescriptorPool(frame_[i].descriptorPool);
		frame_[i].pushBuffer->Destroy(vulkan_);
		delete frame_[i].pushBuffer;
		vulkan_->Delete().QueueDeleteCommandPool(frame_[i].cmdPool_);
	}
	vulkan_->Delete().QueueDeleteDescriptorSetLayout(descriptorSetLayout_);
	vulkan_->Delete().QueueDeletePipelineLayout(pipelineLayout_);
	vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
}

// Effectively wiped every frame, just allocate new ones!
VkCommandBuffer VKContext::AllocCmdBuf() {
	FrameData *frame = &frame_[frameNum_];

	if (frame->numCmdBufs >= MAX_FRAME_COMMAND_BUFFERS)
		Crash();

	if (frame->cmdBufs[frame->numCmdBufs]) {
		VkCommandBuffer cmdBuf = frame->cmdBufs[frame->numCmdBufs++];
		if (!cmdBuf)
			Crash();
		return cmdBuf;
	}

	VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	alloc.commandBufferCount = 1;
	alloc.commandPool = frame->cmdPool_;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	VkResult result = vkAllocateCommandBuffers(vulkan_->GetDevice(), &alloc, &frame->cmdBufs[frame->numCmdBufs]);
	assert(result == VK_SUCCESS);
	VkCommandBuffer cmdBuf = frame->cmdBufs[frame->numCmdBufs++];
	if (!cmdBuf)
		Crash();
	return cmdBuf;
}

void VKContext::BeginFrame() {
	vulkan_->BeginFrame();

	FrameData &frame = frame_[frameNum_];
	frame.startCmdBufs_ = 0;
	frame.numCmdBufs = 0;
	vkResetCommandPool(vulkan_->GetDevice(), frame.cmdPool_, 0);
	push_ = frame.pushBuffer;

	// OK, we now know that nothing is reading from this frame's data pushbuffer,
	push_->Reset();
	push_->Begin(vulkan_);

	frame.descSets_.clear();
	VkResult result = vkResetDescriptorPool(device_, frame.descriptorPool, 0);
	assert(result == VK_SUCCESS);

	scissor_.extent.width = pixel_xres;
	scissor_.extent.height = pixel_yres;
	scissorDirty_ = true;
	viewportDirty_ = true;
}

void VKContext::WaitRenderCompletion(Framebuffer *fbo) {
	// TODO
}

void VKContext::EndFrame() {
	EndCurrentRenderpass();

	if (cmd_)
		Crash();

	// Cap off and submit all the command buffers we recorded during the frame.
	FrameData &frame = frame_[frameNum_];
	for (int i = frame.startCmdBufs_; i < frame.numCmdBufs; i++) {
		vkEndCommandBuffer(frame.cmdBufs[i]);
		vulkan_->QueueBeforeSurfaceRender(frame.cmdBufs[i]);
	}
	frame.startCmdBufs_ = frame.numCmdBufs;

	// Stop collecting data in the frame's data pushbuffer.
	push_->End();
	vulkan_->EndFrame();

	frameNum_++;
	if (frameNum_ >= vulkan_->GetInflightFrames())
		frameNum_ = 0;
	push_ = nullptr;

	DirtyDynamicState();
}

VkDescriptorSet VKContext::GetOrCreateDescriptorSet(VkBuffer buf) {
	DescriptorSetKey key;

	FrameData *frame = &frame_[frameNum_];

	key.texture_ = boundTextures_[0];
	key.sampler_ = boundSamplers_[0];
	key.buffer_ = buf;

	auto iter = frame->descSets_.find(key);
	if (iter != frame->descSets_.end()) {
		return iter->second;
	}

	VkDescriptorSet descSet;
	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	alloc.descriptorPool = frame->descriptorPool;
	alloc.pSetLayouts = &descriptorSetLayout_;
	alloc.descriptorSetCount = 1;
	VkResult res = vkAllocateDescriptorSets(device_, &alloc, &descSet);
	assert(VK_SUCCESS == res);

	VkDescriptorBufferInfo bufferDesc;
	bufferDesc.buffer = buf;
	bufferDesc.offset = 0;
	bufferDesc.range = curPipeline_->GetUBOSize();

	VkDescriptorImageInfo imageDesc;
	imageDesc.imageView = boundTextures_[0]->GetImageView();
	imageDesc.sampler = boundSamplers_[0]->GetSampler();
	imageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet writes[2] = {};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = descSet;
	writes[0].dstArrayElement = 0;
	writes[0].dstBinding = 0;
	writes[0].pBufferInfo = &bufferDesc;
	writes[0].pImageInfo = nullptr;
	writes[0].pTexelBufferView = nullptr;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = descSet;
	writes[1].dstArrayElement = 0;
	writes[1].dstBinding = 1;
	writes[1].pBufferInfo = nullptr;
	writes[1].pImageInfo = &imageDesc;
	writes[1].pTexelBufferView = nullptr;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

	frame->descSets_[key] = descSet;
	return descSet;
}

Pipeline *VKContext::CreateGraphicsPipeline(const PipelineDesc &desc) {
	VKPipeline *pipeline = new VKPipeline(vulkan_, desc.uniformDesc ? desc.uniformDesc->uniformBufferSize : 16 * sizeof(float));
	VKInputLayout *input = (VKInputLayout *)desc.inputLayout;
	VKBlendState *blend = (VKBlendState *)desc.blend;
	VKDepthStencilState *depth = (VKDepthStencilState *)desc.depthStencil;
	VKRasterState *raster = (VKRasterState *)desc.raster;
	for (int i = 0; i < (int)input->bindings.size(); i++) {
		pipeline->stride[i] = input->bindings[i].stride;
	}

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	stages.resize(desc.shaders.size());
	int i = 0;
	for (auto &iter : desc.shaders) {
		VKShaderModule *vkshader = (VKShaderModule *)iter;
		if (!vkshader) {
			ELOG("CreateGraphicsPipeline got passed a null shader");
			return nullptr;
		}
		VkPipelineShaderStageCreateInfo &stage = stages[i++];
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.pNext = nullptr;
		stage.pSpecializationInfo = nullptr;
		stage.stage = StageToVulkan(vkshader->GetStage());
		stage.module = vkshader->Get();
		stage.pName = "main";
		stage.flags = 0;
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = primToVK[(int)desc.prim];
	inputAssembly.primitiveRestartEnable = false;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicInfo.dynamicStateCount = ARRAY_SIZE(dynamics);
	dynamicInfo.pDynamicStates = dynamics;

	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.pNext = nullptr;
	ms.pSampleMask = nullptr;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineViewportStateCreateInfo vs = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vs.pNext = nullptr;
	vs.viewportCount = 1;
	vs.scissorCount = 1;
	vs.pViewports = nullptr;  // dynamic
	vs.pScissors = nullptr;  // dynamic

	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster->ToVulkan(&rs);

	VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	info.pNext = nullptr;
	info.flags = 0;
	info.stageCount = (uint32_t)stages.size();
	info.pStages = stages.data();
	info.pColorBlendState = &blend->info;
	info.pDepthStencilState = &depth->info;
	info.pDynamicState = &dynamicInfo;
	info.pInputAssemblyState = &inputAssembly;
	info.pTessellationState = nullptr;
	info.pMultisampleState = &ms;
	info.pVertexInputState = &input->visc;
	info.pRasterizationState = &rs;
	info.pViewportState = &vs;  // Must set viewport and scissor counts even if we set the actual state dynamically.
	info.layout = pipelineLayout_;
	info.subpass = 0;
	info.renderPass = vulkan_->GetSurfaceRenderPass();

	// OK, need to create a new pipeline.
	VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &pipeline->vkpipeline);
	if (result != VK_SUCCESS) {
		ELOG("Failed to create graphics pipeline");
		delete pipeline;
		return nullptr;
	}
	if (desc.uniformDesc) {
		pipeline->dynamicUniformSize = (int)desc.uniformDesc->uniformBufferSize;
	}

	return pipeline;
}

void VKContext::SetScissorRect(int left, int top, int width, int height) {
	scissor_.offset.x = left;
	scissor_.offset.y = top;
	scissor_.extent.width = width;
	scissor_.extent.height = height;
	scissorDirty_ = true;
}

void VKContext::SetViewports(int count, Viewport *viewports) {
	if (count > 0) {
		viewport_.x = viewports[0].TopLeftX;
		viewport_.y = viewports[0].TopLeftY;
		viewport_.width = viewports[0].Width;
		viewport_.height = viewports[0].Height;
		viewport_.minDepth = viewports[0].MinDepth;
		viewport_.maxDepth = viewports[0].MaxDepth;
		viewportDirty_ = true;
	}
}

void VKContext::SetBlendFactor(float color[4]) {
	vkCmdSetBlendConstants(cmd_, color);
}

void VKContext::ApplyDynamicState() {
	if (scissorDirty_) {
		vkCmdSetScissor(cmd_, 0, 1, &scissor_);
		scissorDirty_ = false;
	}
	if (viewportDirty_) {
		vkCmdSetViewport(cmd_, 0, 1, &viewport_);
		viewportDirty_ = false;
	}
}

void VKContext::DirtyDynamicState() {
	scissorDirty_ = true;
	viewportDirty_ = true;
}

InputLayout *VKContext::CreateInputLayout(const InputLayoutDesc &desc) {
	VKInputLayout *vl = new VKInputLayout();
	vl->visc = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vl->visc.flags = 0;
	vl->visc.vertexAttributeDescriptionCount = (uint32_t)desc.attributes.size();
	vl->visc.vertexBindingDescriptionCount = (uint32_t)desc.bindings.size();
	vl->bindings.resize(vl->visc.vertexBindingDescriptionCount);
	vl->attributes.resize(vl->visc.vertexAttributeDescriptionCount);
	vl->visc.pVertexBindingDescriptions = vl->bindings.data();
	vl->visc.pVertexAttributeDescriptions = vl->attributes.data();
	for (size_t i = 0; i < desc.attributes.size(); i++) {
		vl->attributes[i].binding = (uint32_t)desc.attributes[i].binding;
		vl->attributes[i].format = DataFormatToVulkan(desc.attributes[i].format);
		vl->attributes[i].location = desc.attributes[i].location;
		vl->attributes[i].offset = desc.attributes[i].offset;
	}
	for (size_t i = 0; i < desc.bindings.size(); i++) {
		vl->bindings[i].inputRate = desc.bindings[i].instanceRate ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
		vl->bindings[i].binding = (uint32_t)i;
		vl->bindings[i].stride = desc.bindings[i].stride;
	}
	return vl;
}

Texture *VKContext::CreateTexture(const TextureDesc &desc) {
	return new VKTexture(vulkan_, desc);
}

void VKTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	VkFormat vulkanFormat = DataFormatToVulkan(format_);
	if (stride == 0) {
		stride = width * (int)DataFormatSizeInBytes(format_);
	}
	int bpp = GetBpp(vulkanFormat);
	int bytesPerPixel = bpp / 8;
	vkTex_->Create(width, height, vulkanFormat);
	int rowPitch;
	uint8_t *dstData = vkTex_->Lock(0, &rowPitch);
	for (int y = 0; y < height; y++) {
		memcpy(dstData + rowPitch * y, data + stride * y, width * bytesPerPixel);
	}
	vkTex_->Unlock();
}

inline void CopySide(VkStencilOpState &dest, const StencilSide &src) {
	dest.compareMask = src.compareMask;
	dest.reference = src.reference;
	dest.writeMask = src.writeMask;
	dest.compareOp = compToVK[(int)src.compareOp];
	dest.failOp = stencilOpToVK[(int)src.failOp];
	dest.passOp = stencilOpToVK[(int)src.passOp];
	dest.depthFailOp = stencilOpToVK[(int)src.depthFailOp];
}

DepthStencilState *VKContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	VKDepthStencilState *ds = new VKDepthStencilState();
	ds->info.depthCompareOp = compToVK[(int)desc.depthCompare];
	ds->info.depthTestEnable = desc.depthTestEnabled;
	ds->info.depthWriteEnable = desc.depthWriteEnabled;
	ds->info.stencilTestEnable = false;
	ds->info.depthBoundsTestEnable = false;
	if (ds->info.stencilTestEnable) {
		CopySide(ds->info.front, desc.front);
		CopySide(ds->info.back, desc.back);
	}
	return ds;
}

BlendState *VKContext::CreateBlendState(const BlendStateDesc &desc) {
	VKBlendState *bs = new VKBlendState();
	bs->info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	bs->info.attachmentCount = 1;
	bs->info.logicOp = logicOpToVK[(int)desc.logicOp];
	bs->info.logicOpEnable = desc.logicEnabled;
	bs->attachments.resize(1);
	bs->attachments[0].blendEnable = desc.enabled;
	bs->attachments[0].colorBlendOp = blendEqToVk[(int)desc.eqCol];
	bs->attachments[0].alphaBlendOp = blendEqToVk[(int)desc.eqAlpha];
	bs->attachments[0].colorWriteMask = desc.colorMask;
	bs->attachments[0].dstAlphaBlendFactor = blendFactorToVk[(int)desc.dstAlpha];
	bs->attachments[0].dstColorBlendFactor = blendFactorToVk[(int)desc.dstCol];
	bs->attachments[0].srcAlphaBlendFactor = blendFactorToVk[(int)desc.srcAlpha];
	bs->attachments[0].srcColorBlendFactor = blendFactorToVk[(int)desc.srcCol];
	bs->info.pAttachments = bs->attachments.data();
	return bs;
}

// Very simplistic buffer that will simply copy its contents into our "pushbuffer" when it's time to draw,
// to avoid synchronization issues.
class VKBuffer : public Buffer {
public:
	VKBuffer(size_t size, uint32_t flags) : dataSize_(size) {
		data_ = new uint8_t[size];
	}
	~VKBuffer() override {
		delete[] data_;
	}

	size_t GetSize() const { return dataSize_; }
	const uint8_t *GetData() const { return data_; }

	uint8_t *data_;
	size_t dataSize_;
};

Buffer *VKContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new VKBuffer(size, usageFlags);
}

void VKContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	VKBuffer *buf = (VKBuffer *)buffer;
	memcpy(buf->data_ + offset, data, size);
}

void VKContext::BindTextures(int start, int count, Texture **textures) {
	for (int i = start; i < start + count; i++) {
		boundTextures_[i] = static_cast<VKTexture *>(textures[i]);
		boundImageView_[i] = boundTextures_[i]->GetImageView();
	}
}

ShaderModule *VKContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t size) {
	VKShaderModule *shader = new VKShaderModule(stage);
	if (shader->Compile(vulkan_, language, data, size)) {
		return shader;
	} else {
		ELOG("Failed to compile shader: %s", (const char *)data);
		shader->Release();
		return nullptr;
	}
}

int VKPipeline::GetUniformLoc(const char *name) {
	int loc = -1;

	// HACK! As we only use one uniform we hardcode it.
	if (!strcmp(name, "WorldViewProj")) {
		return 0;
	}

	return loc;
}

inline VkPrimitiveTopology PrimToVK(Primitive prim) {
	switch (prim) {
	case Primitive::POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	case Primitive::LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case Primitive::LINE_LIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
	case Primitive::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case Primitive::LINE_STRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
	case Primitive::TRIANGLE_LIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case Primitive::TRIANGLE_LIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
	case Primitive::TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	case Primitive::TRIANGLE_STRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
	case Primitive::TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	case Primitive::PATCH_LIST: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
	default:
		return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	}
}

void VKContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	curPipeline_->SetDynamicUniformData(ub, size);
}

void VKContext::Draw(int vertexCount, int offset) {
	ApplyDynamicState();
	
	VKBuffer *vbuf = curVBuffers_[0];

	VkBuffer vulkanVbuf;
	VkBuffer vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);

	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, curPipeline_->vkpipeline);
	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);
	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);
	vkCmdDraw(cmd_, vertexCount, 1, offset, 0);
}

void VKContext::DrawIndexed(int vertexCount, int offset) {
	ApplyDynamicState();
	
	VKBuffer *ibuf = static_cast<VKBuffer *>(curIBuffer_);
	VKBuffer *vbuf = static_cast<VKBuffer *>(curVBuffers_[0]);

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);
	size_t ibBindOffset = push_->Push(ibuf->GetData(), ibuf->GetSize(), &vulkanIbuf);

	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, curPipeline_->vkpipeline);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);

	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);

	vkCmdBindIndexBuffer(cmd_, vulkanIbuf, ibBindOffset, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd_, vertexCount, 1, 0, offset, 0);
}

void VKContext::DrawUP(const void *vdata, int vertexCount) {
	ApplyDynamicState();

	VkBuffer vulkanVbuf, vulkanUBObuf;
	size_t vbBindOffset = push_->Push(vdata, vertexCount * curPipeline_->stride[0], &vulkanVbuf);
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, curPipeline_->vkpipeline);

	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);
	vkCmdDraw(cmd_, vertexCount, 1, 0, 0);
}

// TODO: We should avoid this function as much as possible, instead use renderpass on-load clearing.
void VKContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	if (!curRenderPass_) {
		ELOG("Clear: Need an active render pass");
		return;
	}

	int numAttachments = 0;
	VkClearRect rc{};
	rc.baseArrayLayer = 0;
	rc.layerCount = 1;
	rc.rect.extent.width = curWidth_;
	rc.rect.extent.height = curHeight_;
	VkClearAttachment attachments[2];
	if (mask & FBChannel::FB_COLOR_BIT) {
		VkClearAttachment &attachment = attachments[numAttachments++];
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = 0;
		Uint8x4ToFloat4(colorval, attachment.clearValue.color.float32);
	}
	if (mask & (FBChannel::FB_DEPTH_BIT | FBChannel::FB_STENCIL_BIT)) {
		VkClearAttachment &attachment = attachments[numAttachments++];
		attachment.aspectMask = 0;
		if (mask & FBChannel::FB_DEPTH_BIT) {
			attachment.clearValue.depthStencil.depth = depthVal;
			attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (mask & FBChannel::FB_STENCIL_BIT) {
			attachment.clearValue.depthStencil.stencil = stencilVal;
			attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	if (numAttachments) {
		vkCmdClearAttachments(cmd_, numAttachments, attachments, 1, &rc);
	}
}

DrawContext *T3DCreateVulkanContext(VulkanContext *vulkan) {
	return new VKContext(vulkan);
}

void AddFeature(std::vector<std::string> &features, const char *name, VkBool32 available, VkBool32 enabled) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s: Available: %d Enabled: %d", name, (int)available, (int)enabled);
	features.push_back(buf);
}

std::vector<std::string> VKContext::GetFeatureList() const {
	const VkPhysicalDeviceFeatures &available = vulkan_->GetFeaturesAvailable();
	const VkPhysicalDeviceFeatures &enabled = vulkan_->GetFeaturesEnabled();

	std::vector<std::string> features;
	AddFeature(features, "dualSrcBlend", available.dualSrcBlend, enabled.dualSrcBlend);
	AddFeature(features, "logicOp", available.logicOp, enabled.logicOp);
	AddFeature(features, "geometryShader", available.geometryShader, enabled.geometryShader);
	AddFeature(features, "depthBounds", available.depthBounds, enabled.depthBounds);
	AddFeature(features, "depthClamp", available.depthClamp, enabled.depthClamp);
	AddFeature(features, "fillModeNonSolid", available.fillModeNonSolid, enabled.fillModeNonSolid);
	AddFeature(features, "largePoints", available.largePoints, enabled.largePoints);
	AddFeature(features, "wideLines", available.wideLines, enabled.wideLines);
	AddFeature(features, "pipelineStatisticsQuery", available.pipelineStatisticsQuery, enabled.pipelineStatisticsQuery);
	AddFeature(features, "samplerAnisotropy", available.samplerAnisotropy, enabled.samplerAnisotropy);
	AddFeature(features, "textureCompressionBC", available.textureCompressionBC, enabled.textureCompressionBC);
	AddFeature(features, "textureCompressionETC2", available.textureCompressionETC2, enabled.textureCompressionETC2);
	AddFeature(features, "textureCompressionASTC_LDR", available.textureCompressionASTC_LDR, enabled.textureCompressionASTC_LDR);
	AddFeature(features, "shaderClipDistance", available.shaderClipDistance, enabled.shaderClipDistance);
	AddFeature(features, "shaderCullDistance", available.shaderCullDistance, enabled.shaderCullDistance);
	AddFeature(features, "occlusionQueryPrecise", available.occlusionQueryPrecise, enabled.occlusionQueryPrecise);
	AddFeature(features, "multiDrawIndirect", available.multiDrawIndirect, enabled.multiDrawIndirect);

	// Also list texture formats and their properties.
	for (int i = VK_FORMAT_BEGIN_RANGE; i <= VK_FORMAT_END_RANGE; i++) {
		// TODO
	}

	return features;
}

std::vector<std::string> VKContext::GetExtensionList() const {
	std::vector<std::string> extensions;
	for (auto &iter : vulkan_->GetDeviceExtensionsAvailable()) {
		extensions.push_back(iter.extensionName);
	}
	return extensions;
}

uint32_t VKContext::GetDataFormatSupport(DataFormat fmt) const {
	// TODO: Actually do proper checks
	switch (fmt) {
	case DataFormat::B8G8R8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE;
	case DataFormat::B4G4R4A4_UNORM_PACK16:
		// This is the one that's guaranteed to be supported.
		// A four-component, 16-bit packed unsigned normalized format that has a 4-bit B component in bits 12..15, a 4-bit
		// G component in bits 8..11, a 4 - bit R component in bits 4..7, and a 4 - bit A component in bits 0..3
		return FMT_RENDERTARGET | FMT_TEXTURE;
	case DataFormat::R4G4B4A4_UNORM_PACK16:
		return 0;
	case DataFormat::A4R4G4B4_UNORM_PACK16:
		return 0;

	case DataFormat::R8G8B8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_INPUTLAYOUT;

	case DataFormat::R32_FLOAT:
	case DataFormat::R32G32_FLOAT:
	case DataFormat::R32G32B32_FLOAT:
	case DataFormat::R32G32B32A32_FLOAT:
		return FMT_INPUTLAYOUT;

	case DataFormat::R8_UNORM:
		return FMT_TEXTURE;

	case DataFormat::BC1_RGBA_UNORM_BLOCK:
	case DataFormat::BC2_UNORM_BLOCK:
	case DataFormat::BC3_UNORM_BLOCK:
		return FMT_TEXTURE;
	default:
		return 0;
	}
}

void CreateImage(VulkanContext *vulkan, VKImage &img, int width, int height, VkFormat format, VkImageLayout initialLayout, bool color) {
	VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.arrayLayers = 1;
	ici.mipLevels = 1;
	ici.extent.width = width;
	ici.extent.height = height;
	ici.extent.depth = 1;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.format = format;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (color) {
		ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	} else {
		ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	vkCreateImage(vulkan->GetDevice(), &ici, nullptr, &img.image);

	// TODO: If available, use nVidia's VK_NV_dedicated_allocation for framebuffers

	VkMemoryRequirements memreq;
	vkGetImageMemoryRequirements(vulkan->GetDevice(), img.image, &memreq);
	
	VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = memreq.size;
	vulkan->MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex);
	VkResult res = vkAllocateMemory(vulkan->GetDevice(), &alloc, nullptr, &img.memory);
	assert(res == VK_SUCCESS);
	res = vkBindImageMemory(vulkan->GetDevice(), img.image, img.memory, 0);
	assert(res == VK_SUCCESS);

	VkImageAspectFlags aspects = color ? VK_IMAGE_ASPECT_COLOR_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	VkImageViewCreateInfo ivci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ivci.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	ivci.format = ici.format;
	ivci.image = img.image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.subresourceRange.aspectMask = aspects;
	ivci.subresourceRange.layerCount = 1;
	ivci.subresourceRange.levelCount = 1;
	res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.view);
	assert(res == VK_SUCCESS);

	VkPipelineStageFlagBits dstStage;
	VkAccessFlagBits dstAccessMask;
	switch (initialLayout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		break;
	}

	TransitionImageLayout2(vulkan->GetInitCommandBuffer(), img.image, aspects,
		VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dstStage,
		0, dstAccessMask);
}

// A VKFramebuffer is a VkFramebuffer (note caps difference) plus all the textures it owns.
// It also has a reference to the command buffer that it was last rendered to with.
// If it needs to be transitioned, and the frame number matches, use it, otherwise
// use this frame's init command buffer.
class VKFramebuffer : public Framebuffer {
public:
	VKFramebuffer(VulkanContext *vk) : vulkan_(vk) {}
	~VKFramebuffer() {
		vulkan_->Delete().QueueDeleteImage(color.image);
		vulkan_->Delete().QueueDeleteImage(depth.image);
		vulkan_->Delete().QueueDeleteImageView(color.view);
		vulkan_->Delete().QueueDeleteImageView(depth.view);
		vulkan_->Delete().QueueDeleteDeviceMemory(color.memory);
		vulkan_->Delete().QueueDeleteDeviceMemory(depth.memory);
		vulkan_->Delete().QueueDeleteFramebuffer(framebuf);
	}
	VkFramebuffer framebuf = VK_NULL_HANDLE;
	VKImage color{};
	VKImage depth{};
	int width = 0;
	int height = 0;

	// These belong together, see above.
	VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
	int frameCount = 0;

private:
	VulkanContext *vulkan_;
};

Framebuffer *VKContext::CreateFramebuffer(const FramebufferDesc &desc) {
	VKFramebuffer *fb = new VKFramebuffer(vulkan_);
	fb->width = desc.width;
	fb->height = desc.height;

	CreateImage(vulkan_, fb->color, fb->width, fb->height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
	CreateImage(vulkan_, fb->depth, fb->width, fb->height, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

	VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	VkImageView views[2]{};

	fbci.renderPass = renderPasses_[0];
	fbci.attachmentCount = 2;
	fbci.pAttachments = views;
	views[0] = fb->color.view;
	views[1] = fb->depth.view;
	fbci.width = fb->width;
	fbci.height = fb->height;
	fbci.layers = 1;
	
	vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &fb->framebuf);
	return fb;
}

void VKContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits) {
	// Can't copy during render passes.
	EndCurrentRenderpass();

	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	VkImageCopy copy{};
	copy.srcOffset.x = x;
	copy.srcOffset.y = y;
	copy.srcOffset.z = z;
	copy.srcSubresource.mipLevel = level;
	copy.srcSubresource.layerCount = 1;
	copy.dstOffset.x = dstX;
	copy.dstOffset.y = dstY;
	copy.dstOffset.z = dstZ;
	copy.dstSubresource.mipLevel = dstLevel;
	copy.dstSubresource.layerCount = 1;
	copy.extent.width = width;
	copy.extent.height = height;
	copy.extent.depth = depth;

	// We're gonna tack copies onto the src's command buffer, if it's from this frame.
	// If from a previous frame, just do it in frame init.
	VkCommandBuffer cmd = src->cmdBuf;
	if (src->frameCount != frameNum_) {
		// TODO: What about the case where dst->frameCount == frameNum_ here? 
		// That will cause bad ordering. We'll have to allocate a new command buffer and assign it to dest.
		cmd = vulkan_->GetInitCommandBuffer();
	}

	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};
	int srcCount = 0;
	int dstCount = 0;

	// First source barriers.
	if (channelBits & FB_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (channelBits & (FB_DEPTH_BIT | FB_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	// TODO: Fix the pipe bits to be bit less conservative.
	if (srcCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (channelBits & FB_COLOR_BIT) {
		copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &copy);
	}
	if (channelBits & (FB_DEPTH_BIT | FB_STENCIL_BIT)) {
		copy.srcSubresource.aspectMask = 0;
		copy.dstSubresource.aspectMask = 0;
		if (channelBits & FB_DEPTH_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (channelBits & FB_STENCIL_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdCopyImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &copy);
	}
}

bool VKContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	// We're gonna tack blits onto the src's command buffer, if it's from this frame.
	// If from a previous frame, just do it in frame init.
	VkCommandBuffer cmd = src->cmdBuf;
	if (src->frameCount != frameNum_) {
		// TODO: What about the case where dst->frameCount == frameNum_ here? 
		// That will cause bad ordering. We'll have to allocate a new command buffer and assign it to dest.
		cmd = vulkan_->GetInitCommandBuffer();
	}
	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};
	int srcCount = 0;
	int dstCount = 0;

	VkImageBlit blit{};
	blit.srcOffsets[0].x = srcX1;
	blit.srcOffsets[0].y = srcY1;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = srcX2;
	blit.srcOffsets[1].y = srcY2;
	blit.srcOffsets[1].z = 1;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.layerCount = 1;
	blit.dstOffsets[0].x = dstX1;
	blit.dstOffsets[0].y = dstY1;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = dstX2;
	blit.dstOffsets[1].y = dstY2;
	blit.dstOffsets[1].z = 1;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.layerCount = 1;

	// First source barriers.
	if (channelBits & FB_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (channelBits & (FB_DEPTH_BIT | FB_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	// TODO: Fix the pipe bits to be bit less conservative.
	if (srcCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (channelBits & FB_COLOR_BIT) {
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdBlitImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &blit, filter == FB_BLIT_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
	}
	if (channelBits & (FB_DEPTH_BIT | FB_STENCIL_BIT)) {
		blit.srcSubresource.aspectMask = 0;
		blit.dstSubresource.aspectMask = 0;
		if (channelBits & FB_DEPTH_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (channelBits & FB_STENCIL_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdBlitImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &blit, filter == FB_BLIT_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
	}
	return true;
}

void VKContext::SetupTransitionToTransferSrc(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	img.layout = barrier.newLayout;
}

void VKContext::SetupTransitionToTransferDst(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	img.layout = barrier.newLayout;
}

void VKContext::EndCurrentRenderpass() {
	if (curRenderPass_ != VK_NULL_HANDLE) {
		vkCmdEndRenderPass(cmd_);
		curRenderPass_ = VK_NULL_HANDLE;
		curFramebuffer_ = VK_NULL_HANDLE;
		cmd_ = VK_NULL_HANDLE;
	}
}

void VKContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) {
	VkFramebuffer framebuf;
	VkCommandBuffer cmdBuf;
	int w;
	int h;
	VkImageLayout prevLayout;
	if (fbo) {
		VKFramebuffer *fb = (VKFramebuffer *)fbo;
		framebuf = fb->framebuf;
		w = fb->width;
		h = fb->height;
		prevLayout = fb->color.layout;
		cmdBuf = fb->cmdBuf;
	} else {
		framebuf = vulkan_->GetSurfaceFramebuffer();
		w = vulkan_->GetWidth();
		h = vulkan_->GetHeight();
		cmdBuf = vulkan_->GetSurfaceCommandBuffer();
	}

	if (framebuf == curFramebuffer_) {
		if (framebuf == 0)
			Crash();
		if (!curRenderPass_)
			Crash();
		// If we're asking to clear, but already bound, we'll just keep it bound but send a clear command.
		// We will try to avoid this as much as possible.
		VkClearAttachment clear[2]{};
		int count = 0;
		if (rp.color == RPAction::CLEAR) {
			clear[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Uint8x4ToFloat4(rp.clearColor, clear[count].clearValue.color.float32);
			clear[count].colorAttachment = 0;
			count++;
		}
		if (rp.depth == RPAction::CLEAR) {
			clear[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			clear[count].clearValue.depthStencil.depth = rp.clearDepth;
			clear[count].clearValue.depthStencil.stencil = rp.clearStencil;
			clear[count].colorAttachment = 0;
			count++;
		}
		if (count > 0) {
			VkClearRect rc{ { 0,0,(uint32_t)w,(uint32_t)h }, 0, 1 };
			vkCmdClearAttachments(cmdBuf, count, clear, 1, &rc);
		}
		// We're done.
		return;
	}

	// OK, we're switching framebuffers.
	EndCurrentRenderpass();
	VkRenderPass renderPass;
	int numClearVals = 0;
	VkClearValue clearVal[2];
	memset(clearVal, 0, sizeof(clearVal));
	if (fbo) {
		VKFramebuffer *fb = (VKFramebuffer *)fbo;
		fb->cmdBuf = AllocCmdBuf();
		if (!fb->cmdBuf)
			Crash();
		fb->frameCount = frameNum_;
		cmd_ = fb->cmdBuf;
		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VkResult res = vkBeginCommandBuffer(cmd_, &begin);
		assert(res == VK_SUCCESS);
		// Now, if the image needs transitioning, let's transition.
		// The backbuffer does not, that's handled by VulkanContext.
		if (fb->color.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			VkAccessFlagBits srcAccessMask;
			VkPipelineStageFlagBits srcStage;
			switch (fb->color.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_UNDEFINED:
				srcAccessMask = (VkAccessFlagBits)0;
				srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				break;
			default:
				assert(0);
				break;
			}
			TransitionImageLayout2(cmd_, fb->color.image, VK_IMAGE_ASPECT_COLOR_BIT,
				fb->color.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				srcAccessMask, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
			fb->color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (fb->depth.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			VkAccessFlagBits srcAccessMask;
			VkPipelineStageFlagBits srcStage;
			switch (fb->depth.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_UNDEFINED:
				srcAccessMask = (VkAccessFlagBits)0;
				srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				break;
			default:
				assert(0);
				break;
			}
			TransitionImageLayout2(cmd_, fb->depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				fb->depth.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				srcAccessMask, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
			fb->depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		renderPass = renderPasses_[RPIndex(rp.color, rp.depth)];
		// ILOG("Switching framebuffer to FBO (fc=%d, cmd=%x, rp=%x)", frameNum_, (int)(uintptr_t)cmd_, (int)(uintptr_t)renderPass);
		if (rp.color == RPAction::CLEAR) {
			Uint8x4ToFloat4(rp.clearColor, clearVal[0].color.float32);
			numClearVals = 1;
		}
		if (rp.depth == RPAction::CLEAR) {
			clearVal[1].depthStencil.depth = rp.clearDepth;
			clearVal[1].depthStencil.stencil = rp.clearStencil;
			numClearVals = 2;
		}
	} else {
		cmd_ = vulkan_->GetSurfaceCommandBuffer();
		renderPass = vulkan_->GetSurfaceRenderPass();
		numClearVals = 2;
	}

	VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rp_begin.renderPass = renderPass;
	rp_begin.framebuffer = framebuf;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = w;
	rp_begin.renderArea.extent.height = h;
	rp_begin.clearValueCount = numClearVals;
	rp_begin.pClearValues = numClearVals ? clearVal : nullptr;
	vkCmdBeginRenderPass(cmd_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
	curFramebuffer_ = framebuf;
	curRenderPass_ = renderPass;
	curWidth_ = w;
	curHeight_ = h;
}

// color must be 0, for now.
void VKContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	boundImageView_[0] = fb->color.view;
	// If we already have the right layout, nothing else to do.
	if (fb->color.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		return;

	VkCommandBuffer transitionCmdBuf;
	if (fb->cmdBuf && fb->frameCount == frameNum_) {
		// If the framebuffer has a "live" command buffer, we can directly use it to transition it for sampling.
		transitionCmdBuf = fb->cmdBuf;
	} else {
		// If not, we can just do it at the "start" of the frame.
		transitionCmdBuf = vulkan_->GetInitCommandBuffer();
	}

	VkAccessFlags srcAccessMask;
	VkPipelineStageFlags srcStage;
	switch (fb->color.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	}
	TransitionImageLayout2(transitionCmdBuf, fb->color.image, VK_IMAGE_ASPECT_COLOR_BIT,
		fb->color.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		srcStage, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		srcAccessMask, VK_ACCESS_SHADER_READ_BIT);
	fb->color.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

uintptr_t VKContext::GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) {
	// TODO: Insert transition at the end of the previous command buffer, or the one that rendered to it last.
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	return (uintptr_t)fb->color.image;
}

void VKContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	if (fb) {
		*w = fb->width;
		*h = fb->height;
	} else {
		*w = vulkan_->GetWidth();
		*h = vulkan_->GetHeight();
	}
}

void VKContext::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	// Noop
}

}  // namespace Draw
