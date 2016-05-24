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

#include <stdio.h>
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
static const VkBlendOp blendEqToGL[] = {
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
};

static const VkBlendFactor blendFactorToVk[] = {
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
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
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
};

static inline void Uint8x4ToFloat4(uint32_t u, float f[4]) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
}


class Thin3DVKBlendState : public Thin3DBlendState {
public:
	bool blendEnabled;
	VkBlendOp eqCol, eqAlpha;
	VkBlendFactor srcCol, srcAlpha, dstColor, dstAlpha;
	bool logicEnabled;
	VkLogicOp logicOp;

	void ToVulkan(VkPipelineColorBlendStateCreateInfo *info, VkPipelineColorBlendAttachmentState *attachments) {
		memset(info, 0, sizeof(*info));
		info->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		info->attachmentCount = 1;
		info->logicOp = logicOp;
		info->logicOpEnable = logicEnabled;
		attachments[0].blendEnable = blendEnabled;
		attachments[0].colorBlendOp = eqCol;
		attachments[0].alphaBlendOp = eqAlpha;
		attachments[0].colorWriteMask = 0xF;
		attachments[0].dstAlphaBlendFactor = dstAlpha;
		attachments[0].dstColorBlendFactor = dstColor;
		attachments[0].srcAlphaBlendFactor = srcAlpha;
		attachments[0].srcColorBlendFactor = srcCol;
		info->pAttachments = attachments;
	}
};

class Thin3DVKDepthStencilState : public Thin3DDepthStencilState {
public:
	bool depthTestEnabled;
	bool depthWriteEnabled;
	VkCompareOp depthComp;

	void ToVulkan(VkPipelineDepthStencilStateCreateInfo *info) {
		memset(info, 0, sizeof(*info));
		info->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		info->depthCompareOp = depthComp;
		info->depthTestEnable = depthTestEnabled;
		info->depthWriteEnable = depthWriteEnabled;
		info->stencilTestEnable = false;
		info->depthBoundsTestEnable = false;
	}
};

