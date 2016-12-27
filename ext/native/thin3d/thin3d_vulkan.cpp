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
	VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
	// These are for geometry shaders only.
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

// Very simplistic buffer that will simply copy its contents into our "pushbuffer" when it's time to draw,
// to avoid synchronization issues.
class Thin3DVKBuffer : public Buffer {
public:
	Thin3DVKBuffer(size_t size, uint32_t flags) : dataSize_(size) {
		data_ = new uint8_t[size];
	}
	~Thin3DVKBuffer() override {
		delete[] data_;
	}

	void SetData(const uint8_t *data, size_t size) override {
		delete[] data_;
		dataSize_ = size;
		data_ = new uint8_t[size];
		if (data) {
			memcpy(data_, data, size);
		}
	}

	void SubData(const uint8_t *data, size_t offset, size_t size) override {
		memcpy(data_, data_ + offset, size);
	}

	size_t GetSize() const { return dataSize_; }
	const uint8_t *GetData() const { return data_; }

private:
	uint8_t *data_;
	size_t dataSize_;
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
	bool Compile(VulkanContext *vulkan, const char *source);
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

bool VKShaderModule::Compile(VulkanContext *vulkan, const char *source) {
	// We'll need this to free it later.
	device_ = vulkan->GetDevice();
	this->source_ = source;
	std::vector<uint32_t> spirv;
	if (!GLSLtoSPV(vkstage_, source, spirv)) {
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
	VKPipeline() {
		// HACK! Hardcoded
		uboSize_ = 16 * sizeof(float);  // WorldViewProj
		ubo_ = new uint8_t[uboSize_];
	}
	~VKPipeline() {
		delete[] ubo_;
	}

	// Returns the binding offset, and the VkBuffer to bind.
	size_t PushUBO(VulkanPushBuffer *buf, VulkanContext *vulkan, VkBuffer *vkbuf) {
		return buf->PushAligned(ubo_, uboSize_, vulkan->GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment, vkbuf);
	}

	int GetUniformLoc(const char *name);

	void SetVector(const char *name, float *value, int n) override;
	void SetMatrix4x4(const char *name, const float value[16]) override;

	int GetUBOSize() const {
		return uboSize_;
	}
	bool RequiresBuffer() override {
		return false;
	}

	VkPipeline vkpipeline;
	int stride[4];

private:
	uint8_t *ubo_;
	int uboSize_;
};

class VKTexture;
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

class VKContext : public DrawContext {
public:
	VKContext(VulkanContext *vulkan);
	virtual ~VKContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	// The implementation makes the choice of which shader code to use.
	ShaderModule *CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	Texture *CreateTexture() override;

	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override;

	void BindSamplerStates(int start, int count, SamplerState **state) override;
	void BindTextures(int start, int count, Texture **textures) override;
	void BindPipeline(Pipeline *pipeline) override {
		curPipeline_ = (VKPipeline *)pipeline;
	}

	// TODO: Add more sophisticated draws.
	void Draw(Buffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;

	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	void Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) override;
	void End() override;

	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case APINAME: return "Vulkan";
		case VENDORSTRING: return vulkan_->GetPhysicalDeviceProperties().deviceName;
		case VENDOR: return StringFromFormat("%08x", vulkan_->GetPhysicalDeviceProperties().vendorID);
		case RENDERER: return StringFromFormat("%08x", vulkan_->GetPhysicalDeviceProperties().driverVersion);
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

private:
	void ApplyDynamicState();
	void DirtyDynamicState();

	VulkanContext *vulkan_;

	VKPipeline *curPipeline_;

	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;
	VkPipelineCache pipelineCache_;

	VkCommandPool cmdPool_;
	VkDevice device_;
	VkQueue queue_;
	int queueFamilyIndex_;

	// State to apply at the next draw call if viewportDirty or scissorDirty are true.
	bool viewportDirty_;
	VkViewport viewport_;
	bool scissorDirty_;
	VkRect2D scissor_;

	enum {MAX_BOUND_TEXTURES = 1};
	VKTexture *boundTextures_[MAX_BOUND_TEXTURES];
	VKSamplerState *boundSamplers_[MAX_BOUND_TEXTURES];

	VkCommandBuffer cmd_; // The current one

	struct FrameData {
		VulkanPushBuffer *pushBuffer;

		// Per-frame descriptor set cache. As it's per frame and reset every frame, we don't need to
		// worry about invalidating descriptors pointing to deleted textures.
		std::map<DescriptorSetKey, VkDescriptorSet> descSets_;
		VkDescriptorPool descriptorPool;
	};

	FrameData frame_[2];

	int frameNum_;
	VulkanPushBuffer *push_;

	DeviceCaps caps_;
};

int GetBpp(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		return 32;
	case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
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
	case DataFormat::R4G4_UNORM: return VK_FORMAT_R4G4_UNORM_PACK8;
	case DataFormat::R4G4B4A4_UNORM: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
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
		vkDestroySampler(vulkan_->GetDevice(), sampler_, nullptr);
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

class VKTexture : public Texture {
public:
	VKTexture(VulkanContext *vulkan) : vulkan_(vulkan), vkTex_(nullptr) {
	}

	VKTexture(VulkanContext *vulkan, TextureType type, DataFormat format, int width, int height, int depth, int mipLevels)
		: vulkan_(vulkan), format_(format), mipLevels_(mipLevels) {
		Create(type, format, width, height, depth, mipLevels);
	}

	~VKTexture() {
		Destroy();
	}

	bool Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override {
		format_ = format;
		mipLevels_ = mipLevels;
		width_ = width;
		height_ = height;
		depth_ = depth;
		vkTex_ = new VulkanTexture(vulkan_);
		// We don't actually do anything here.
		return true;
	}

	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;
	void Finalize(int zim_flags) override;
	void AutoGenMipmaps() override {}

	VkImageView GetImageView() { return vkTex_->GetImageView(); }

private:
	void Destroy() {
		if (vkTex_) {
			vkTex_->Destroy();
			delete vkTex_;
		}
	}

	VulkanContext *vulkan_;
	VulkanTexture *vkTex_;

	int mipLevels_;

	DataFormat format_;
};

VKContext::VKContext(VulkanContext *vulkan)
	: viewportDirty_(false), scissorDirty_(false), vulkan_(vulkan), frameNum_(0), caps_{} {
	caps_.anisoSupported = vulkan->GetFeaturesAvailable().samplerAnisotropy != 0;
	caps_.geometryShaderSupported = vulkan->GetFeaturesAvailable().geometryShader != 0;
	caps_.tesselationShaderSupported = vulkan->GetFeaturesAvailable().tessellationShader != 0;
	caps_.multiViewport = vulkan->GetFeaturesAvailable().multiViewport != 0;
	caps_.dualSourceBlend = vulkan->GetFeaturesAvailable().dualSrcBlend != 0;

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
	CreatePresets();

	VkCommandPoolCreateInfo p = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	p.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();
	VkResult res = vkCreateCommandPool(device_, &p, nullptr, &cmdPool_);
	assert(VK_SUCCESS == res);

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
	res = vkCreateDescriptorPool(device_, &dp, nullptr, &frame_[0].descriptorPool);
	assert(VK_SUCCESS == res);
	res = vkCreateDescriptorPool(device_, &dp, nullptr, &frame_[1].descriptorPool);
	assert(VK_SUCCESS == res);

	frame_[0].pushBuffer = new VulkanPushBuffer(vulkan_, 1024 * 1024);
	frame_[1].pushBuffer = new VulkanPushBuffer(vulkan_, 1024 * 1024);

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
	res = vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &descriptorSetLayout_);
	assert(VK_SUCCESS == res);

	VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pl.pPushConstantRanges = nullptr;
	pl.pushConstantRangeCount = 0;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	res = vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);

	pipelineCache_ = vulkan_->CreatePipelineCache();
}

VKContext::~VKContext() {
	vkDestroyCommandPool(device_, cmdPool_, nullptr);
	// This also destroys all descriptor sets.
	for (int i = 0; i < 2; i++) {
		frame_[i].descSets_.clear();
		vkDestroyDescriptorPool(device_, frame_[i].descriptorPool, nullptr);
		frame_[i].pushBuffer->Destroy(vulkan_);
		delete frame_[i].pushBuffer;
	}
	vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
	vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
	vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
}

void VKContext::Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) {
	VkClearValue clearVal[2] = {};
	Uint8x4ToFloat4(colorval, clearVal[0].color.float32);

	// // Debug flicker - used to see if we swap at all. no longer necessary
	// if (frameNum_ & 1)
	//	clearVal[0].color.float32[2] = 1.0f;

	clearVal[1].depthStencil.depth = depthVal;
	clearVal[1].depthStencil.stencil = stencilVal;

	cmd_ = vulkan_->BeginSurfaceRenderPass(clearVal);

	FrameData *frame = &frame_[frameNum_ & 1];
	push_ = frame->pushBuffer;

	// OK, we now know that nothing is reading from this frame's data pushbuffer,
	push_->Reset();
	push_->Begin(vulkan_);

	frame->descSets_.clear();
	VkResult result = vkResetDescriptorPool(device_, frame->descriptorPool, 0);
	assert(result == VK_SUCCESS);

	scissor_.extent.width = pixel_xres;
	scissor_.extent.height = pixel_yres;
	scissorDirty_ = true;
	viewportDirty_ = true;
}

