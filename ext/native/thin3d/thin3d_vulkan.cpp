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
#include <cassert>

#include "Common/Vulkan/SPIRVDisasm.h"
#include "Core/Config.h"

#include "base/logging.h"
#include "base/display.h"
#include "base/stringutil.h"
#include "image/zim_load.h"
#include "math/lin/matrix4x4.h"
#include "math/dataconv.h"
#include "thin3d/thin3d.h"
#include "thin3d/VulkanRenderManager.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"

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
			vulkan_->Delete().QueueDeleteShaderModule(module_);
		}
	}
	VkShaderModule Get() const { return module_; }
	ShaderStage GetStage() const override {
		return stage_;
	}

private:
	VulkanContext *vulkan_;
	VkShaderModule module_;
	VkShaderStageFlagBits vkstage_;
	bool ok_;
	ShaderStage stage_;
	std::string source_;  // So we can recompile in case of context loss.
};

bool VKShaderModule::Compile(VulkanContext *vulkan, ShaderLanguage language, const uint8_t *data, size_t size) {
	vulkan_ = vulkan;
	// We'll need this to free it later.
	source_ = (const char *)data;
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
		return buf->PushAligned(ubo_, uboSize_, vulkan->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).limits.minUniformBufferOffsetAlignment, vkbuf);
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
	VKTexture(VulkanContext *vulkan, VkCommandBuffer cmd, VulkanPushBuffer *pushBuffer, const TextureDesc &desc, VulkanDeviceAllocator *alloc)
		: vulkan_(vulkan), mipLevels_(desc.mipLevels), format_(desc.format) {
		bool result = Create(cmd, pushBuffer, desc, alloc);
		_assert_(result);
	}

	~VKTexture() {
		Destroy();
	}

	VkImageView GetImageView() {
		if (vkTex_) {
			vkTex_->Touch();
			return vkTex_->GetImageView();
		} else {
			// This would be bad.
			return VK_NULL_HANDLE;
		}
	}

private:
	bool Create(VkCommandBuffer cmd, VulkanPushBuffer *pushBuffer, const TextureDesc &desc, VulkanDeviceAllocator *alloc);

	void Destroy() {
		if (vkTex_) {
			vkTex_->Destroy();
			delete vkTex_;
			vkTex_ = nullptr;
		}
	}

	VulkanContext *vulkan_;
	VulkanTexture *vkTex_ = nullptr;

	int mipLevels_;

	DataFormat format_;
};

class VKFramebuffer;

class VKContext : public DrawContext {
public:
	VKContext(VulkanContext *vulkan, bool splitSubmit);
	virtual ~VKContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	std::vector<std::string> GetDeviceList() const override {
		std::vector<std::string> list;
		for (int i = 0; i < vulkan_->GetNumPhysicalDevices(); i++) {
			list.push_back(vulkan_->GetPhysicalDeviceProperties(i).deviceName);
		}
		return list;
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
	bool CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride) override;

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
	void WipeQueue() override;

	void FlushState() override {
	}
	void WaitRenderCompletion(Framebuffer *fbo) override;