// Very simplistic buffer that will simply copy its contents into our "pushbuffer" when it's time to draw,
// to avoid synchronization issues.
class Thin3DVKBuffer : public Thin3DBuffer {
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

// Not registering this as a resource holder, instead ShaderSet is registered. It will
// invoke Compile again to recreate the shader then link them together.
class Thin3DVKShader : public Thin3DShader {
public:
	Thin3DVKShader(bool isFragmentShader) : module_(VK_NULL_HANDLE), ok_(false) {
		stage_ = isFragmentShader ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
	}
	bool Compile(VulkanContext *vulkan, const char *source);
	const std::string &GetSource() const { return source_; }
	~Thin3DVKShader() {
		if (module_) {
			vkDestroyShaderModule(device_, module_, nullptr);
		}
	}
	VkShaderModule Get() const { return module_; }

private:
	VkDevice device_;
	VkShaderModule module_;
	VkShaderStageFlagBits stage_;
	bool ok_;
	std::string source_;  // So we can recompile in case of context loss.
};

bool Thin3DVKShader::Compile(VulkanContext *vulkan, const char *source) {
	// We'll need this to free it later.
	device_ = vulkan->GetDevice();
	this->source_ = source;
	std::vector<uint32_t> spirv;
	if (!GLSLtoSPV(stage_, source, spirv)) {
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


inline VkFormat ConvertVertexDataTypeToVk(T3DVertexDataType type) {
	switch (type) {
	case FLOATx2: return VK_FORMAT_R32G32_SFLOAT;
	case FLOATx3: return VK_FORMAT_R32G32B32_SFLOAT;
	case FLOATx4: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case UNORM8x4: return VK_FORMAT_R8G8B8A8_UNORM;
	default: return VK_FORMAT_UNDEFINED;
	}
}

class Thin3DVKVertexFormat : public Thin3DVertexFormat {
public:
	void ToVulkan(VkPipelineVertexInputStateCreateInfo *info, VkVertexInputAttributeDescription *attrDescs, VkVertexInputBindingDescription *bindDescs) {
		memset(info, 0, sizeof(*info));
		info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		for (uint32_t i = 0; i < components_.size(); i++) {
			attrDescs[i].binding = 0;
			attrDescs[i].format = ConvertVertexDataTypeToVk(components_[i].type);
			attrDescs[i].location = (int)components_[i].semantic;
			attrDescs[i].offset = components_[i].offset;
		}
		bindDescs[0].binding = 0;
		bindDescs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		bindDescs[0].stride = stride_;

		info->vertexAttributeDescriptionCount = (uint32_t)components_.size();
		info->pVertexAttributeDescriptions = attrDescs;
		info->vertexBindingDescriptionCount = 1;
		info->pVertexBindingDescriptions = bindDescs;
		info->flags = 0;
	}

	bool RequiresBuffer() {
		return false;
	}

	std::vector<Thin3DVertexComponent> components_;
	int stride_;
};

class Thin3DVKShaderSet : public Thin3DShaderSet {
public:
	Thin3DVKShaderSet() {
		// HACK! Hardcoded
		uboSize_ = 16 * sizeof(float);  // WorldViewProj
		ubo_ = new uint8_t[uboSize_];
	}
	~Thin3DVKShaderSet() {
		vshader->Release();
		fshader->Release();
		delete[] ubo_;
	}
	bool Link();

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

	Thin3DVKShader *vshader;
	Thin3DVKShader *fshader;

private:
	uint8_t *ubo_;
	int uboSize_;
};

struct PipelineKey {
	Thin3DVKDepthStencilState *depthStencil;
	Thin3DVKBlendState *blend;
	Thin3DVKShaderSet *shaderSet;
	VkPrimitiveTopology topology;
	T3DCullMode cullMode;

	// etc etc

	bool operator < (const PipelineKey &other) const {
		if (depthStencil < other.depthStencil) return true; else if (depthStencil > other.depthStencil) return false;
		if (blend < other.blend) return true; else if (blend > other.blend) return false;
		if (shaderSet < other.shaderSet) return true; else if (shaderSet > other.shaderSet) return false;
		if (topology < other.topology) return true; else if (topology > other.topology) return false;
		if (cullMode < other.cullMode) return true; else if (cullMode > other.cullMode) return false;
		// etc etc
		return false;
	}
};



class Thin3DVKTexture;
class Thin3DVKSamplerState;

struct DescriptorSetKey {
	Thin3DVKTexture *texture_;
	Thin3DVKSamplerState *sampler_;
	VkBuffer buffer_;

	bool operator < (const DescriptorSetKey &other) const {
		if (texture_ < other.texture_) return true; else if (texture_ > other.texture_) return false;
		if (sampler_ < other.sampler_) return true; else if (sampler_ > other.sampler_) return false;
		if (buffer_ < other.buffer_) return true; else if (buffer_ > other.buffer_) return false;
		return false;
	}
};

class Thin3DVKContext : public Thin3DContext {
public:
	Thin3DVKContext(VulkanContext *vulkan);
	virtual ~Thin3DVKContext();

	Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare) override;
	Thin3DBlendState *CreateBlendState(const T3DBlendStateDesc &desc) override;
	Thin3DBuffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Thin3DShaderSet *CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) override;
	Thin3DVertexFormat *CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride, Thin3DShader *vshader) override;
	Thin3DSamplerState *CreateSamplerState(const T3DSamplerStateDesc &desc) override;
	Thin3DTexture *CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) override;
	Thin3DTexture *CreateTexture() override;

	// Bound state objects
	void SetBlendState(Thin3DBlendState *state) override {
		Thin3DVKBlendState *s = static_cast<Thin3DVKBlendState *>(state);
		curBlendState_ = s;
	}

	// Bound state objects
	void SetDepthStencilState(Thin3DDepthStencilState *state) override {
		Thin3DVKDepthStencilState *s = static_cast<Thin3DVKDepthStencilState *>(state);
		curDepthStencilState_ = s;
	}

	// The implementation makes the choice of which shader code to use.
	Thin3DShader *CreateVertexShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;
	Thin3DShader *CreateFragmentShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	void SetScissorEnabled(bool enable) override {
		scissorEnabled_ = enable;
		scissorDirty_ = true;
	}

	void SetScissorRect(int left, int top, int width, int height) override;

	void SetViewports(int count, T3DViewport *viewports) override;

	void SetTextures(int start, int count, Thin3DTexture **textures) override;

	void SetSamplerStates(int start, int count, Thin3DSamplerState **state) override;

	void SetRenderState(T3DRenderState rs, uint32_t value) override;

	// TODO: Add more sophisticated draws.
	void Draw(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) override;
	void DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) override;

	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	void Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) override;
	void End() override;

	std::string GetInfoString(T3DInfo info) const override {
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

	VkPipeline GetOrCreatePipeline();
	VkDescriptorSet GetOrCreateDescriptorSet(VkBuffer buffer);

	std::vector<std::string> GetFeatureList() override;

private:
	void ApplyDynamicState();
	void DirtyDynamicState();

	VulkanContext *vulkan_;

	// These are used to compose the pipeline cache key.
	Thin3DVKBlendState *curBlendState_;
	Thin3DVKDepthStencilState *curDepthStencilState_;
	Thin3DVKShaderSet *curShaderSet_;
	VkPrimitiveTopology curPrim_;
	Thin3DVKVertexFormat *curVertexFormat_;
	T3DCullMode curCullMode_;

	// We keep a pipeline state cache.
	std::map<PipelineKey, VkPipeline> pipelines_;

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
	bool scissorEnabled_;

	VkRect2D noScissor_;  // Simply a scissor covering the screen.

	enum {MAX_BOUND_TEXTURES = 1};
	Thin3DVKTexture *boundTextures_[MAX_BOUND_TEXTURES];
	Thin3DVKSamplerState *boundSamplers_[MAX_BOUND_TEXTURES];

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
};

