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
#include "image/zim_load.h"
#include "math/lin/matrix4x4.h"
#include "math/dataconv.h"
#include "thin3d/thin3d.h"
#include "thin3d/vulkan_utils.h"
#include "thin3d/VulkanContext.h"

// We use a simple descriptor set for all rendering: 1 sampler, 1 texture, 1 UBO binding point.
// binding 0 - vertex data
// binding 1 - uniform data
// binding 2 - sampler

#define VK_PROTOTYPES
#include "ext/vulkan/vulkan.h"

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


// Use these to push vertex, index and uniform data.
// TODO: Make this dynamically grow by chaining new buffers in the future.
// Until then, we cap at a maximum size.
// We'll have two of these that we alternate between on each frame.
// These will only be used for the "Thin3D" system - the PSP emulation etc will have
// their own similar buffer solutions.
class VulkanPushBuffer {
public:
	VulkanPushBuffer(VkDevice device, VulkanDeviceMemoryManager *memMan, size_t size) : offset_(0), size_(size), writePtr_(nullptr) {
		VkBufferCreateInfo b;
		b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		b.pNext = nullptr;
		b.size = size;
		b.flags = 0;
		b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		b.queueFamilyIndexCount = 0;
		b.pQueueFamilyIndices = nullptr;
		VkResult res = vkCreateBuffer(device, &b, nullptr, &buffer_);
		assert(VK_SUCCESS == res);

		// Okay, that's the buffer. Now let's allocate some memory for it.
		VkMemoryAllocateInfo alloc;
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.pNext = nullptr;
		memMan->memory_type_from_properties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &alloc.memoryTypeIndex);
		alloc.allocationSize = size;

		res = vkAllocateMemory(device, &alloc, nullptr, &deviceMemory_);
		assert(VK_SUCCESS == res);
		res = vkBindBufferMemory(device, buffer_, deviceMemory_, 0);
		assert(VK_SUCCESS == res);
	}

	void Destroy(VkDevice device) {
		vkDestroyBuffer(device, buffer_, nullptr);
		vkFreeMemory(device, deviceMemory_, nullptr);
	}

	void Reset() { offset_ = 0; }

	void Begin(VkDevice device) {
		offset_ = 0;
		VkResult res = vkMapMemory(device, deviceMemory_, 0, size_, 0, (void **)(&writePtr_));
		assert(VK_SUCCESS == res);
	}

	void End(VkDevice device) {
		vkUnmapMemory(device, deviceMemory_);
		writePtr_ = nullptr;
	}


	size_t Allocate(size_t numBytes) {
		size_t out = offset_;
		offset_ += (numBytes + 3) & ~3;  // Round up to 4 bytes.
		return out;
	}

	// TODO: Add alignment support?
	// Returns the offset that should be used when binding this buffer to get this data.
	size_t Push(const void *data, size_t size) {
		size_t off = Allocate(size);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	size_t PushAligned(const void *data, size_t size, int align) {
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	void *Push(size_t size, size_t *bindOffset) {
		size_t off = Allocate(size);
		*bindOffset = off;
		return writePtr_ + off;
	}

	VkBuffer GetVkBuffer() const { return buffer_; }

private:
	// TODO: Make it possible to suballocate pushbuffers in a large DeviceMemory block.
	VkDeviceMemory deviceMemory_;
	VkBuffer buffer_;
	size_t offset_;
	size_t size_;
	uint8_t *writePtr_;
};

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
	Thin3DVKShader(VulkanContext *vulkan, bool isFragmentShader) : vulkan_(vulkan), module_(nullptr), ok_(false) {
		stage_ = isFragmentShader ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
	}
	bool Compile(VkDevice device, const char *source);
	const std::string &GetSource() const { return source_; }
	~Thin3DVKShader() {
	}
	VkShaderModule Get() const { return module_; }

private:
	VulkanContext *vulkan_;
	VkShaderModule module_;
	VkShaderStageFlagBits stage_;
	bool ok_;
	std::string source_;  // So we can recompile in case of context loss.
};