	// From Sascha's code
	static std::string FormatDriverVersion(const VkPhysicalDeviceProperties &props) {
		if (props.vendorID == 4318) {
			// 10 bits = major version (up to r1023)
			// 8 bits = minor version (up to 255)
			// 8 bits = secondary branch version/build version (up to 255)
			// 6 bits = tertiary branch/build version (up to 63)
			uint32_t major = (props.driverVersion >> 22) & 0x3ff;
			uint32_t minor = (props.driverVersion >> 14) & 0x0ff;
			uint32_t secondaryBranch = (props.driverVersion >> 6) & 0x0ff;
			uint32_t tertiaryBranch = (props.driverVersion) & 0x003f;
			return StringFromFormat("%d.%d.%d.%d (%08x)", major, minor, secondaryBranch, tertiaryBranch, props.driverVersion);
		} else {
			// Standard scheme, use the standard macros.
			uint32_t major = VK_VERSION_MAJOR(props.driverVersion);
			uint32_t minor = VK_VERSION_MINOR(props.driverVersion);
			uint32_t branch = VK_VERSION_PATCH(props.driverVersion);
			return StringFromFormat("%d.%d.%d (%08x)", major, minor, branch, props.driverVersion);
		}
	}

	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case APINAME: return "Vulkan";
		case VENDORSTRING: return vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).deviceName;
		case VENDOR: return VulkanVendorString(vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).vendorID);
		case DRIVER: return FormatDriverVersion(vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()));
		case SHADELANGVERSION: return "N/A";;
		case APIVERSION: 
		{
			uint32_t ver = vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).apiVersion;
			return StringFromFormat("%d.%d.%d", ver >> 22, (ver >> 12) & 0x3ff, ver & 0xfff);
		}
		default: return "?";
		}
	}

	VkDescriptorSet GetOrCreateDescriptorSet(VkBuffer buffer);

	std::vector<std::string> GetFeatureList() const override;
	std::vector<std::string> GetExtensionList() const override;

	uintptr_t GetNativeObject(NativeObject obj) override {
		switch (obj) {
		case NativeObject::FRAMEBUFFER_RENDERPASS:
			// Return a representative renderpass.
			return (uintptr_t)renderManager_.GetFramebufferRenderPass();
		case NativeObject::BACKBUFFER_RENDERPASS:
			return (uintptr_t)renderManager_.GetBackbufferRenderPass();
		case NativeObject::COMPATIBLE_RENDERPASS:
			return (uintptr_t)renderManager_.GetCompatibleRenderPass();
		case NativeObject::INIT_COMMANDBUFFER:
			return (uintptr_t)renderManager_.GetInitCmd();
		case NativeObject::BOUND_TEXTURE0_IMAGEVIEW:
			return (uintptr_t)boundImageView_[0];
		case NativeObject::BOUND_TEXTURE1_IMAGEVIEW:
			return (uintptr_t)boundImageView_[1];
		case NativeObject::RENDER_MANAGER:
			return (uintptr_t)&renderManager_;
		case NativeObject::NULL_IMAGEVIEW:
			return (uintptr_t)GetNullTexture()->GetImageView();
		default:
			Crash();
			return 0;
		}
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

private:
	VulkanTexture *GetNullTexture();
	VulkanContext *vulkan_ = nullptr;

	VulkanRenderManager renderManager_;

	VulkanDeviceAllocator *allocator_ = nullptr;

	VulkanTexture *nullTexture_ = nullptr;

	VKPipeline *curPipeline_ = nullptr;
	VKBuffer *curVBuffers_[4]{};
	int curVBufferOffsets_[4]{};
	VKBuffer *curIBuffer_ = nullptr;
	int curIBufferOffset_ = 0;

	VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
	VKFramebuffer *curFramebuffer_ = nullptr;

	VkDevice device_;
	VkQueue queue_;
	int queueFamilyIndex_;

	enum {
		MAX_BOUND_TEXTURES = 2,
		MAX_FRAME_COMMAND_BUFFERS = 256,
	};
	VKTexture *boundTextures_[MAX_BOUND_TEXTURES]{};
	VKSamplerState *boundSamplers_[MAX_BOUND_TEXTURES]{};
	VkImageView boundImageView_[MAX_BOUND_TEXTURES]{};

	struct FrameData {
		VulkanPushBuffer *pushBuffer;
		// Per-frame descriptor set cache. As it's per frame and reset every frame, we don't need to
		// worry about invalidating descriptors pointing to deleted textures.
		// However! ARM is not a fan of doing it this way.
		std::map<DescriptorSetKey, VkDescriptorSet> descSets_;
		VkDescriptorPool descriptorPool;
	};

	FrameData frame_[VulkanContext::MAX_INFLIGHT_FRAMES]{};

	VulkanPushBuffer *push_ = nullptr;

	DeviceCaps caps_{};
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

static inline VkSamplerAddressMode AddressModeToVulkan(Draw::TextureAddressMode mode) {
	switch (mode) {
	case TextureAddressMode::CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case TextureAddressMode::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case TextureAddressMode::REPEAT_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	default:
	case TextureAddressMode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VulkanTexture *VKContext::GetNullTexture() {
	if (!nullTexture_) {
		VkCommandBuffer cmdInit = renderManager_.GetInitCmd();
		nullTexture_ = new VulkanTexture(vulkan_, allocator_);
		nullTexture_->SetTag("Null");
		int w = 8;
		int h = 8;
		nullTexture_->CreateDirect(cmdInit, w, h, 1, VK_FORMAT_A8B8G8R8_UNORM_PACK32, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		uint32_t bindOffset;
		VkBuffer bindBuf;
		uint32_t *data = (uint32_t *)push_->Push(w * h * 4, &bindOffset, &bindBuf);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				// data[y*w + x] = ((x ^ y) & 1) ? 0xFF808080 : 0xFF000000;   // gray/black checkerboard
				data[y*w + x] = 0;  // black
			}
		}
		nullTexture_->UploadMip(cmdInit, 0, w, h, bindBuf, bindOffset, w);
		nullTexture_->EndCreate(cmdInit);
	} else {
		nullTexture_->Touch();
	}
	return nullTexture_;
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

bool VKTexture::Create(VkCommandBuffer cmd, VulkanPushBuffer *push, const TextureDesc &desc, VulkanDeviceAllocator *alloc) {
	// Zero-sized textures not allowed.
	_assert_(desc.width * desc.height * desc.depth > 0);  // remember to set depth to 1!
	_assert_(push);
	format_ = desc.format;
	mipLevels_ = desc.mipLevels;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	vkTex_ = new VulkanTexture(vulkan_, alloc);
	vkTex_->SetTag(desc.tag);
	VkFormat vulkanFormat = DataFormatToVulkan(format_);
	int stride = desc.width * (int)DataFormatSizeInBytes(format_);
	int bpp = GetBpp(vulkanFormat);
	int bytesPerPixel = bpp / 8;
	int usageBits = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	if (mipLevels_ > (int)desc.initData.size()) {
		// Gonna have to generate some, which requires TRANSFER_SRC
		usageBits |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (!vkTex_->CreateDirect(cmd, width_, height_, mipLevels_, vulkanFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, usageBits)) {
		ELOG("Failed to create VulkanTexture: %dx%dx%d fmt %d, %d levels", width_, height_, depth_, (int)vulkanFormat, mipLevels_);
		return false;
	}
	if (desc.initData.size()) {
		int w = width_;
		int h = height_;
		int i;
		for (i = 0; i < (int)desc.initData.size(); i++) {
			uint32_t offset;
			VkBuffer buf;
			size_t size = w * h * bytesPerPixel;
			offset = push->PushAligned((const void *)desc.initData[i], size, 16, &buf);
			vkTex_->UploadMip(cmd, i, w, h, buf, offset, w);
			w = (w + 1) / 2;
			h = (h + 1) / 2;
		}
		// Generate the rest of the mips automatically.
		for (; i < mipLevels_; i++) {
			vkTex_->GenerateMip(cmd, i);
		}
	}
	vkTex_->EndCreate(cmd, false);
	return true;
}

VKContext::VKContext(VulkanContext *vulkan, bool splitSubmit)
	: vulkan_(vulkan), caps_{}, renderManager_(vulkan) {
	caps_.anisoSupported = vulkan->GetFeaturesAvailable().samplerAnisotropy != 0;
	caps_.geometryShaderSupported = vulkan->GetFeaturesAvailable().geometryShader != 0;
	caps_.tesselationShaderSupported = vulkan->GetFeaturesAvailable().tessellationShader != 0;
	caps_.multiViewport = vulkan->GetFeaturesAvailable().multiViewport != 0;
	caps_.dualSourceBlend = vulkan->GetFeaturesAvailable().dualSrcBlend != 0;
	caps_.framebufferBlitSupported = true;
	caps_.framebufferCopySupported = true;
	caps_.framebufferDepthBlitSupported = false;  // Can be checked for.
	caps_.framebufferDepthCopySupported = true;   // Will pretty much always be the case.
	caps_.preferredDepthBufferFormat = DataFormat::D24_S8;  // TODO: Ask vulkan.

	switch (vulkan->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).vendorID) {
	case VULKAN_VENDOR_AMD: caps_.vendor = GPUVendor::VENDOR_AMD; break;
	case VULKAN_VENDOR_ARM: caps_.vendor = GPUVendor::VENDOR_ARM; break;
	case VULKAN_VENDOR_IMGTEC: caps_.vendor = GPUVendor::VENDOR_IMGTEC; break;
	case VULKAN_VENDOR_NVIDIA: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
	case VULKAN_VENDOR_QUALCOMM: caps_.vendor = GPUVendor::VENDOR_QUALCOMM; break;
	case VULKAN_VENDOR_INTEL: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
	default:
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
	}

	device_ = vulkan->GetDevice();

	queue_ = vulkan->GetGraphicsQueue();
	queueFamilyIndex_ = vulkan->GetGraphicsQueueFamilyIndex();

	VkDescriptorPoolSize dpTypes[2];
	dpTypes[0].descriptorCount = 200;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	dpTypes[1].descriptorCount = 200;
	dpTypes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dp{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dp.flags = 0;   // Don't want to mess around with individually freeing these, let's go dynamic each frame.
	dp.maxSets = 200;  // 200 textures per frame should be enough for the UI...
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);

	VkCommandPoolCreateInfo p{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	p.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	p.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();

	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frame_[i].pushBuffer = new VulkanPushBuffer(vulkan_, 1024 * 1024);
		VkResult res = vkCreateDescriptorPool(device_, &dp, nullptr, &frame_[i].descriptorPool);
		assert(res == VK_SUCCESS);
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

	VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &pipelineCache_);
	assert(VK_SUCCESS == res);

	renderManager_.SetSplitSubmit(splitSubmit);

	allocator_ = new VulkanDeviceAllocator(vulkan_, 256 * 1024, 2048 * 1024);
}

VKContext::~VKContext() {
	delete nullTexture_;
	allocator_->Destroy();
	// We have to delete on queue, so this can free its queued deletions.
	vulkan_->Delete().QueueCallback([](void *ptr) {
		auto allocator = static_cast<VulkanDeviceAllocator *>(ptr);
		delete allocator;
	}, allocator_);
	allocator_ = nullptr;
	// This also destroys all descriptor sets.
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frame_[i].descSets_.clear();
		vulkan_->Delete().QueueDeleteDescriptorPool(frame_[i].descriptorPool);
		frame_[i].pushBuffer->Destroy(vulkan_);
		delete frame_[i].pushBuffer;
	}
	vulkan_->Delete().QueueDeleteDescriptorSetLayout(descriptorSetLayout_);
	vulkan_->Delete().QueueDeletePipelineLayout(pipelineLayout_);
	vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
}

void VKContext::BeginFrame() {
	renderManager_.BeginFrame();

	FrameData &frame = frame_[vulkan_->GetCurFrame()];
	push_ = frame.pushBuffer;

	// OK, we now know that nothing is reading from this frame's data pushbuffer,
	push_->Reset();
	push_->Begin(vulkan_);
	allocator_->Begin();

	frame.descSets_.clear();
	VkResult result = vkResetDescriptorPool(device_, frame.descriptorPool, 0);
	assert(result == VK_SUCCESS);
}

void VKContext::WaitRenderCompletion(Framebuffer *fbo) {
	// TODO
}

void VKContext::EndFrame() {
	// Stop collecting data in the frame's data pushbuffer.
	push_->End();
	allocator_->End();

	renderManager_.Finish();

	push_ = nullptr;
}

void VKContext::WipeQueue() {
	renderManager_.Wipe();
}

VkDescriptorSet VKContext::GetOrCreateDescriptorSet(VkBuffer buf) {
	DescriptorSetKey key;

	FrameData *frame = &frame_[vulkan_->GetCurFrame()];

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
	imageDesc.imageView = boundTextures_[0] ? boundTextures_[0]->GetImageView() : VK_NULL_HANDLE;
	imageDesc.sampler = boundSamplers_[0] ? boundSamplers_[0]->GetSampler() : VK_NULL_HANDLE;
#ifdef VULKAN_USE_GENERAL_LAYOUT_FOR_COLOR
	imageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
#else
	imageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif

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
	info.renderPass = renderManager_.GetBackbufferRenderPass();

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
	VkRect2D scissor{ {left, top}, {(uint32_t)width, (uint32_t)height} };
	renderManager_.SetScissor(scissor);
}

void VKContext::SetViewports(int count, Viewport *viewports) {
	if (count > 0) {
		VkViewport viewport;
		viewport.x = viewports[0].TopLeftX;
		viewport.y = viewports[0].TopLeftY;
		viewport.width = viewports[0].Width;
		viewport.height = viewports[0].Height;
		viewport.minDepth = viewports[0].MinDepth;
		viewport.maxDepth = viewports[0].MaxDepth;
		renderManager_.SetViewport(viewport);
	}
}

void VKContext::SetBlendFactor(float color[4]) {
	renderManager_.SetBlendFactor(color);
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
	if (!push_) {
		// Too early! Fail.
		ELOG("Can't create textures before the first frame has started.");
		return nullptr;
	}
	_assert_(renderManager_.GetInitCmd());
	return new VKTexture(vulkan_, renderManager_.GetInitCmd(), push_, desc, allocator_);
}

static inline void CopySide(VkStencilOpState &dest, const StencilSide &src) {
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
		boundImageView_[i] = boundTextures_[i] ? boundTextures_[i]->GetImageView() : GetNullTexture()->GetImageView();
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

void VKContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	curPipeline_->SetDynamicUniformData(ub, size);
}

void VKContext::Draw(int vertexCount, int offset) {
	VKBuffer *vbuf = curVBuffers_[0];

	VkBuffer vulkanVbuf;
	VkBuffer vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);

	renderManager_.BindPipeline(curPipeline_->vkpipeline);
	// TODO: blend constants, stencil, viewports should be here, after bindpipeline..
	renderManager_.Draw(pipelineLayout_, descSet, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vertexCount);
}

void VKContext::DrawIndexed(int vertexCount, int offset) {
	VKBuffer *ibuf = curIBuffer_;
	VKBuffer *vbuf = curVBuffers_[0];

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);
	size_t ibBindOffset = push_->Push(ibuf->GetData(), ibuf->GetSize(), &vulkanIbuf);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);

	renderManager_.BindPipeline(curPipeline_->vkpipeline);
	// TODO: blend constants, stencil, viewports should be here, after bindpipeline..
	renderManager_.DrawIndexed(pipelineLayout_, descSet, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vulkanIbuf, (int)ibBindOffset, vertexCount, 1, VK_INDEX_TYPE_UINT32);
}