VkFormat FormatToVulkan(T3DImageFormat fmt, int *bpp) {
	switch (fmt) {
	case RGBA8888: *bpp = 32; return VK_FORMAT_R8G8B8A8_UNORM;
	case RGBA4444: *bpp = 16; return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
	case D24S8: *bpp = 32; return VK_FORMAT_D24_UNORM_S8_UINT;
	case D16: *bpp = 16; return VK_FORMAT_D16_UNORM;
	default: return VK_FORMAT_UNDEFINED;
	}
}

class Thin3DVKSamplerState : public Thin3DSamplerState {
public:
	Thin3DVKSamplerState(VulkanContext *vulkan, const T3DSamplerStateDesc &desc) : vulkan_(vulkan) {
		VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		s.addressModeU = desc.wrapS ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeV = desc.wrapT ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.magFilter = desc.magFilt == T3DTextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.minFilter = desc.minFilt == T3DTextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.mipmapMode = desc.mipFilt == T3DTextureFilter::LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.maxLod = 0.0;  // TODO: Actually support mipmaps

		VkResult res = vkCreateSampler(vulkan_->GetDevice(), &s, nullptr, &sampler_);
		assert(VK_SUCCESS == res);
	}
	~Thin3DVKSamplerState() {
		vkDestroySampler(vulkan_->GetDevice(), sampler_, nullptr);
	}

	VkSampler GetSampler() { return sampler_; }

private:
	VulkanContext *vulkan_;
	VkSampler sampler_;
};

Thin3DSamplerState *Thin3DVKContext::CreateSamplerState(const T3DSamplerStateDesc &desc) {
	return new Thin3DVKSamplerState(vulkan_, desc);
}

void Thin3DVKContext::SetSamplerStates(int start, int count, Thin3DSamplerState **state) {
	for (int i = start; i < start + count; i++) {
		boundSamplers_[i] = (Thin3DVKSamplerState *)state[i];
	}
}

enum class TextureState {
	UNINITIALIZED,
	STAGED,
	INITIALIZED,
	PENDING_DESTRUCTION,
};

class Thin3DVKTexture : public Thin3DTexture {
public:
	Thin3DVKTexture(VulkanContext *vulkan) : vulkan_(vulkan), vkTex_(nullptr) {
	}

	Thin3DVKTexture(VulkanContext *vulkan, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels)
		: vulkan_(vulkan), format_(format), mipLevels_(mipLevels) {
		Create(type, format, width, height, depth, mipLevels);
	}

	~Thin3DVKTexture() {
		Destroy();
	}