bool Thin3DVKShader::Compile(VkDevice device, const char *source) {
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

	if (CreateShaderModule(device, spirv, &module_)) {
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
		int offset = 0;
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
	}
	bool Link();

	// Returns the binding offset.
	size_t PushUBO(VulkanPushBuffer *buf) {
		return buf->PushAligned(ubo_, uboSize_, 16);
	}

	int GetUniformLoc(const char *name);

	void SetVector(const char *name, float *value, int n) override;
	void SetMatrix4x4(const char *name, const Matrix4x4 &value) override;

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

struct DescriptorSetKey {
	Thin3DVKTexture *texture_;
	Thin3DVertexFormat *vertexFormat_;
	int frame;  // 0 or 1

	bool operator < (const DescriptorSetKey &other) const {
		if (texture_ < other.texture_) return true; else if (texture_ > other.texture_) return false;
		if (vertexFormat_ < other.vertexFormat_) return true; else if (vertexFormat_ > other.vertexFormat_) return false;
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

	void SetRenderState(T3DRenderState rs, uint32_t value) override;

	// TODO: Add more sophisticated draws.
	void Draw(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) override;
	void DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) override;

	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	virtual void Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal);
	virtual void End();

	std::string GetInfoString(T3DInfo info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case APINAME:
			return "Vulkan";
		case VENDORSTRING: return "N/A";
		case VENDOR: return "N/A";
		case RENDERER: return "N/A";
		case SHADELANGVERSION: return "N/A";;
		case APIVERSION: return "N/A";
		default: return "?";
		}
	}

	VkPipeline GetOrCreatePipeline();
	VkDescriptorSet GetOrCreateDescriptorSet();

private:
	void ApplyDynamicState();
	void DirtyDynamicState();

	void BeginInitCommands();
	void EndInitCommands();

	VulkanContext *vulkan_;

	VulkanDeviceMemoryManager vulkanMem_;

	// These are used to compose the pipeline cache key.
	Thin3DVKBlendState *curBlendState_;
	Thin3DVKDepthStencilState *curDepthStencilState_;
	Thin3DVKShaderSet *curShaderSet_;
	VkPrimitiveTopology curPrim_;
	Thin3DVKVertexFormat *curVertexFormat_;
	T3DCullMode curCullMode_;

	// We keep a pipeline state cache.
	std::map<PipelineKey, VkPipeline> pipelines_;

	// Also, a descriptor set state cache. We precompute a descriptor set for every active texture.
	// The only difference between descriptor sets will be their textures. And input bindings? Hm
	std::map<DescriptorSetKey, VkDescriptorSet> descSets_;

	VkDescriptorPool descriptorPool_;
	VkDescriptorSet descriptorSet_;
	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;
	VkPipelineCache pipelineCache_;

	VkCommandPool cmdPool_;
	VkInstance instance_;
	VkPhysicalDevice physicalDevice_;
	VkDevice device_;
	VkQueue queue_;
	VkRenderPass renderPass_;
	int queueFamilyIndex_;

	// Default object that Thin3D doesn't yet abstract
	VkSampler sampler_;

	// State to apply at the next draw call if viewportDirty or scissorDirty are true.
	bool viewportDirty_;
	VkViewport viewport_;
	bool scissorDirty_;
	VkRect2D scissor_;
	bool scissorEnabled_;

	VkRect2D noScissor_;  // Simply a scissor covering the screen.

	enum {MAX_BOUND_TEXTURES = 1};
	Thin3DVKTexture *boundTextures_[MAX_BOUND_TEXTURES];

	// Ephemeral command buffer used for initializing textures, etc. As there can only be one in flight, can cause annoying stalls until I do something better.
	VkCommandBuffer initCmd_;
	bool hasInitCommands_;
	VkFence initFence_;
	bool pendingInitFence_;

	// TODO: Transpose this into a struct FrameObject[2].

	// We write to one, while we wait for the draws from the other to complete.
	// Then, at the end of the frame, they switch roles.
	// cmdBuf_ for commands, pushBuffer_ for data. cmd_ will often refer to push_.

	VkCommandBuffer cmdBuffer_[2];
	VkCommandBuffer cmd_; // The current one
	
	VkFence cmdFences_[2];
	VkFence cmdFence_;

	VulkanPushBuffer *pushBuffer_[2];
	int frameNum_;
	VulkanPushBuffer *push_;
};