void VKContext::DrawUP(const void *vdata, int vertexCount) {
	VkBuffer vulkanVbuf, vulkanUBObuf;
	size_t vbBindOffset = push_->Push(vdata, vertexCount * curPipeline_->stride[0], &vulkanVbuf);
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);

	renderManager_.BindPipeline(curPipeline_->vkpipeline);
	// TODO: blend constants, stencil, viewports should be here, after bindpipeline..
	renderManager_.Draw(pipelineLayout_, descSet, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vertexCount);
}

void VKContext::Clear(int clearMask, uint32_t colorval, float depthVal, int stencilVal) {
	int mask = 0;
	if (clearMask & FBChannel::FB_COLOR_BIT)
		mask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (clearMask & FBChannel::FB_DEPTH_BIT)
		mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (clearMask & FBChannel::FB_STENCIL_BIT)
		mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	renderManager_.Clear(colorval, depthVal, stencilVal, mask);
}

DrawContext *T3DCreateVulkanContext(VulkanContext *vulkan, bool split) {
	return new VKContext(vulkan, split);
}

void AddFeature(std::vector<std::string> &features, const char *name, VkBool32 available, VkBool32 enabled) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s: Available: %d Enabled: %d", name, (int)available, (int)enabled);
	features.push_back(buf);
}