	bool Create(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) override {
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

	T3DImageFormat format_;
};

Thin3DVKContext::Thin3DVKContext(VulkanContext *vulkan)
	: viewportDirty_(false), scissorDirty_(false), vulkan_(vulkan), frameNum_(0) {
	device_ = vulkan->GetDevice();

	queue_ = vulkan->GetGraphicsQueue();
	queueFamilyIndex_ = vulkan->GetGraphicsQueueFamilyIndex();
	noScissor_.offset.x = 0;
	noScissor_.offset.y = 0;
	noScissor_.extent.width = pixel_xres;
	noScissor_.extent.height = pixel_yres;
	scissor_ = noScissor_;
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

Thin3DVKContext::~Thin3DVKContext() {
	for (auto x : pipelines_) {
		vkDestroyPipeline(device_, x.second, nullptr);
	}
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

void Thin3DVKContext::Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) {
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

	noScissor_.extent.width = pixel_xres;
	noScissor_.extent.height = pixel_yres;
	scissorDirty_ = true;
	viewportDirty_ = true;
}

void Thin3DVKContext::End() {
	// Stop collecting data in the frame's data pushbuffer.
	push_->End();
	vulkan_->EndSurfaceRenderPass();

	frameNum_++;
	cmd_ = nullptr;  // will be set on the next begin
	push_ = nullptr;

	DirtyDynamicState();
}

VkDescriptorSet Thin3DVKContext::GetOrCreateDescriptorSet(VkBuffer buf) {
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
	bufferDesc.range = curShaderSet_->GetUBOSize();

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

VkPipeline Thin3DVKContext::GetOrCreatePipeline() {
	PipelineKey key;
	key.blend = curBlendState_;
	key.depthStencil = curDepthStencilState_;
	key.shaderSet = curShaderSet_;
	key.topology = curPrim_;
	key.cullMode = curCullMode_;

	auto iter = pipelines_.find(key);
	if (iter != pipelines_.end()) {
		return iter->second;
	}

	VkPipelineShaderStageCreateInfo stages[2];
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].pNext = nullptr;
	stages[0].pSpecializationInfo = nullptr;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = curShaderSet_->vshader->Get();
	stages[0].pName = "main";
	stages[0].flags = 0;

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].pNext = nullptr;
	stages[1].pSpecializationInfo = nullptr;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = curShaderSet_->fshader->Get();
	stages[1].pName = "main";
	stages[1].flags = 0;

	VkPipelineColorBlendStateCreateInfo colorBlend;
	VkPipelineColorBlendAttachmentState attachment0;
	curBlendState_->ToVulkan(&colorBlend, &attachment0);

	VkPipelineDepthStencilStateCreateInfo depthStencil;
	curDepthStencilState_->ToVulkan(&depthStencil);

	VkPipelineVertexInputStateCreateInfo vertex;
	VkVertexInputAttributeDescription attrDescs[4];
	VkVertexInputBindingDescription bindDescs[1];
	curVertexFormat_->ToVulkan(&vertex, attrDescs, bindDescs);

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = curPrim_;
	inputAssembly.primitiveRestartEnable = false;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicInfo.dynamicStateCount = ARRAY_SIZE(dynamics);
	dynamicInfo.pDynamicStates = dynamics;

	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	switch (curCullMode_) {
	case NO_CULL: raster.cullMode = VK_CULL_MODE_NONE; break;
	case CW: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
	default:
	case CCW: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
	}
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.rasterizerDiscardEnable = false;
	raster.lineWidth = 1.0f;
	raster.depthBiasClamp = 0.0f;
	raster.depthBiasEnable = false;
	raster.depthClampEnable = false;
	raster.depthBiasSlopeFactor = 0.0;

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

	VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	info.pNext = nullptr;
	info.flags = 0;
	info.stageCount = 2;
	info.pStages = stages;
	info.pColorBlendState = &colorBlend;
	info.pDepthStencilState = &depthStencil;
	info.pDynamicState = &dynamicInfo;
	info.pInputAssemblyState = &inputAssembly;
	info.pTessellationState = nullptr;
	info.pMultisampleState = &ms;
	info.pVertexInputState = &vertex;
	info.pRasterizationState = &raster;
	info.pViewportState = &vs;  // Must set viewport and scissor counts even if we set the actual state dynamically.
	info.layout = pipelineLayout_;
	info.subpass = 0;
	info.renderPass = vulkan_->GetSurfaceRenderPass();

	// OK, need to create a new pipeline.
	VkPipeline pipeline;
	VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &pipeline);
	if (result != VK_SUCCESS) {
		ELOG("Failed to create graphics pipeline");
		return VK_NULL_HANDLE;
	}
	
	pipelines_.insert(std::make_pair(key, pipeline));
	return pipeline;
}

void Thin3DVKContext::SetScissorRect(int left, int top, int width, int height) {
	scissor_.offset.x = left;
	scissor_.offset.y = top;
	scissor_.extent.width = width;
	scissor_.extent.height = height;
	scissorDirty_ = true;
}