VkFormat FormatToVulkan(T3DImageFormat fmt) {
	switch (fmt) {
	case RGBA8888: return VK_FORMAT_R8G8B8A8_UNORM;
	case RGBA4444: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
	case D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
	case D16: return VK_FORMAT_D16_UNORM;
	default: return VK_FORMAT_UNDEFINED;
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
	Thin3DVKTexture(VkDevice device, VulkanDeviceMemoryManager *memMan) : device_(device), memMan_(memMan), state_(TextureState::UNINITIALIZED) {
	}
	Thin3DVKTexture(VkDevice device, VulkanDeviceMemoryManager *memMan, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels)
		: device_(device), memMan_(memMan), format_(format), mipLevels_(mipLevels) {
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

		// We don't actually do anything here.
		return true;
	}

	void Destroy() {
	}

	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;

	void Upload(VkCommandBuffer cmd);

	void Finalize(int zim_flags) override;

	void AutoGenMipmaps() {}

	bool NeedsUpload();

	VkImageView GetImageView() { return view_; }

private:
	VulkanDeviceMemoryManager *memMan_;
	VkDevice device_;
	VulkanImage image_;
	VulkanImage staging_;
	VkImageView view_;

	int32_t width_, height_, depth_;
	int mipLevels_;

	T3DImageFormat format_;
	TextureState state_;
};

Thin3DVKContext::Thin3DVKContext(VulkanContext *vulkan)
	: viewportDirty_(false), scissorDirty_(false) {
	device_ = vulkan->Device();

	noScissor_.offset.x = 0;
	noScissor_.offset.y = 0;
	noScissor_.extent.width = pixel_xres;
	noScissor_.extent.height = pixel_yres;

	memset(boundTextures_, 0, sizeof(boundTextures_));
	CreatePresets();

	vulkanMem_.Init(vulkan->GetPhysicalDevice());

	VkSamplerCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.magFilter = VK_FILTER_LINEAR;
	info.minFilter = VK_FILTER_LINEAR;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VkResult res = vkCreateSampler(device_, &info, nullptr, &sampler_);
	assert(VK_SUCCESS == res);

	VkCommandPoolCreateInfo p;
	p.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	p.pNext = nullptr;
	p.flags = 0;
	p.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();
	res = vkCreateCommandPool(device_, &p, nullptr, &cmdPool_);
	assert(VK_SUCCESS == res);

	VkDescriptorPoolSize dpTypes[3];
	dpTypes[0].descriptorCount = 200;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	dpTypes[1].descriptorCount = 1;
	dpTypes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	dpTypes[2].descriptorCount = 1;
	dpTypes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

	VkDescriptorPoolCreateInfo dp;
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.pNext = nullptr;
	dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;   // We want to individually alloc and free descriptor sets. (do we?)
	dp.maxSets = 200;  // One set for every texture available... sigh
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);
	res = vkCreateDescriptorPool(device_, &dp, nullptr, &descriptorPool_);
	assert(VK_SUCCESS == res);
	pushBuffer_[0] = new VulkanPushBuffer(device_, &vulkanMem_, 1024 * 1024);
	pushBuffer_[1] = new VulkanPushBuffer(device_, &vulkanMem_, 1024 * 1024);

	// binding 0 - vertex data
	// binding 1 - uniform data
	// binding 2 - sampler
	// binding 3 - image
	VkDescriptorSetLayoutBinding bindings[4];
	bindings[0].descriptorCount = 1;
	bindings[0].pImmutableSamplers = nullptr;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[1].descriptorCount = 1;
	bindings[1].pImmutableSamplers = nullptr;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[2].descriptorCount = 1;
	bindings[2].pImmutableSamplers = nullptr;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dsl;
	dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dsl.pNext = nullptr;
	dsl.bindingCount = 3;
	dsl.pBindings = bindings;
	res = vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &descriptorSetLayout_);
	assert(VK_SUCCESS == res);

	VkPipelineLayoutCreateInfo pl;
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.pNext = nullptr;
	pl.pPushConstantRanges = nullptr;
	pl.pushConstantRangeCount = 0;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	res = vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);

	VkCommandBufferAllocateInfo cb;
	cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cb.pNext = nullptr;
	cb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb.commandPool = cmdPool_;
	cb.commandBufferCount = 1;
	res = vkAllocateCommandBuffers(device_, &cb, &cmdBuffer_[0]);
	assert(VK_SUCCESS == res);
	res = vkAllocateCommandBuffers(device_, &cb, &cmdBuffer_[1]);
	assert(VK_SUCCESS == res);
	res = vkAllocateCommandBuffers(device_, &cb, &initCmd_);
	assert(VK_SUCCESS == res);
	hasInitCommands_ = false;
	
	VkFenceCreateInfo f;
	f.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	f.pNext = nullptr;
	f.flags = 0;
	res = vkCreateFence(device_, &f, nullptr, &cmdFences_[0]);
	assert(VK_SUCCESS == res);

	res = vkCreateFence(device_, &f, nullptr, &cmdFences_[1]);
	assert(VK_SUCCESS == res);
	// Create as already signalled, so we can wait for it the first time.
	f.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	res = vkCreateFence(device_, &f, nullptr, &initFence_);
	assert(VK_SUCCESS == res);
	pendingInitFence_ = false;

	VkPipelineCacheCreateInfo pc;
	pc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	pc.pNext = nullptr;
	pc.pInitialData = nullptr;
	pc.initialDataSize = 0;
	pc.flags = 0;
	res = vkCreatePipelineCache(device_, &pc, nullptr, &pipelineCache_);
	assert(VK_SUCCESS == res);

	push_ = pushBuffer_[0];
	cmd_ = cmdBuffer_[0];
}