void VKContext::End() {
	// Stop collecting data in the frame's data pushbuffer.
	push_->End();
	vulkan_->EndSurfaceRenderPass();

	frameNum_++;
	cmd_ = nullptr;  // will be set on the next begin
	push_ = nullptr;

	DirtyDynamicState();
}

VkDescriptorSet VKContext::GetOrCreateDescriptorSet(VkBuffer buf) {
	DescriptorSetKey key;

	FrameData *frame = &frame_[frameNum_ & 1];

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
	VKPipeline *pipeline = new VKPipeline();
	VKInputLayout *input = (VKInputLayout *)desc.inputLayout;
	VKBlendState *blend = (VKBlendState *)desc.blend;
	VKDepthStencilState *depth = (VKDepthStencilState *)desc.depthStencil;
	VKRasterState *raster = (VKRasterState *)desc.raster;
	for (int i = 0; i < input->bindings.size(); i++) {
		pipeline->stride[i] = input->bindings[i].stride;
	}

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	stages.resize(desc.shaders.size());
	int i = 0;
	for (auto &iter : desc.shaders) {
		VKShaderModule *vkshader = (VKShaderModule *)iter;
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
	viewport_.x = viewports[0].TopLeftX;
	viewport_.y = viewports[0].TopLeftY;
	viewport_.width = viewports[0].Width;
	viewport_.height = viewports[0].Height;
	viewport_.minDepth = viewports[0].MinDepth;
	viewport_.maxDepth = viewports[0].MaxDepth;
	viewportDirty_ = true;
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

Texture *VKContext::CreateTexture() {
	return new VKTexture(vulkan_);
}

Texture *VKContext::CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
	return new VKTexture(vulkan_, type, format, width, height, depth, mipLevels);
}

void VKTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	VkFormat vulkanFormat = DataFormatToVulkan(format_);
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

void VKTexture::Finalize(int zim_flags) {
	// TODO
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

Buffer *VKContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DVKBuffer(size, usageFlags);
}

void VKContext::BindTextures(int start, int count, Texture **textures) {
	for (int i = start; i < start + count; i++) {
		boundTextures_[i] = static_cast<VKTexture *>(textures[i]);
	}
}

ShaderModule *VKContext::CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	VKShaderModule *shader = new VKShaderModule(stage);
	if (shader->Compile(vulkan_, vulkan_source)) {
		return shader;
	} else {
		ELOG("Failed to compile shader: %s", vulkan_source);
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

void VKPipeline::SetVector(const char *name, float *value, int n) {
	// TODO: Implement
}

void VKPipeline::SetMatrix4x4(const char *name, const float value[16]) {
	int loc = GetUniformLoc(name);
	if (loc != -1) {
		memcpy(ubo_ + loc, value, 16 * sizeof(float));
	}
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

void VKContext::Draw(Buffer *vdata, int vertexCount, int offset) {
	ApplyDynamicState();
	
	Thin3DVKBuffer *vbuf = static_cast<Thin3DVKBuffer *>(vdata);

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

void VKContext::DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) {
	ApplyDynamicState();
	
	Thin3DVKBuffer *ibuf = static_cast<Thin3DVKBuffer *>(idata);
	Thin3DVKBuffer *vbuf = static_cast<Thin3DVKBuffer *>(vdata);

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

void VKContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	if (mask & ClearFlag::COLOR) {
		VkClearColorValue col;
		Uint8x4ToFloat4(colorval, col.float32);

		/*
		VkRect3D rect;
		rect.extent.width =
		vkCmdClearColorAttachment(cmdBuf_, 0, imageLayout_, &col, 1, nullptr);
		*/
	}
	if (mask & (ClearFlag::DEPTH | ClearFlag::STENCIL)) {

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

}  // namespace Draw