void Thin3DVKContext::SetViewports(int count, T3DViewport *viewports) {
	viewport_.x = viewports[0].TopLeftX;
	viewport_.y = viewports[0].TopLeftY;
	viewport_.width = viewports[0].Width;
	viewport_.height = viewports[0].Height;
	viewport_.minDepth = viewports[0].MinDepth;
	viewport_.maxDepth = viewports[0].MaxDepth;
	viewportDirty_ = true;
}

void Thin3DVKContext::ApplyDynamicState() {
	if (scissorDirty_) {
		if (scissorEnabled_) {
			vkCmdSetScissor(cmd_, 0, 1, &scissor_);
		} else {
			vkCmdSetScissor(cmd_, 0, 1, &noScissor_);
		}
		scissorDirty_ = false;
	}
	if (viewportDirty_) {
		vkCmdSetViewport(cmd_, 0, 1, &viewport_);
		viewportDirty_ = false;
	}
}

void Thin3DVKContext::DirtyDynamicState() {
	scissorDirty_ = true;
	viewportDirty_ = true;
}

Thin3DVertexFormat *Thin3DVKContext::CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride, Thin3DShader *vshader) {
	Thin3DVKVertexFormat *fmt = new Thin3DVKVertexFormat();
	fmt->components_ = components;
	fmt->stride_ = stride;
	return fmt;
}

Thin3DTexture *Thin3DVKContext::CreateTexture() {
	return new Thin3DVKTexture(vulkan_);
}

Thin3DTexture *Thin3DVKContext::CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) {
	return new Thin3DVKTexture(vulkan_, type, format, width, height, depth, mipLevels);
}

void Thin3DVKTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	int bpp;
	VkFormat vulkanFormat = FormatToVulkan(format_, &bpp);
	int bytesPerPixel = bpp / 8;
	vkTex_->Create(width, height, vulkanFormat);
	int rowPitch;
	uint8_t *dstData = vkTex_->Lock(0, &rowPitch);
	for (int y = 0; y < height; y++) {
		memcpy(dstData + rowPitch * y, data + stride * y, width * bytesPerPixel);
	}
	vkTex_->Unlock();
}

void Thin3DVKTexture::Finalize(int zim_flags) {
	// TODO
}

Thin3DDepthStencilState *Thin3DVKContext::CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare) {
	Thin3DVKDepthStencilState *ds = new Thin3DVKDepthStencilState();
	ds->depthTestEnabled = depthTestEnabled;
	ds->depthWriteEnabled = depthWriteEnabled;
	ds->depthComp = compToVK[depthCompare];
	return ds;
}

Thin3DBlendState *Thin3DVKContext::CreateBlendState(const T3DBlendStateDesc &desc) {
	Thin3DVKBlendState *bs = new Thin3DVKBlendState();
	bs->blendEnabled = desc.enabled;
	bs->eqCol = blendEqToGL[desc.eqCol];
	bs->srcCol = blendFactorToVk[desc.srcCol];
	bs->dstColor = blendFactorToVk[desc.dstCol];
	bs->eqAlpha = blendEqToGL[desc.eqAlpha];
	bs->srcAlpha = blendFactorToVk[desc.srcAlpha];
	bs->dstAlpha = blendFactorToVk[desc.dstAlpha];
	bs->logicEnabled = desc.logicEnabled;
	bs->logicOp = logicOpToVK[desc.logicOp];
	return bs;
}

Thin3DBuffer *Thin3DVKContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DVKBuffer(size, usageFlags);
}

Thin3DShaderSet *Thin3DVKContext::CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) {
	if (!vshader || !fshader) {
		ELOG("ShaderSet requires both a valid vertex and a fragment shader: %p %p", vshader, fshader);
		return NULL;
	}
	Thin3DVKShaderSet *shaderSet = new Thin3DVKShaderSet();
	vshader->AddRef();
	fshader->AddRef();
	shaderSet->vshader = static_cast<Thin3DVKShader *>(vshader);
	shaderSet->fshader = static_cast<Thin3DVKShader *>(fshader);
	if (shaderSet->Link()) {
		return shaderSet;
	} else {
		delete shaderSet;
		return NULL;
	}
}

void Thin3DVKContext::SetTextures(int start, int count, Thin3DTexture **textures) {
	for (int i = start; i < start + count; i++) {
		boundTextures_[i] = static_cast<Thin3DVKTexture *>(textures[i]);
	}
}