Thin3DVKContext::~Thin3DVKContext() {
	for (auto x : pipelines_) {
		vkDestroyPipeline(device_, x.second, nullptr);
	}
	vkFreeCommandBuffers(device_, cmdPool_, 2, cmdBuffer_);
	vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd_);
	vkDestroyCommandPool(device_, cmdPool_, nullptr);
	// This also destroys all descriptor sets.
	vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
	vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
	vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
	vkDestroySampler(device_, sampler_, nullptr);
	vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
}

void Thin3DVKContext::Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) {
	VkClearValue clearVal[2];
	Uint8x4ToFloat4(colorval, clearVal[0].color.float32);
	clearVal[0].color.float32[2] = 1.0f;
	clearVal[1].depthStencil.depth = depthVal;
	clearVal[1].depthStencil.stencil = stencilVal;
	vulkan_->BeginSurfaceRenderPass(clearVal);

	// Make sure we don't stomp over the old command buffer.
	vkWaitForFences(device_, 1, &cmdFence_, true, 0);
	push_->Begin(device_);
}

void Thin3DVKContext::BeginInitCommands() {
	assert(!hasInitCommands_);

	// Before we can begin, we must be sure that the command buffer is no longer in use, as we only have a single one for init
	// tasks (for now).

	VkCommandBufferBeginInfo begin;
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.pNext = nullptr;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin.pInheritanceInfo = nullptr;
	VkResult res = vkBeginCommandBuffer(initCmd_, &begin);
	assert(VK_SUCCESS == res);
	hasInitCommands_ = true;
}

void Thin3DVKContext::EndInitCommands() {
	VkResult res = vkEndCommandBuffer(initCmd_);
	assert(VK_SUCCESS == res);
}

void Thin3DVKContext::End() {
	// Stop collecting data in the frame data buffer.
	push_->End(device_);

	vkCmdEndRenderPass(cmd_);
	VkResult endRes = vkEndCommandBuffer(cmd_);

	if (hasInitCommands_) {
		assert(!pendingInitFence_);
		EndInitCommands();

		// Run the texture uploads etc _before_ we execute the ordinary command buffer
		pendingInitFence_ = true;
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pCommandBuffers = &initCmd_;
		submit.commandBufferCount = 1;
		VkResult res = vkQueueSubmit(queue_, 1, &submit, initFence_);
		assert(VK_SUCCESS == res);
		hasInitCommands_ = false;
	}

	if (VK_SUCCESS != endRes) {
		ELOG("vkEndCommandBuffer failed");
		vkResetCommandBuffer(cmd_, 0);
	} else {
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pCommandBuffers = &cmd_;
		submit.commandBufferCount = 1;
		VkResult res = vkQueueSubmit(queue_, 1, &submit, cmdFence_);
		assert(VK_SUCCESS == res);
	}

	frameNum_++;
	push_ = pushBuffer_[frameNum_ & 1];
	cmd_ = cmdBuffer_[frameNum_ & 1];
	cmdFence_ = cmdFences_[frameNum_ & 1];

	DirtyDynamicState();
}