// Limited to depth buffer formats as that's what we need right now.
static const char *VulkanFormatToString(VkFormat fmt) {
	switch (fmt) {
	case VkFormat::VK_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
	case VkFormat::VK_FORMAT_D16_UNORM: return "D16";
	case VkFormat::VK_FORMAT_D16_UNORM_S8_UINT: return "D16S8";
	case VkFormat::VK_FORMAT_D32_SFLOAT: return "D32f";
	case VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32fS8";
	case VkFormat::VK_FORMAT_S8_UINT: return "S8";
	case VkFormat::VK_FORMAT_UNDEFINED: return "UNDEFINED (BAD!)";
	default: return "UNKNOWN";
	}
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

	features.push_back(std::string("Preferred depth buffer format: ") + VulkanFormatToString(vulkan_->GetDeviceInfo().preferredDepthStencilFormat));

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

// A VKFramebuffer is a VkFramebuffer (note caps difference) plus all the textures it owns.
// It also has a reference to the command buffer that it was last rendered to with.
// If it needs to be transitioned, and the frame number matches, use it, otherwise
// use this frame's init command buffer.
class VKFramebuffer : public Framebuffer {
public:
	VKFramebuffer(VKRFramebuffer *fb) : buf_(fb) { _assert_msg_(G3D, fb, "Null fb in VKFramebuffer constructor"); }
	~VKFramebuffer() {
		_assert_msg_(G3D, buf_, "Null buf_ in VKFramebuffer - double delete?");
		buf_->vulkan_->Delete().QueueCallback([](void *fb) {
			VKRFramebuffer *vfb = static_cast<VKRFramebuffer *>(fb);
			delete vfb;
		}, buf_);
		buf_ = nullptr;
	}
	VKRFramebuffer *GetFB() const { return buf_; }
private:
	VKRFramebuffer *buf_;
};

Framebuffer *VKContext::CreateFramebuffer(const FramebufferDesc &desc) {
	VkCommandBuffer cmd = renderManager_.GetInitCmd();
	VKRFramebuffer *vkrfb = new VKRFramebuffer(vulkan_, cmd, renderManager_.GetFramebufferRenderPass(), desc.width, desc.height);
	return new VKFramebuffer(vkrfb);
}

void VKContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	int aspectMask = 0;
	if (channelBits & FBChannel::FB_COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (channelBits & FBChannel::FB_DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (channelBits & FBChannel::FB_STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	renderManager_.CopyFramebuffer(src->GetFB(), VkRect2D{ {x, y}, {(uint32_t)width, (uint32_t)height } }, dst->GetFB(), VkOffset2D{ dstX, dstY }, aspectMask);
}

bool VKContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	int aspectMask = 0;
	if (channelBits & FBChannel::FB_COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (channelBits & FBChannel::FB_DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (channelBits & FBChannel::FB_STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	renderManager_.BlitFramebuffer(src->GetFB(), VkRect2D{ {srcX1, srcY1}, {(uint32_t)(srcX2 - srcX1), (uint32_t)(srcY2 - srcY1) } }, dst->GetFB(), VkRect2D{ {dstX1, dstY1}, {(uint32_t)(dstX2 - dstX1), (uint32_t)(dstY2 - dstY1) } }, aspectMask, filter == FB_BLIT_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
	return true;
}

bool VKContext::CopyFramebufferToMemorySync(Framebuffer *srcfb, int channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;

	int aspectMask = 0;
	if (channelBits & FBChannel::FB_COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (channelBits & FBChannel::FB_DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (channelBits & FBChannel::FB_STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	return renderManager_.CopyFramebufferToMemorySync(src ? src->GetFB() : nullptr, aspectMask, x, y, w, h, format, (uint8_t *)pixels, pixelStride);
}

void VKContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	VKRRenderPassAction color = (VKRRenderPassAction)rp.color;
	VKRRenderPassAction depth = (VKRRenderPassAction)rp.depth;
	VKRRenderPassAction stencil = (VKRRenderPassAction)rp.stencil;

	renderManager_.BindFramebufferAsRenderTarget(fb ? fb->GetFB() : nullptr, color, depth, stencil, rp.clearColor, rp.clearDepth, rp.clearStencil);
	curFramebuffer_ = fb;
}

// color must be 0, for now.
void VKContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;

	if (fb == curFramebuffer_) {
		Crash();
	}

	int aspect = 0;
	if (channelBit & FBChannel::FB_COLOR_BIT) aspect |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (channelBit & FBChannel::FB_DEPTH_BIT) aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (channelBit & FBChannel::FB_STENCIL_BIT) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

	boundImageView_[binding] = renderManager_.BindFramebufferAsTexture(fb->GetFB(), binding, aspect, attachment);
}

uintptr_t VKContext::GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) {
	if (!fbo)
		return 0;

	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	VkImageView view = VK_NULL_HANDLE;
	switch (channelBit) {
	case FB_COLOR_BIT:
		view = fb->GetFB()->color.imageView;
		break;
	case FB_DEPTH_BIT:
	case FB_STENCIL_BIT:
		view = fb->GetFB()->depth.imageView;
		break;
	}
	return (uintptr_t)view;
}

void VKContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	if (fb) {
		*w = fb->GetFB()->width;
		*h = fb->GetFB()->height;
	} else {
		*w = vulkan_->GetBackbufferWidth();
		*h = vulkan_->GetBackbufferHeight();
	}
}

void VKContext::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	switch (ev) {
	case Event::LOST_BACKBUFFER:
		renderManager_.DestroyBackbuffers();
		break;
	case Event::GOT_BACKBUFFER:
		renderManager_.CreateBackbuffers();
		break;
	default:
		assert(false);
		break;
	}
}

}  // namespace Draw