Thin3DShader *Thin3DVKContext::CreateVertexShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DVKShader *shader = new Thin3DVKShader(false);
	if (shader->Compile(vulkan_, vulkan_source)) {
		return shader;
	} else {
		ELOG("Failed to compile shader: %s", vulkan_source);
		shader->Release();
		return nullptr;
	}
}

Thin3DShader *Thin3DVKContext::CreateFragmentShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DVKShader *shader = new Thin3DVKShader(true);
	if (shader->Compile(vulkan_, vulkan_source)) {
		return shader;
	} else {
		ELOG("Failed to compile shader: %s", vulkan_source);
		shader->Release();
		return nullptr;
	}
}

bool Thin3DVKShaderSet::Link() {
	// There is no link step. However, we will create and cache Pipeline objects in the device context.
	return true;
}

int Thin3DVKShaderSet::GetUniformLoc(const char *name) {
	int loc = -1;

	// HACK! As we only use one uniform we hardcode it.
	if (!strcmp(name, "WorldViewProj")) {
		return 0;
	}

	return loc;
}

void Thin3DVKShaderSet::SetVector(const char *name, float *value, int n) {
	// TODO: Implement
}

void Thin3DVKShaderSet::SetMatrix4x4(const char *name, const float value[16]) {
	int loc = GetUniformLoc(name);
	if (loc != -1) {
		memcpy(ubo_ + loc, value, 16 * sizeof(float));
	}
}

void Thin3DVKContext::SetRenderState(T3DRenderState rs, uint32_t value) {
	switch (rs) {
	case T3DRenderState::CULL_MODE:
		curCullMode_ = (T3DCullMode)value;
		break;
	}
}

void Thin3DVKContext::Draw(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) {
	ApplyDynamicState();
	
	curPrim_ = primToVK[prim];
	curShaderSet_ = (Thin3DVKShaderSet *)shaderSet;
	curVertexFormat_ = (Thin3DVKVertexFormat *)format;
	Thin3DVKBuffer *vbuf = static_cast<Thin3DVKBuffer *>(vdata);

	VkBuffer vulkanVbuf;
	VkBuffer vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);
	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);
	vkCmdDraw(cmd_, vertexCount, 1, offset, 0);
}

void Thin3DVKContext::DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) {
	ApplyDynamicState();
	
	curPrim_ = primToVK[prim];
	curShaderSet_ = (Thin3DVKShaderSet *)shaderSet;
	curVertexFormat_ = (Thin3DVKVertexFormat *)format;

	Thin3DVKBuffer *ibuf = static_cast<Thin3DVKBuffer *>(idata);
	Thin3DVKBuffer *vbuf = static_cast<Thin3DVKBuffer *>(vdata);

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), &vulkanVbuf);
	size_t ibBindOffset = push_->Push(ibuf->GetData(), ibuf->GetSize(), &vulkanIbuf);

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);

	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);

	vkCmdBindIndexBuffer(cmd_, vulkanIbuf, ibBindOffset, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd_, vertexCount, 1, 0, offset, 0);
}

void Thin3DVKContext::DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) {
	ApplyDynamicState();

	curPrim_ = primToVK[prim];
	curShaderSet_ = (Thin3DVKShaderSet *)shaderSet;
	curVertexFormat_ = (Thin3DVKVertexFormat *)format;

	VkBuffer vulkanVbuf, vulkanUBObuf;
	size_t vbBindOffset = push_->Push(vdata, vertexCount * curVertexFormat_->stride_, &vulkanVbuf);
	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkBuffer buffers[1] = { vulkanVbuf };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet(vulkanUBObuf);
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);
	vkCmdDraw(cmd_, vertexCount, 1, 0, 0);
}

void Thin3DVKContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	if (mask & T3DClear::COLOR) {
		VkClearColorValue col;
		Uint8x4ToFloat4(colorval, col.float32);

		/*
		VkRect3D rect;
		rect.extent.width =
		vkCmdClearColorAttachment(cmdBuf_, 0, imageLayout_, &col, 1, nullptr);
		*/
	}
	if (mask & (T3DClear::DEPTH | T3DClear::STENCIL)) {

	}
}

Thin3DContext *T3DCreateVulkanContext(VulkanContext *vulkan) {
	return new Thin3DVKContext(vulkan);
}

void AddFeature(std::vector<std::string> &features, const char *name, VkBool32 available, VkBool32 enabled) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s: Available: %d Enabled: %d", name, (int)available, (int)enabled);
	features.push_back(buf);
}

std::vector<std::string> Thin3DVKContext::GetFeatureList() {
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