VkDescriptorSet Thin3DVKContext::GetOrCreateDescriptorSet() {
	DescriptorSetKey key;
	key.texture_ = boundTextures_[0];
	key.vertexFormat_ = nullptr;
	key.frame = frameNum_ & 1;

	auto iter = descSets_.find(key);
	if (iter != descSets_.end()) {
		return iter->second;
	}

	VkDescriptorSet descSet;
	VkDescriptorSetAllocateInfo alloc;
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.pNext = nullptr;
	alloc.descriptorPool = descriptorPool_;
	alloc.pSetLayouts = &descriptorSetLayout_;
	alloc.descriptorSetCount = 1;
	VkResult res = vkAllocateDescriptorSets(device_, &alloc, &descSet);
	assert(VK_SUCCESS == res);

	// bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	// bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	// bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

	VkDescriptorBufferInfo bufferDesc;
	bufferDesc.buffer = push_->GetVkBuffer();
	bufferDesc.offset = 0;
	bufferDesc.range = 16 * 4;

	VkDescriptorImageInfo imageDesc;
	imageDesc.imageView = boundTextures_[0]->GetImageView();
	imageDesc.sampler = sampler_;
	imageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet writes[2];
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].pNext = nullptr;
	writes[0].dstSet = descSet;
	writes[0].dstArrayElement = 0;
	writes[0].dstBinding = 1;
	writes[0].pBufferInfo = &bufferDesc;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].pNext = nullptr;
	writes[1].dstSet = descSet;
	writes[1].dstArrayElement = 0;
	writes[1].dstBinding = 2;
	writes[1].pImageInfo = &imageDesc;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

	descSets_[key] = descSet;
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

	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.pNext = nullptr;
	inputAssembly.topology = curPrim_;
	inputAssembly.primitiveRestartEnable = false;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo;
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.pNext = nullptr;
	dynamicInfo.dynamicStateCount = ARRAY_SIZE(dynamics);
	dynamicInfo.pDynamicStates = dynamics;

	VkPipelineRasterizationStateCreateInfo raster;
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.pNext = nullptr;
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

	VkPipelineMultisampleStateCreateInfo ms;
	memset(&ms, 0, sizeof(ms));
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.pNext = nullptr;
	ms.pSampleMask = nullptr;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineViewportStateCreateInfo vs;
	vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vs.pNext = nullptr;
	vs.viewportCount = 1;
	vs.scissorCount = 1;
	vs.pViewports = nullptr;  // dynamic
	vs.pScissors = nullptr;  // dynamic

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
	info.renderPass = renderPass_;

	// OK, need to create a new pipeline.
	VkPipeline pipeline;
	VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &pipeline);
	if (result != VK_SUCCESS) {
		ELOG("Failed to create graphics pipeline");
		return nullptr;
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
	return new Thin3DVKTexture(device_, &vulkanMem_);
}

Thin3DTexture *Thin3DVKContext::CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) {
	return new Thin3DVKTexture(device_, &vulkanMem_, type, format, width, height, depth, mipLevels);
}

void Thin3DVKTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	VkFormat vulkanFormat = FormatToVulkan(format_);

	// nVidia and probably many other GPUs can texture _directly_ from a mappable buffer! But let's not rely on that, and it's not quite optimal.
	// So we need to do a staging copy. We upload the data to the staging buffer immediately, then we actually do the final copy once it's used the first time
	// as we need a command buffer and the architecture of Thin3D doesn't really work the way we want.. 
	if (!image_.IsValid()) {
		staging_.Create2D(device_, memMan_, vulkanFormat, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, width, height);
		image_.Create2D(device_, memMan_, vulkanFormat, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), width, height);
	}

	VkImageViewCreateInfo iv;
	iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	iv.pNext = nullptr;
	iv.image = image_.GetImage();
	iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
	iv.flags = 0;
	iv.format = FormatToVulkan(format_);
	iv.components.r = VK_COMPONENT_SWIZZLE_R;
	iv.components.g = VK_COMPONENT_SWIZZLE_G;
	iv.components.b = VK_COMPONENT_SWIZZLE_B;
	iv.components.a = VK_COMPONENT_SWIZZLE_A;
	iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	iv.subresourceRange.layerCount = 1;
	iv.subresourceRange.baseArrayLayer = 0;
	iv.subresourceRange.baseMipLevel = 0;
	iv.subresourceRange.levelCount = 1;
	VkResult res = vkCreateImageView(device_, &iv, nullptr, &view_);
	assert(VK_SUCCESS == res);

	// TODO: Support setting only parts of the image efficiently.
	staging_.SetImageData2D(device_, data, width, height, stride);
	state_ = TextureState::STAGED;
}

void Thin3DVKTexture::Finalize(int zim_flags) {
	// TODO
}

bool Thin3DVKTexture::NeedsUpload() {
	return state_ == TextureState::STAGED;
}

void Thin3DVKTexture::Upload(VkCommandBuffer cmd) {
	if (state_ == TextureState::STAGED) {
		// Before we can texture, we need to Copy and ChangeLayout.
		VkImageCopy copy_region;
		copy_region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy_region.srcOffset = { 0, 0, 0 };
		copy_region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy_region.dstOffset = { 0, 0, 0 };
		copy_region.extent = { (uint32_t)width_, (uint32_t)height_, 1 };
		vkCmdCopyImage(cmd, staging_.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
		image_.ChangeLayout(cmd, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		// From this point on, the image can be used for texturing.
		// Even before this function call (but after SetImageData), the image object can be referenced in a descriptor set.
		state_ = TextureState::INITIALIZED;
	}
}

static bool isPowerOf2(int n) {
	return n == 1 || (n & (n - 1)) == 0;
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
		// Bind simply copies the texture to VRAM if needed.
		if (boundTextures_[i]->NeedsUpload()) {
			if (!hasInitCommands_) {
				BeginInitCommands();
			}
			boundTextures_[i]->Upload(initCmd_);
		}
	}
}

Thin3DShader *Thin3DVKContext::CreateVertexShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DVKShader *shader = new Thin3DVKShader(vulkan_, false);
	if (shader->Compile(device_, vulkan_source)) {
		return shader;
	} else {
		shader->Release();
		return nullptr;
	}
}

Thin3DShader *Thin3DVKContext::CreateFragmentShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DVKShader *shader = new Thin3DVKShader(vulkan_, true);
	if (shader->Compile(device_, vulkan_source)) {
		return shader;
	} else {
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

	/*
	auto iter = uniforms_.find(name);
	if (iter != uniforms_.end()) {
		loc = iter->second.loc_;
	} else {
		loc = glGetUniformLocation(program_, name);
		UniformInfo info;
		info.loc_ = loc;
		uniforms_[name] = info;
	}*/

	return loc;
}

void Thin3DVKShaderSet::SetVector(const char *name, float *value, int n) {
	// TODO: Implement
}

void Thin3DVKShaderSet::SetMatrix4x4(const char *name, const Matrix4x4 &value) {
	int loc = GetUniformLoc(name);
	if (loc != -1) {
		memcpy(ubo_ + loc, value.getReadPtr(), 16 * sizeof(float));
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

	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize());

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	VkDescriptorSet descSet = GetOrCreateDescriptorSet();
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);
	VkBuffer buffers[1] = { push_->GetVkBuffer() };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);
	vkCmdDraw(cmd_, vertexCount, 1, offset, 0);
}

void Thin3DVKContext::DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) {
	ApplyDynamicState();
	
	curPrim_ = primToVK[prim];
	curShaderSet_ = (Thin3DVKShaderSet *)shaderSet;
	curVertexFormat_ = (Thin3DVKVertexFormat *)format;

	Thin3DVKBuffer *ibuf = (Thin3DVKBuffer *)idata;
	Thin3DVKBuffer *vbuf = (Thin3DVKBuffer *)vdata;

	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize());
	size_t ibBindOffset = push_->Push(ibuf->GetData(), ibuf->GetSize());

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet();
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);

	VkBuffer buffers[1] = { push_->GetVkBuffer() };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);

	vkCmdBindIndexBuffer(cmd_, push_->GetVkBuffer(), ibBindOffset, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(cmd_, vertexCount, 1, 0, offset, 0);
}

void Thin3DVKContext::DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) {
	ApplyDynamicState();

	curPrim_ = primToVK[prim];
	curShaderSet_ = (Thin3DVKShaderSet *)shaderSet;
	curVertexFormat_ = (Thin3DVKVertexFormat *)format;

	uint32_t ubo_offset = (uint32_t)curShaderSet_->PushUBO(push_);
	size_t vbBindOffset = push_->Push(vdata, vertexCount * curVertexFormat_->stride_);

	VkPipeline pipeline = GetOrCreatePipeline();
	vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDescriptorSet descSet = GetOrCreateDescriptorSet();
	vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 1, &ubo_offset);

	VkBuffer buffers[1] = { push_->GetVkBuffer() };
	VkDeviceSize offsets[1] = { vbBindOffset };
	vkCmdBindVertexBuffers(cmd_, 0, 1, buffers, offsets);
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
	if (mask & T3DClear::DEPTH | T3DClear::STENCIL) {

	}
}

Thin3DContext *T3DCreateVulkanContext(VulkanContext *vulkan) {
	return new Thin3DVKContext(vulkan);
}
