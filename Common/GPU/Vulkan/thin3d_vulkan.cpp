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

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Thread/Promise.h"

// For descriptor set 0 (the only one), we use a simple descriptor set for all thin3d rendering: 1 UBO binding point, 3 combined texture/samples.
//
// binding 0 - uniform buffer
// binding 1 - texture/sampler
// binding 2 - texture/sampler
// binding 3 - texture/sampler
//
// Vertex data lives in a separate namespace (location = 0, 1, etc).

using namespace PPSSPP_VK;

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
	VKRasterState(const RasterStateDesc &desc) {
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
	case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
	case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
	case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
	case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	return VK_SHADER_STAGE_FRAGMENT_BIT;
}

// Not registering this as a resource holder, instead the pipeline is registered. It will
// invoke Compile again to recreate the shader then link them together.
class VKShaderModule : public ShaderModule {
public:
	VKShaderModule(ShaderStage stage, const std::string &tag) : stage_(stage), tag_(tag) {
		vkstage_ = StageToVulkan(stage);
	}
	bool Compile(VulkanContext *vulkan, const uint8_t *data, size_t size);
	const std::string &GetSource() const { return source_; }
	~VKShaderModule() {
		if (module_) {
			VkShaderModule shaderModule = module_->BlockUntilReady();
			vulkan_->Delete().QueueDeleteShaderModule(shaderModule);
			vulkan_->Delete().QueueCallback([](VulkanContext *context, void *m) {
				auto module = (Promise<VkShaderModule> *)m;
				delete module;
			}, module_);
		}
	}
	Promise<VkShaderModule> *Get() const { return module_; }
	ShaderStage GetStage() const override {
		return stage_;
	}

private:
	VulkanContext *vulkan_ = nullptr;
	Promise<VkShaderModule> *module_ = nullptr;
	VkShaderStageFlagBits vkstage_;
	bool ok_ = false;
	ShaderStage stage_;
	std::string source_;  // So we can recompile in case of context loss.
	std::string tag_;
};

bool VKShaderModule::Compile(VulkanContext *vulkan, const uint8_t *data, size_t size) {
	// We'll need this to free it later.
	vulkan_ = vulkan;
	source_ = (const char *)data;
	std::vector<uint32_t> spirv;
	std::string errorMessage;
	if (!GLSLtoSPV(vkstage_, source_.c_str(), GLSLVariant::VULKAN, spirv, &errorMessage)) {
		WARN_LOG(Log::G3D, "Shader compile to module failed (%s): %s", tag_.c_str(), errorMessage.c_str());
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

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	if (vulkan->CreateShaderModule(spirv, &shaderModule, tag_.c_str())) {
		module_ = Promise<VkShaderModule>::AlreadyDone(shaderModule);
		ok_ = true;
	} else {
		WARN_LOG(Log::G3D, "vkCreateShaderModule failed (%s)", tag_.c_str());
		ok_ = false;
	}
	return ok_;
}

class VKInputLayout : public InputLayout {
public:
	VkVertexInputBindingDescription binding;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateInfo visc;
};

class VKPipeline : public Pipeline {
public:
	VKPipeline(VulkanContext *vulkan, size_t size, PipelineFlags _flags, const char *tag) : vulkan_(vulkan), flags(_flags), tag_(tag) {
		uboSize_ = (int)size;
		ubo_ = new uint8_t[uboSize_];
		vkrDesc = new VKRGraphicsPipelineDesc();
	}
	~VKPipeline() {
		if (pipeline) {
			pipeline->QueueForDeletion(vulkan_);
		}
		for (auto dep : deps) {
			dep->Release();
		}
		delete[] ubo_;
		vkrDesc->Release();
	}

	void SetDynamicUniformData(const void *data, size_t size) {
		_dbg_assert_(size <= uboSize_);
		memcpy(ubo_, data, size);
	}

	// Returns the binding offset, and the VkBuffer to bind.
	size_t PushUBO(VulkanPushPool *buf, VulkanContext *vulkan, VkBuffer *vkbuf) {
		return buf->Push(ubo_, uboSize_, vulkan->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment, vkbuf);
	}

	int GetUBOSize() const {
		return uboSize_;
	}

	VKRGraphicsPipeline *pipeline = nullptr;
	VKRGraphicsPipelineDesc *vkrDesc = nullptr;
	PipelineFlags flags;

	std::vector<VKShaderModule *> deps;

	int stride = 0;
	int dynamicUniformSize = 0;

	bool usesStencil = false;

private:
	VulkanContext *vulkan_;
	uint8_t *ubo_;
	int uboSize_;
	std::string tag_;
};

class VKTexture;
class VKBuffer;
class VKSamplerState;

enum {
	MAX_BOUND_TEXTURES = MAX_TEXTURE_SLOTS,
};

struct DescriptorSetKey {
	VkImageView imageViews_[MAX_BOUND_TEXTURES];
	VKSamplerState *samplers_[MAX_BOUND_TEXTURES];
	VkBuffer buffer_;

	bool operator < (const DescriptorSetKey &other) const {
		for (int i = 0; i < MAX_BOUND_TEXTURES; ++i) {
			if (imageViews_[i] < other.imageViews_[i]) return true; else if (imageViews_[i] > other.imageViews_[i]) return false;
			if (samplers_[i] < other.samplers_[i]) return true; else if (samplers_[i] > other.samplers_[i]) return false;
		}
		if (buffer_ < other.buffer_) return true; else if (buffer_ > other.buffer_) return false;
		return false;
	}
};

class VKTexture : public Texture {
public:
	VKTexture(VulkanContext *vulkan, const TextureDesc &desc)
		: vulkan_(vulkan), mipLevels_(desc.mipLevels) {
		format_ = desc.format;
	}
	bool Create(VkCommandBuffer cmd, VulkanBarrierBatch *postBarriers, VulkanPushPool *pushBuffer, const TextureDesc &desc);
	void Update(VkCommandBuffer cmd, VulkanBarrierBatch *postBarriers, VulkanPushPool *pushBuffer, const uint8_t *const *data, TextureCallback callback, int numLevels);

	~VKTexture() {
		Destroy();
	}

	VkImageView GetImageView() {
		if (vkTex_) {
			return vkTex_->GetImageView();
		}
		return VK_NULL_HANDLE;  // This would be bad.
	}

	VkImageView GetImageArrayView() {
		if (vkTex_) {
			return vkTex_->GetImageArrayView();
		}
		return VK_NULL_HANDLE;  // This would be bad.
	}

	int NumLevels() const {
		return mipLevels_;
	}

private:
	void UpdateInternal(VkCommandBuffer cmd, VulkanPushPool *pushBuffer, const uint8_t *const *data, TextureCallback callback, int numLevels);

	void Destroy() {
		if (vkTex_) {
			vkTex_->Destroy();
			delete vkTex_;
			vkTex_ = nullptr;
		}
	}

	VulkanContext *vulkan_;
	VulkanTexture *vkTex_ = nullptr;

	int mipLevels_ = 0;
};

class VKFramebuffer;

class VKContext : public DrawContext {
public:
	VKContext(VulkanContext *vulkan, bool useRenderThread);
	~VKContext();

	BackendState GetCurrentBackendState() const override {
		return BackendState{
			(u32)renderManager_.GetNumSteps(),
			true,  // Means that the other value is meaningful.
		};
	}

	void DebugAnnotate(const char *annotation) override;
	void Wait() override {
		vkDeviceWaitIdle(vulkan_->GetDevice());
	}

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	std::vector<std::string> GetDeviceList() const override {
		std::vector<std::string> list;
		for (int i = 0; i < vulkan_->GetNumPhysicalDevices(); i++) {
			list.emplace_back(vulkan_->GetPhysicalDeviceProperties(i).properties.deviceName);
		}
		return list;
	}
	std::vector<std::string> GetPresentModeList(std::string_view currentMarkerString) const override {
		std::vector<std::string> list;
		for (auto mode : vulkan_->GetAvailablePresentModes()) {
			std::string str = VulkanPresentModeToString(mode);
			if (mode == vulkan_->GetPresentMode()) {
				str += std::string(" (") + std::string(currentMarkerString) + ")";
			}
			list.push_back(str);
		}
		return list;
	}
	std::vector<std::string> GetSurfaceFormatList() const override {
		std::vector<std::string> list;
		for (auto &format : vulkan_->SurfaceFormats()) {
			std::string str = StringFromFormat("%s : %s", VulkanFormatToString(format.format), VulkanColorSpaceToString(format.colorSpace));
			list.push_back(str);
		}
		return list;
	}

	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::GLSL_VULKAN;
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	PresentMode GetPresentMode() const {
		switch (vulkan_->GetPresentMode()) {
		case VK_PRESENT_MODE_FIFO_KHR: return PresentMode::FIFO;
		case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return PresentMode::FIFO;  // We treat is as FIFO for now (and won't ever enable it anyway...)
		case VK_PRESENT_MODE_IMMEDIATE_KHR: return PresentMode::IMMEDIATE;
		case VK_PRESENT_MODE_MAILBOX_KHR: return PresentMode::MAILBOX;
		default: return PresentMode::FIFO;
		}
	}

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) override;

	Texture *CreateTexture(const TextureDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;
	void UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemory(Framebuffer *src, Aspect aspects, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) override;
	DataFormat PreferredFramebufferReadbackFormat(Framebuffer *src) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect channelBit, int layer) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewport(const Viewport &viewport) override;
	void SetBlendFactor(float color[4]) override;
	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override;

	void BindSamplerStates(int start, int count, SamplerState **state) override;
	void BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) override;
	void BindNativeTexture(int sampler, void *nativeTexture) override;

	void BindPipeline(Pipeline *pipeline) override {
		curPipeline_ = (VKPipeline *)pipeline;
	}

	void BindVertexBuffer(Buffer *vertexBuffer, int offset) override {
		curVBuffer_ = (VKBuffer *)vertexBuffer;
		curVBufferOffset_ = offset;
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
	void DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) override;
	// Specialized for quick IMGUI drawing.
	void DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw>, const void *dynUniforms, size_t size) override;

	void BindCurrentPipeline();
	void ApplyDynamicState();

	void Clear(Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) override;

	void BeginFrame(DebugFlags debugFlags) override;
	void EndFrame() override;
	void Present(PresentMode presentMode, int vblanks) override;

	int GetFrameCount() override {
		return frameCount_;
	}

	void FlushState() override {}

	void ResetStats() override {
		renderManager_.ResetStats();
	}
	void StopThreads() override {
		renderManager_.StopThreads();
	}

	void StartThreads() override {
		renderManager_.StartThreads();
	}


	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case InfoField::APINAME: return "Vulkan";
		case InfoField::VENDORSTRING: return vulkan_->GetPhysicalDeviceProperties().properties.deviceName;
		case InfoField::VENDOR: return VulkanVendorString(vulkan_->GetPhysicalDeviceProperties().properties.vendorID);
		case InfoField::DRIVER: return FormatDriverVersion(vulkan_->GetPhysicalDeviceProperties().properties);
		case InfoField::SHADELANGVERSION: return "N/A";;
		case InfoField::APIVERSION: return FormatAPIVersion(vulkan_->InstanceApiVersion());
		case InfoField::DEVICE_API_VERSION: return FormatAPIVersion(vulkan_->DeviceApiVersion());
		default: return "?";
		}
	}

	void BindDescriptors(VkBuffer buffer, PackedDescriptor descriptors[4]);

	std::vector<std::string> GetFeatureList() const override;
	std::vector<std::string> GetExtensionList(bool device, bool enabledOnly) const override;

	uint64_t GetNativeObject(NativeObject obj, void *srcObject) override;

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

	void Invalidate(InvalidationFlags flags) override;

	void InvalidateFramebuffer(FBInvalidationStage stage, Aspect aspects) override;

	void SetInvalidationCallback(InvalidationCallback callback) override {
		renderManager_.SetInvalidationCallback(callback);
	}

	std::string GetGpuProfileString() const override {
		return renderManager_.GetGpuProfileString();
	}

private:
	VulkanTexture *GetNullTexture();
	VulkanContext *vulkan_ = nullptr;

	int frameCount_ = 0;
	VulkanRenderManager renderManager_;

	VulkanTexture *nullTexture_ = nullptr;

	AutoRef<VKPipeline> curPipeline_;
	AutoRef<VKBuffer> curVBuffer_;
	int curVBufferOffset_ = 0;
	AutoRef<VKBuffer> curIBuffer_;
	int curIBufferOffset_ = 0;

	VKRPipelineLayout *pipelineLayout_ = nullptr;
	VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
	AutoRef<VKFramebuffer> curFramebuffer_;

	VkDevice device_;

	enum {
		MAX_FRAME_COMMAND_BUFFERS = 256,
	};
	AutoRef<VKTexture> boundTextures_[MAX_BOUND_TEXTURES];
	AutoRef<VKSamplerState> boundSamplers_[MAX_BOUND_TEXTURES];
	VkImageView boundImageView_[MAX_BOUND_TEXTURES]{};
	TextureBindFlags boundTextureFlags_[MAX_BOUND_TEXTURES]{};

	VulkanPushPool *push_ = nullptr;

	DeviceCaps caps_{};

	uint8_t stencilRef_ = 0;
	uint8_t stencilWriteMask_ = 0xFF;
	uint8_t stencilCompareMask_ = 0xFF;
};

// Bits per pixel, not bytes.
// VERY incomplete!
static int GetBpp(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
		return 32;
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_S8_UINT:
		return 8;
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R16_UNORM:
		return 16;
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
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		return 4;
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		return 8;
	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
		return 8;
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		return 4;
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		return 8;
	default:
		return 0;
	}
}

static VkFormat DataFormatToVulkan(DataFormat format) {
	switch (format) {
	case DataFormat::D16: return VK_FORMAT_D16_UNORM;
	case DataFormat::D16_S8: return VK_FORMAT_D16_UNORM_S8_UINT;
	case DataFormat::D24_S8: return VK_FORMAT_D24_UNORM_S8_UINT;
	case DataFormat::D32F: return VK_FORMAT_D32_SFLOAT;
	case DataFormat::D32F_S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
	case DataFormat::S8: return VK_FORMAT_S8_UINT;

	case DataFormat::R16_UNORM: return VK_FORMAT_R16_UNORM;

	case DataFormat::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
	case DataFormat::R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
	case DataFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case DataFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
	case DataFormat::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
	case DataFormat::R8G8B8_UNORM: return VK_FORMAT_R8G8B8_UNORM;
	case DataFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case DataFormat::R4G4_UNORM_PACK8: return VK_FORMAT_R4G4_UNORM_PACK8;

	// Note: A4R4G4B4_UNORM_PACK16 is not supported.
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
	case DataFormat::BC5_UNORM_BLOCK: return VK_FORMAT_BC5_UNORM_BLOCK;
	case DataFormat::BC7_UNORM_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;

	case DataFormat::ETC2_R8G8B8A1_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
	case DataFormat::ETC2_R8G8B8A8_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
	case DataFormat::ETC2_R8G8B8_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;

	case DataFormat::ASTC_4x4_UNORM_BLOCK: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

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
		nullTexture_ = new VulkanTexture(vulkan_, "Null");
		int w = 8;
		int h = 8;
		VulkanBarrierBatch barrier;
		nullTexture_->CreateDirect(w, h, 1, 1, VK_FORMAT_A8B8G8R8_UNORM_PACK32, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &barrier);
		barrier.Flush(cmdInit);
		uint32_t bindOffset;
		VkBuffer bindBuf;
		uint32_t *data = (uint32_t *)push_->Allocate(w * h * 4, 4, &bindBuf, &bindOffset);
		_assert_(data != nullptr);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				// data[y*w + x] = ((x ^ y) & 1) ? 0xFF808080 : 0xFF000000;   // gray/black checkerboard
				data[y*w + x] = 0;  // black
			}
		}
		TextureCopyBatch batch;
		nullTexture_->CopyBufferToMipLevel(cmdInit, &batch, 0, w, h, 0, bindBuf, bindOffset, w);
		nullTexture_->FinishCopyBatch(cmdInit, &batch);
		nullTexture_->EndCreate(cmdInit, false, VK_PIPELINE_STAGE_TRANSFER_BIT);
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
		s.maxAnisotropy = desc.maxAniso;
		s.magFilter = desc.magFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.minFilter = desc.minFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.mipmapMode = desc.mipFilter == TextureFilter::LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.maxLod = VK_LOD_CLAMP_NONE;
		VkResult res = vkCreateSampler(vulkan_->GetDevice(), &s, nullptr, &sampler_);
		_assert_(VK_SUCCESS == res);
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
	return new VKRasterState(desc);
}

void VKContext::BindSamplerStates(int start, int count, SamplerState **state) {
	_assert_(start + count <= MAX_BOUND_TEXTURES);
	for (int i = start; i < start + count; i++) {
		boundSamplers_[i] = (VKSamplerState *)state[i - start];
	}
}

enum class TextureState {
	UNINITIALIZED,
	STAGED,
	INITIALIZED,
	PENDING_DESTRUCTION,
};

bool VKTexture::Create(VkCommandBuffer cmd, VulkanBarrierBatch *postBarriers, VulkanPushPool *pushBuffer, const TextureDesc &desc) {
	// Zero-sized textures not allowed.
	_assert_(desc.width * desc.height * desc.depth > 0);  // remember to set depth to 1!
	if (desc.width * desc.height * desc.depth <= 0) {
		ERROR_LOG(Log::G3D,  "Bad texture dimensions %dx%dx%d", desc.width, desc.height, desc.depth);
		return false;
	}
	_dbg_assert_(pushBuffer);
	format_ = desc.format;
	mipLevels_ = desc.mipLevels;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	vkTex_ = new VulkanTexture(vulkan_, desc.tag);
	VkFormat vulkanFormat = DataFormatToVulkan(format_);
	int usageBits = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	if (mipLevels_ > (int)desc.initData.size()) {
		// Gonna have to generate some, which requires TRANSFER_SRC
		usageBits |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	VkComponentMapping r8AsAlpha[4] = { {VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_R} };
	VkComponentMapping r8AsColor[4] = { {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE} };

	VkComponentMapping *swizzle = nullptr;
	switch (desc.swizzle) {
	case TextureSwizzle::R8_AS_ALPHA: swizzle = r8AsAlpha; break;
	case TextureSwizzle::R8_AS_GRAYSCALE: swizzle = r8AsColor; break;
	case TextureSwizzle::DEFAULT:
		break;
	}
	VulkanBarrierBatch barrier;
	if (!vkTex_->CreateDirect(width_, height_, 1, mipLevels_, vulkanFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, usageBits, &barrier, swizzle)) {
		ERROR_LOG(Log::G3D,  "Failed to create VulkanTexture: %dx%dx%d fmt %d, %d levels", width_, height_, depth_, (int)vulkanFormat, mipLevels_);
		return false;
	}
	barrier.Flush(cmd);
	VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	if (desc.initData.size()) {
		UpdateInternal(cmd, pushBuffer, desc.initData.data(), desc.initDataCallback, (int)desc.initData.size());
		// Generate the rest of the mips automatically.
		if (desc.initData.size() < mipLevels_) {
			vkTex_->GenerateMips(cmd, (int)desc.initData.size(), false);
			layout = VK_IMAGE_LAYOUT_GENERAL;
		}
	}
	vkTex_->EndCreate(cmd, false, VK_PIPELINE_STAGE_TRANSFER_BIT, layout);
	return true;
}

void VKTexture::Update(VkCommandBuffer cmd, VulkanBarrierBatch *postBarriers, VulkanPushPool *pushBuffer, const uint8_t * const *data, TextureCallback initDataCallback, int numLevels) {
	// Before we can use UpdateInternal, we need to transition the image to the same state as after CreateDirect,
	// making it ready for writing.
	vkTex_->PrepareForTransferDst(cmd, numLevels);
	UpdateInternal(cmd, pushBuffer, data, initDataCallback, numLevels);
	vkTex_->RestoreAfterTransferDst(numLevels, postBarriers);
}

void VKTexture::UpdateInternal(VkCommandBuffer cmd, VulkanPushPool *pushBuffer, const uint8_t * const *data, TextureCallback initDataCallback, int numLevels) {
	int w = width_;
	int h = height_;
	int d = depth_;
	VkFormat vulkanFormat = DataFormatToVulkan(format_);
	int bpp = GetBpp(vulkanFormat);
	_dbg_assert_(bpp != 0);
	int bytesPerPixel = bpp / 8;
	TextureCopyBatch batch;
	batch.reserve(numLevels);
	for (int i = 0; i < numLevels; i++) {
		uint32_t offset;
		VkBuffer buf;
		size_t size = w * h * d * bytesPerPixel;
		uint8_t *dest = (uint8_t *)pushBuffer->Allocate(size, 16, &buf, &offset);
		if (initDataCallback) {
			_assert_(dest != nullptr);
			if (!initDataCallback(dest, data[i], w, h, d, w * bytesPerPixel, h * w * bytesPerPixel)) {
				memcpy(dest, data[i], size);
			}
		} else {
			memcpy(dest, data[i], size);
		}
		vkTex_->CopyBufferToMipLevel(cmd, &batch, i, w, h, 0, buf, offset, w);
		w = (w + 1) / 2;
		h = (h + 1) / 2;
		d = (d + 1) / 2;
	}
	vkTex_->FinishCopyBatch(cmd, &batch);
}

static DataFormat DataFormatFromVulkanDepth(VkFormat fmt) {
	switch (fmt) {
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return DataFormat::D24_S8;
	case VK_FORMAT_D16_UNORM:
		return DataFormat::D16;
	case VK_FORMAT_D32_SFLOAT:
		return DataFormat::D32F;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return DataFormat::D32F_S8;
	case VK_FORMAT_D16_UNORM_S8_UINT:
		return DataFormat::D16_S8;
	default:
		break;
	}

	return DataFormat::UNDEFINED;
}

VKContext::VKContext(VulkanContext *vulkan, bool useRenderThread)
	: vulkan_(vulkan), renderManager_(vulkan, useRenderThread, frameTimeHistory_) {
	shaderLanguageDesc_.Init(GLSL_VULKAN);

	INFO_LOG(Log::G3D, "Determining Vulkan device caps");

	caps_.coordConvention = CoordConvention::Vulkan;
	caps_.setMaxFrameLatencySupported = true;
	caps_.anisoSupported = vulkan->GetDeviceFeatures().enabled.standard.samplerAnisotropy != 0;
	caps_.geometryShaderSupported = vulkan->GetDeviceFeatures().enabled.standard.geometryShader != 0;
	caps_.tesselationShaderSupported = vulkan->GetDeviceFeatures().enabled.standard.tessellationShader != 0;
	caps_.dualSourceBlend = vulkan->GetDeviceFeatures().enabled.standard.dualSrcBlend != 0;
	caps_.depthClampSupported = vulkan->GetDeviceFeatures().enabled.standard.depthClamp != 0;

	// Comment out these two to test geometry shader culling on any geometry shader-supporting hardware.
	caps_.clipDistanceSupported = vulkan->GetDeviceFeatures().enabled.standard.shaderClipDistance != 0;
	caps_.cullDistanceSupported = vulkan->GetDeviceFeatures().enabled.standard.shaderCullDistance != 0;

	caps_.framebufferBlitSupported = true;
	caps_.framebufferCopySupported = true;
	caps_.framebufferDepthBlitSupported = vulkan->GetDeviceInfo().canBlitToPreferredDepthStencilFormat;
	caps_.framebufferStencilBlitSupported = caps_.framebufferDepthBlitSupported;
	caps_.framebufferDepthCopySupported = true;   // Will pretty much always be the case.
	caps_.framebufferSeparateDepthCopySupported = true;   // Will pretty much always be the case.
	// This doesn't affect what depth/stencil format is actually used, see VulkanQueueRunner.
	caps_.preferredDepthBufferFormat = DataFormatFromVulkanDepth(vulkan->GetDeviceInfo().preferredDepthStencilFormat);
	caps_.texture3DSupported = true;
	caps_.textureDepthSupported = true;
	caps_.fragmentShaderInt32Supported = true;
	caps_.textureNPOTFullySupported = true;
	caps_.fragmentShaderDepthWriteSupported = true;
	caps_.fragmentShaderStencilWriteSupported = vulkan->Extensions().EXT_shader_stencil_export;
	caps_.blendMinMaxSupported = true;
	caps_.logicOpSupported = vulkan->GetDeviceFeatures().enabled.standard.logicOp != 0;
	caps_.multiViewSupported = vulkan->GetDeviceFeatures().enabled.multiview.multiview != 0;
	caps_.sampleRateShadingSupported = vulkan->GetDeviceFeatures().enabled.standard.sampleRateShading != 0;
	caps_.textureSwizzleSupported = true;

	// Note that it must also be enabled on the pipelines (which we do).
	caps_.provokingVertexLast = vulkan->GetDeviceFeatures().enabled.provokingVertex.provokingVertexLast;

	// Present mode stuff
	caps_.presentMaxInterval = 1;
	caps_.presentInstantModeChange = false;  // TODO: Fix this with some work in VulkanContext
	caps_.presentModesSupported = (PresentMode)0;
	for (auto mode : vulkan->GetAvailablePresentModes()) {
		switch (mode) {
		case VK_PRESENT_MODE_FIFO_KHR: caps_.presentModesSupported |= PresentMode::FIFO; break;
		case VK_PRESENT_MODE_IMMEDIATE_KHR: caps_.presentModesSupported |= PresentMode::IMMEDIATE; break;
		case VK_PRESENT_MODE_MAILBOX_KHR: caps_.presentModesSupported |= PresentMode::MAILBOX; break;
		default: break;  // Ignore any other modes.
		}
	}

	const auto &limits = vulkan->GetPhysicalDeviceProperties().properties.limits;

	auto deviceProps = vulkan->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDeviceIndex()).properties;

	switch (deviceProps.vendorID) {
	case VULKAN_VENDOR_AMD: caps_.vendor = GPUVendor::VENDOR_AMD; break;
	case VULKAN_VENDOR_ARM: caps_.vendor = GPUVendor::VENDOR_ARM; break;
	case VULKAN_VENDOR_IMGTEC: caps_.vendor = GPUVendor::VENDOR_IMGTEC; break;
	case VULKAN_VENDOR_NVIDIA: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
	case VULKAN_VENDOR_QUALCOMM: caps_.vendor = GPUVendor::VENDOR_QUALCOMM; break;
	case VULKAN_VENDOR_INTEL: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
	case VULKAN_VENDOR_APPLE: caps_.vendor = GPUVendor::VENDOR_APPLE; break;
	case VULKAN_VENDOR_MESA: caps_.vendor = GPUVendor::VENDOR_MESA; break;
	default:
		WARN_LOG(Log::G3D, "Unknown vendor ID %08x", deviceProps.vendorID);
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
		break;
	}

	switch (caps_.vendor) {
	case GPUVendor::VENDOR_ARM:
	case GPUVendor::VENDOR_IMGTEC:
	case GPUVendor::VENDOR_QUALCOMM:
		caps_.isTilingGPU = true;
		break;
	default:
		caps_.isTilingGPU = false;
		break;
	}

	if (caps_.vendor == GPUVendor::VENDOR_IMGTEC) {
		// Enable some things that cut down pipeline counts but may have other costs.
		caps_.verySlowShaderCompiler = true;
	}

	// VkSampleCountFlagBits is arranged correctly for our purposes.
	// Only support MSAA levels that have support for all three of color, depth, stencil.

	bool multisampleAllowed = true;

    caps_.deviceID = deviceProps.deviceID;

    if (caps_.vendor == GPUVendor::VENDOR_QUALCOMM) {
		if (caps_.deviceID < 0x6000000) { // On sub 6xx series GPUs, disallow multisample.
			INFO_LOG(Log::G3D, "Multisampling was disabled due to old driver version (Adreno)");
			multisampleAllowed = false;
		}

		// Adreno 5xx devices, all known driver versions, fail to discard stencil when depth write is off.
		// See: https://github.com/hrydgard/ppsspp/pull/11684
		if (deviceProps.deviceID >= 0x05000000 && deviceProps.deviceID < 0x06000000) {
			if (deviceProps.driverVersion < 0x80180000) {
				bugs_.Infest(Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO);
			}
		}
		// Color write mask not masking write in certain scenarios with a depth test, see #10421.
		// Known still present on driver 0x80180000 and Adreno 5xx (possibly more.)
		// Known working on driver 0x801EA000 and Adreno 620.
		if (deviceProps.driverVersion < 0x801EA000 || deviceProps.deviceID < 0x06000000)
			bugs_.Infest(Bugs::COLORWRITEMASK_BROKEN_WITH_DEPTHTEST);

		// Trying to follow all the rules in https://registry.khronos.org/vulkan/specs/1.3/html/vkspec.html#synchronization-pipeline-barriers-subpass-self-dependencies
		// and https://registry.khronos.org/vulkan/specs/1.3/html/vkspec.html#renderpass-feedbackloop, but still it doesn't
		// quite work - artifacts on triangle boundaries on Adreno.
		bugs_.Infest(Bugs::SUBPASS_FEEDBACK_BROKEN);
	} else if (caps_.vendor == GPUVendor::VENDOR_AMD) {
		// See issue #10074, and also #10065 (AMD) and #10109 for the choice of the driver version to check for.
		if (deviceProps.driverVersion < 0x00407000) {
			bugs_.Infest(Bugs::DUAL_SOURCE_BLENDING_BROKEN);
		}
	} else if (caps_.vendor == GPUVendor::VENDOR_INTEL) {
		// Workaround for Intel driver bug. TODO: Re-enable after some driver version
		bugs_.Infest(Bugs::DUAL_SOURCE_BLENDING_BROKEN);
	} else if (caps_.vendor == GPUVendor::VENDOR_ARM) {
		// Really old Vulkan drivers for Mali didn't have proper versions. We try to detect that (can't be 100% but pretty good).
		bool isOldVersion = IsHashMaliDriverVersion(deviceProps);

		int majorVersion = VK_API_VERSION_MAJOR(deviceProps.driverVersion);

		// These GPUs (up to some certain hardware version?) have a bug where draws where gl_Position.w == .z
		// corrupt the depth buffer. This is easily worked around by simply scaling Z down a tiny bit when this case
		// is detected. See: https://github.com/hrydgard/ppsspp/issues/11937
		bugs_.Infest(Bugs::EQUAL_WZ_CORRUPTS_DEPTH);

		// Nearly identical to the the Adreno bug, see #13833 (Midnight Club map broken) and other issues.
		// It has the additional caveat that combining depth writes with NEVER depth tests crashes the driver.
		// Reported fixed in major version 40 - let's add a check once confirmed.
		bugs_.Infest(Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI);

		// This started in driver 31 or 32, fixed in 40 - let's add a check once confirmed.
		if (majorVersion >= 32) {
			bugs_.Infest(Bugs::MALI_CONSTANT_LOAD_BUG);  // See issue #15661
		}

		// Older ARM devices have very slow geometry shaders, not worth using.  At least before 15.
		// Also seen to cause weird issues on 18, so let's lump it in.
		if (majorVersion <= 18 || isOldVersion) {
			bugs_.Infest(Bugs::GEOMETRY_SHADERS_SLOW_OR_BROKEN);
		}

		// Attempt to workaround #17386
		if (isOldVersion) {
			if (!strcmp(deviceProps.deviceName, "Mali-T880") ||
				!strcmp(deviceProps.deviceName, "Mali-T860") ||
				!strcmp(deviceProps.deviceName, "Mali-T830")) {
				bugs_.Infest(Bugs::UNIFORM_INDEXING_BROKEN);
			}
		}

		if (isOldVersion) {
			// Very rough heuristic.
			multisampleAllowed = false;
		}
	} else if (caps_.vendor == GPUVendor::VENDOR_IMGTEC) {
		// Not sure about driver versions, so let's just ban, impact is tiny.
		bugs_.Infest(Bugs::PVR_BAD_16BIT_TEXFORMATS);
	}

	if (!vulkan->Extensions().KHR_depth_stencil_resolve) {
		INFO_LOG(Log::G3D, "KHR_depth_stencil_resolve not supported, disabling multisampling");
		multisampleAllowed = false;
	}

	if (!vulkan->Extensions().KHR_create_renderpass2) {
		WARN_LOG(Log::G3D, "KHR_create_renderpass2 not supported, disabling multisampling");
		multisampleAllowed = false;
	} else {
		_dbg_assert_(vkCreateRenderPass2 != nullptr);
	}

	// We limit multisampling functionality to reasonably recent and known-good tiling GPUs.
	if (multisampleAllowed) {
		// Check for depth stencil resolve. Without it, depth textures won't work, and we don't want that mess
		// of compatibility reports, so we'll just disable multisampling in this case for now.
		// There are potential workarounds for devices that don't support it, but those are nearly non-existent now.
		const auto &resolveProperties = vulkan->GetPhysicalDeviceProperties().depthStencilResolve;
		if (((resolveProperties.supportedDepthResolveModes & resolveProperties.supportedStencilResolveModes) & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) != 0) {
			caps_.multiSampleLevelsMask = (limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts & limits.framebufferStencilSampleCounts);
			INFO_LOG(Log::G3D, "Multisample levels mask: %d", caps_.multiSampleLevelsMask);
		} else {
			INFO_LOG(Log::G3D, "Not enough depth/stencil resolve modes supported, disabling multisampling. Color: %d Depth: %d Stencil: %d",
				limits.framebufferColorSampleCounts, limits.framebufferDepthSampleCounts, limits.framebufferStencilSampleCounts);
			caps_.multiSampleLevelsMask = 1;
		}
	} else {
		caps_.multiSampleLevelsMask = 1;
	}

	// Vulkan can support this through input attachments and various extensions, but not worth
	// the trouble.
	caps_.framebufferFetchSupported = false;

	device_ = vulkan->GetDevice();

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	push_ = new VulkanPushPool(vulkan_, "pushBuffer", 4 * 1024 * 1024, usage);

	// binding 0 - uniform data
	// binding 1 - combined sampler/image 0
	// binding 2 - combined sampler/image 1
	// ...etc
	BindingType bindings[MAX_BOUND_TEXTURES + 1];
	bindings[0] = BindingType::UNIFORM_BUFFER_DYNAMIC_ALL;
	for (int i = 0; i < MAX_BOUND_TEXTURES; ++i) {
		bindings[1 + i] = BindingType::COMBINED_IMAGE_SAMPLER;
	}
	pipelineLayout_ = renderManager_.CreatePipelineLayout(bindings, ARRAY_SIZE(bindings), caps_.geometryShaderSupported, "thin3d_layout");

	VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	VkResult res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &pipelineCache_);
	_assert_(VK_SUCCESS == res);
}

VKContext::~VKContext() {
	DestroyPresets();

	delete nullTexture_;
	push_->Destroy();
	delete push_;
	renderManager_.DestroyPipelineLayout(pipelineLayout_);
	vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
}

void VKContext::BeginFrame(DebugFlags debugFlags) {
	renderManager_.BeginFrame(debugFlags & DebugFlags::PROFILE_TIMESTAMPS, debugFlags & DebugFlags::PROFILE_SCOPES);
	push_->BeginFrame();
}

void VKContext::EndFrame() {
	// Do all the work to submit the command buffers etc.
	renderManager_.Finish();
	// Unbind stuff, to avoid accidentally relying on it across frames (and provide some protection against forgotten unbinds of deleted things).
	Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
}

void VKContext::Present(PresentMode presentMode, int vblanks) {
	if (presentMode == PresentMode::FIFO) {
		_dbg_assert_(vblanks == 0 || vblanks == 1);
	}
	renderManager_.Present();
	frameCount_++;
}

void VKContext::Invalidate(InvalidationFlags flags) {
	if (flags & InvalidationFlags::CACHED_RENDER_STATE) {
		curPipeline_ = nullptr;

		for (auto &view : boundImageView_) {
			view = VK_NULL_HANDLE;
		}
		for (auto &sampler : boundSamplers_) {
			sampler = nullptr;
		}
		for (auto &texture : boundTextures_) {
			texture = nullptr;
		}
	}
}

void VKContext::BindDescriptors(VkBuffer buf, PackedDescriptor descriptors[4]) {
	descriptors[0].buffer.buffer = buf;
	descriptors[0].buffer.offset = 0;  // dynamic
	descriptors[0].buffer.range = curPipeline_->GetUBOSize();

	for (int i = 0; i < MAX_BOUND_TEXTURES; ++i) {
		VkImageView view;
		VkSampler sampler;
		if (boundTextures_[i]) {
			view = (boundTextureFlags_[i] & TextureBindFlags::VULKAN_BIND_ARRAY) ? boundTextures_[i]->GetImageArrayView() : boundTextures_[i]->GetImageView();
		} else {
			view = boundImageView_[i];
		}
		sampler = boundSamplers_[i] ? boundSamplers_[i]->GetSampler() : VK_NULL_HANDLE;

		if (view && sampler) {
			descriptors[i + 1].image.view = view;
			descriptors[i + 1].image.sampler = sampler;
		} else {
			descriptors[i + 1].image.view = VK_NULL_HANDLE;
			descriptors[i + 1].image.sampler = VK_NULL_HANDLE;
		}
	}
}

Pipeline *VKContext::CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) {
	VKInputLayout *input = (VKInputLayout *)desc.inputLayout;
	VKBlendState *blend = (VKBlendState *)desc.blend;
	VKDepthStencilState *depth = (VKDepthStencilState *)desc.depthStencil;
	VKRasterState *raster = (VKRasterState *)desc.raster;

	PipelineFlags pipelineFlags = (PipelineFlags)0;
	if (depth->info.depthTestEnable || depth->info.stencilTestEnable) {
		pipelineFlags |= PipelineFlags::USES_DEPTH_STENCIL;
	}
	// TODO: We need code to set USES_BLEND_CONSTANT here too, if we're ever gonna use those in thin3d code.

	VKPipeline *pipeline = new VKPipeline(vulkan_, desc.uniformDesc ? desc.uniformDesc->uniformBufferSize : 16 * sizeof(float), pipelineFlags, tag);

	VKRGraphicsPipelineDesc &gDesc = *pipeline->vkrDesc;

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	stages.resize(desc.shaders.size());

	for (auto &iter : desc.shaders) {
		VKShaderModule *vkshader = (VKShaderModule *)iter;
		vkshader->AddRef();
		pipeline->deps.push_back(vkshader);
		if (vkshader->GetStage() == ShaderStage::Vertex) {
			gDesc.vertexShader = vkshader->Get();
		} else if (vkshader->GetStage() == ShaderStage::Fragment) {
			gDesc.fragmentShader = vkshader->Get();
		} else {
			ERROR_LOG(Log::G3D, "Bad stage");
			delete pipeline;
			return nullptr;
		}
	}

	_dbg_assert_(input);
	_dbg_assert_((int)input->attributes.size() == (int)input->visc.vertexAttributeDescriptionCount);

	pipeline->stride = input->binding.stride;
	gDesc.ibd = input->binding;
	for (size_t i = 0; i < input->attributes.size(); i++) {
		gDesc.attrs[i] = input->attributes[i];
	}
	gDesc.vis.vertexAttributeDescriptionCount = input->visc.vertexAttributeDescriptionCount;
	gDesc.vis.vertexBindingDescriptionCount = input->visc.vertexBindingDescriptionCount;
	gDesc.vis.pVertexBindingDescriptions = &gDesc.ibd;
	gDesc.vis.pVertexAttributeDescriptions = gDesc.attrs;

	gDesc.blend0 = blend->attachments[0];
	gDesc.cbs = blend->info;
	gDesc.cbs.pAttachments = &gDesc.blend0;

	gDesc.dss = depth->info;

	// Copy bindings from input layout.
	gDesc.topology = primToVK[(int)desc.prim];

	// We treat the three stencil states as a unit in other places, so let's do that here too.
	const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK };
	gDesc.ds.dynamicStateCount = depth->info.stencilTestEnable ? ARRAY_SIZE(dynamics) : 2;
	for (size_t i = 0; i < gDesc.ds.dynamicStateCount; i++) {
		gDesc.dynamicStates[i] = dynamics[i];
	}
	gDesc.ds.pDynamicStates = gDesc.dynamicStates;

	gDesc.views.viewportCount = 1;
	gDesc.views.scissorCount = 1;
	gDesc.views.pViewports = nullptr;  // dynamic
	gDesc.views.pScissors = nullptr;  // dynamic

	gDesc.pipelineLayout = pipelineLayout_;

	raster->ToVulkan(&gDesc.rs);

	if (renderManager_.GetVulkanContext()->GetDeviceFeatures().enabled.provokingVertex.provokingVertexLast) {
		ChainStruct(gDesc.rs, &gDesc.rs_provoking);
		gDesc.rs_provoking.provokingVertexMode = VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
	}

	pipeline->pipeline = renderManager_.CreateGraphicsPipeline(&gDesc, pipelineFlags, 1 << (size_t)RenderPassType::BACKBUFFER, VK_SAMPLE_COUNT_1_BIT, false, tag ? tag : "thin3d");

	if (desc.uniformDesc) {
		pipeline->dynamicUniformSize = (int)desc.uniformDesc->uniformBufferSize;
	}
	if (depth->info.stencilTestEnable) {
		pipeline->usesStencil = true;
	}
	return pipeline;
}

void VKContext::SetScissorRect(int left, int top, int width, int height) {
	renderManager_.SetScissor(left, top, width, height);
}

void VKContext::SetViewport(const Viewport &viewport) {
	// Ignore viewports more than the first.
	VkViewport vkViewport;
	vkViewport.x = viewport.TopLeftX;
	vkViewport.y = viewport.TopLeftY;
	vkViewport.width = viewport.Width;
	vkViewport.height = viewport.Height;
	vkViewport.minDepth = viewport.MinDepth;
	vkViewport.maxDepth = viewport.MaxDepth;
	renderManager_.SetViewport(vkViewport);
}

void VKContext::SetBlendFactor(float color[4]) {
	uint32_t col = Float4ToUint8x4(color);
	renderManager_.SetBlendFactor(col);
}

void VKContext::SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) {
	if (curPipeline_->usesStencil)
		renderManager_.SetStencilParams(writeMask, compareMask, refValue);
	stencilRef_ = refValue;
	stencilWriteMask_ = writeMask;
	stencilCompareMask_ = compareMask;
}

InputLayout *VKContext::CreateInputLayout(const InputLayoutDesc &desc) {
	VKInputLayout *vl = new VKInputLayout();
	vl->visc = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vl->visc.flags = 0;
	vl->visc.vertexBindingDescriptionCount = 1;
	vl->visc.vertexAttributeDescriptionCount = (uint32_t)desc.attributes.size();
	vl->attributes.resize(vl->visc.vertexAttributeDescriptionCount);
	vl->visc.pVertexBindingDescriptions = &vl->binding;
	vl->visc.pVertexAttributeDescriptions = vl->attributes.data();
	for (size_t i = 0; i < desc.attributes.size(); i++) {
		vl->attributes[i].binding = 0;
		vl->attributes[i].format = DataFormatToVulkan(desc.attributes[i].format);
		vl->attributes[i].location = desc.attributes[i].location;
		vl->attributes[i].offset = desc.attributes[i].offset;
	}
	vl->binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vl->binding.binding = 0;
	vl->binding.stride = desc.stride;
	return vl;
}

Texture *VKContext::CreateTexture(const TextureDesc &desc) {
	VkCommandBuffer initCmd = renderManager_.GetInitCmd();
	if (!push_ || !initCmd) {
		// Too early! Fail.
		ERROR_LOG(Log::G3D,  "Can't create textures before the first frame has started.");
		return nullptr;
	}
	VKTexture *tex = new VKTexture(vulkan_, desc);
	if (tex->Create(initCmd, &renderManager_.PostInitBarrier(), push_, desc)) {
		return tex;
	} else {
		ERROR_LOG(Log::G3D,  "Failed to create texture");
		tex->Release();
		return nullptr;
	}
}

void VKContext::UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) {
	VkCommandBuffer initCmd = renderManager_.GetInitCmd();
	if (!push_ || !initCmd) {
		// Too early! Fail.
		ERROR_LOG(Log::G3D, "Can't create textures before the first frame has started.");
		return;
	}

	VKTexture *tex = (VKTexture *)texture;

	_dbg_assert_(numLevels <= tex->NumLevels());
	tex->Update(initCmd, &renderManager_.PostInitBarrier(), push_, data, initDataCallback, numLevels);
}

static inline void CopySide(VkStencilOpState &dest, const StencilSetup &src) {
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
	ds->info.stencilTestEnable = desc.stencilEnabled;
	ds->info.depthBoundsTestEnable = false;
	if (ds->info.stencilTestEnable) {
		CopySide(ds->info.front, desc.stencil);
		CopySide(ds->info.back, desc.stencil);
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
	VKBuffer(size_t size) : dataSize_(size) {
		data_ = new uint8_t[size];
	}
	~VKBuffer() {
		delete[] data_;
	}

	size_t GetSize() const { return dataSize_; }
	const uint8_t *GetData() const { return data_; }

	uint8_t *data_;
	size_t dataSize_;
};

Buffer *VKContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new VKBuffer(size);
}

void VKContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	VKBuffer *buf = (VKBuffer *)buffer;
	memcpy(buf->data_ + offset, data, size);
}

void VKContext::BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) {
	_assert_(start + count <= MAX_BOUND_TEXTURES);
	for (int i = start; i < start + count; i++) {
		_dbg_assert_(i >= 0 && i < MAX_BOUND_TEXTURES);
		boundTextures_[i] = static_cast<VKTexture *>(textures[i - start]);
		boundTextureFlags_[i] = flags;
		if (boundTextures_[i]) {
			// If a texture is bound, we set these up in BindDescriptors too.
			// But we might need to set the view here anyway so it can be queried using GetNativeObject.
			if (flags & TextureBindFlags::VULKAN_BIND_ARRAY) {
				boundImageView_[i] = boundTextures_[i]->GetImageArrayView();
			} else {
				boundImageView_[i] = boundTextures_[i]->GetImageView();
			}
		} else {
			if (flags & TextureBindFlags::VULKAN_BIND_ARRAY) {
				boundImageView_[i] = GetNullTexture()->GetImageArrayView();
			} else {
				boundImageView_[i] = GetNullTexture()->GetImageView();
			}
		}
	}
}

void VKContext::BindNativeTexture(int sampler, void *nativeTexture) {
	_dbg_assert_(sampler >= 0 && sampler < MAX_BOUND_TEXTURES);
	boundTextures_[sampler] = nullptr;
	boundImageView_[sampler] = (VkImageView)nativeTexture;
}

ShaderModule *VKContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t size, const char *tag) {
	VKShaderModule *shader = new VKShaderModule(stage, tag);
	if (shader->Compile(vulkan_, data, size)) {
		return shader;
	} else {
		ERROR_LOG(Log::G3D, "Failed to compile shader %s:\n%s", tag, (const char *)LineNumberString((const char *)data).c_str());
		shader->Release();
		return nullptr;
	}
}

void VKContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	curPipeline_->SetDynamicUniformData(ub, size);
}

void VKContext::ApplyDynamicState() {
	// TODO: blend constants, stencil, viewports should be here, after bindpipeline..
	if (curPipeline_->usesStencil) {
		renderManager_.SetStencilParams(stencilWriteMask_, stencilCompareMask_, stencilRef_);
	}
}

void VKContext::Draw(int vertexCount, int offset) {
	VKBuffer *vbuf = curVBuffer_;

	VkBuffer vulkanVbuf;
	VkBuffer vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), 4, &vulkanVbuf);

	BindCurrentPipeline();
	ApplyDynamicState();
	int descSetIndex;
	PackedDescriptor *descriptors = renderManager_.PushDescriptorSet(4, &descSetIndex);
	BindDescriptors(vulkanUBObuf, descriptors);
	renderManager_.Draw(descSetIndex, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset + curVBufferOffset_, vertexCount, offset);
}

void VKContext::DrawIndexed(int vertexCount, int offset) {
	VKBuffer *ibuf = curIBuffer_;
	VKBuffer *vbuf = curVBuffer_;

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);
	size_t vbBindOffset = push_->Push(vbuf->GetData(), vbuf->GetSize(), 4, &vulkanVbuf);
	size_t ibBindOffset = push_->Push(ibuf->GetData(), ibuf->GetSize(), 4, &vulkanIbuf);

	BindCurrentPipeline();
	ApplyDynamicState();
	int descSetIndex;
	PackedDescriptor *descriptors = renderManager_.PushDescriptorSet(4, &descSetIndex);
	BindDescriptors(vulkanUBObuf, descriptors);
	renderManager_.DrawIndexed(descSetIndex, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset + curVBufferOffset_, vulkanIbuf, (int)ibBindOffset + offset * sizeof(uint32_t), vertexCount, 1);
}

void VKContext::DrawUP(const void *vdata, int vertexCount) {
	_dbg_assert_(vertexCount >= 0);
	if (vertexCount <= 0) {
		return;
	}

	VkBuffer vulkanVbuf, vulkanUBObuf;
	size_t dataSize = vertexCount * curPipeline_->stride;
	uint32_t vbBindOffset;
	uint8_t *dataPtr = push_->Allocate(dataSize, 4, &vulkanVbuf, &vbBindOffset);
	_assert_(dataPtr != nullptr);
	memcpy(dataPtr, vdata, dataSize);

	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	BindCurrentPipeline();
	ApplyDynamicState();
	int descSetIndex;
	PackedDescriptor *descriptors = renderManager_.PushDescriptorSet(4, &descSetIndex);
	BindDescriptors(vulkanUBObuf, descriptors);
	renderManager_.Draw(descSetIndex, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vertexCount);
}

void VKContext::DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) {
	_dbg_assert_(vertexCount >= 0);
	_dbg_assert_(indexCount >= 0);
	if (vertexCount <= 0 || indexCount <= 0) {
		return;
	}

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	size_t vdataSize = vertexCount * curPipeline_->stride;
	uint32_t vbBindOffset;
	uint8_t *vdataPtr = push_->Allocate(vdataSize, 4, &vulkanVbuf, &vbBindOffset);
	_assert_(vdataPtr != nullptr);
	memcpy(vdataPtr, vdata, vdataSize);

	size_t idataSize = indexCount * sizeof(u16);
	uint32_t ibBindOffset;
	uint8_t *idataPtr = push_->Allocate(idataSize, 4, &vulkanIbuf, &ibBindOffset);
	_assert_(idataPtr != nullptr);
	memcpy(idataPtr, vdata, idataSize);

	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	BindCurrentPipeline();
	ApplyDynamicState();
	int descSetIndex;
	PackedDescriptor *descriptors = renderManager_.PushDescriptorSet(4, &descSetIndex);
	BindDescriptors(vulkanUBObuf, descriptors);
	renderManager_.DrawIndexed(descSetIndex, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vulkanIbuf, (int)ibBindOffset, indexCount, 1);
}

void VKContext::DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) {
	_dbg_assert_(vertexCount >= 0);
	_dbg_assert_(indexCount >= 0);
	if (vertexCount <= 0 || indexCount <= 0 || draws.is_empty()) {
		return;
	}

	curPipeline_ = (VKPipeline *)draws[0].pipeline;

	VkBuffer vulkanVbuf, vulkanIbuf, vulkanUBObuf;
	size_t vdataSize = vertexCount * curPipeline_->stride;
	uint32_t vbBindOffset;
	uint8_t *vdataPtr = push_->Allocate(vdataSize, 4, &vulkanVbuf, &vbBindOffset);
	_assert_(vdataPtr != nullptr);
	memcpy(vdataPtr, vdata, vdataSize);

	constexpr int indexSize = sizeof(u16);

	size_t idataSize = indexCount * indexSize;
	uint32_t ibBindOffset;
	uint8_t *idataPtr = push_->Allocate(idataSize, 4, &vulkanIbuf, &ibBindOffset);
	_assert_(idataPtr != nullptr);
	memcpy(idataPtr, idata, idataSize);

	curPipeline_->SetDynamicUniformData(ub, ubSize);

	uint32_t ubo_offset = (uint32_t)curPipeline_->PushUBO(push_, vulkan_, &vulkanUBObuf);

	BindCurrentPipeline();
	ApplyDynamicState();

	for (auto &draw : draws) {
		if (draw.pipeline != curPipeline_) {
			VKPipeline *vkPipe = (VKPipeline *)draw.pipeline;
			renderManager_.BindPipeline(vkPipe->pipeline, vkPipe->flags, pipelineLayout_);
			curPipeline_ = (VKPipeline *)draw.pipeline;
			curPipeline_->SetDynamicUniformData(ub, ubSize);
		}
		// TODO: Dirty-check these.
		if (draw.bindTexture) {
			BindTexture(0, draw.bindTexture);
		} else if (draw.bindFramebufferAsTex) {
			BindFramebufferAsTexture(draw.bindFramebufferAsTex, 0, draw.aspect, 0);
		} else if (draw.bindNativeTexture) {
			BindNativeTexture(0, draw.bindNativeTexture);
		}
		Draw::SamplerState *sstate = draw.samplerState;
		BindSamplerStates(0, 1, &sstate);
		int descSetIndex;
		PackedDescriptor *descriptors = renderManager_.PushDescriptorSet(4, &descSetIndex);
		BindDescriptors(vulkanUBObuf, descriptors);
		renderManager_.SetScissor(draw.clipx, draw.clipy, draw.clipw, draw.cliph);
		renderManager_.DrawIndexed(descSetIndex, 1, &ubo_offset, vulkanVbuf, (int)vbBindOffset, vulkanIbuf,
			(int)ibBindOffset + draw.indexOffset * indexSize, draw.indexCount, 1);
	}
}

void VKContext::BindCurrentPipeline() {
	renderManager_.BindPipeline(curPipeline_->pipeline, curPipeline_->flags, pipelineLayout_);
}

void VKContext::Clear(Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) {
	int mask = 0;
	if (aspects & Aspect::COLOR_BIT)
		mask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (aspects & Aspect::DEPTH_BIT)
		mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (aspects & Aspect::STENCIL_BIT)
		mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	renderManager_.Clear(colorval, depthVal, stencilVal, mask);
}

DrawContext *T3DCreateVulkanContext(VulkanContext *vulkan, bool useRenderThread) {
	return new VKContext(vulkan, useRenderThread);
}

void AddFeature(std::vector<std::string> &features, const char *name, VkBool32 available, VkBool32 enabled) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s: Available: %d Enabled: %d", name, (int)available, (int)enabled);
	features.push_back(buf);
}

std::vector<std::string> VKContext::GetFeatureList() const {
	const VkPhysicalDeviceFeatures &available = vulkan_->GetDeviceFeatures().available.standard;
	const VkPhysicalDeviceFeatures &enabled = vulkan_->GetDeviceFeatures().enabled.standard;

	std::vector<std::string> features;
	AddFeature(features, "dualSrcBlend", available.dualSrcBlend, enabled.dualSrcBlend);
	AddFeature(features, "logicOp", available.logicOp, enabled.logicOp);
	AddFeature(features, "geometryShader", available.geometryShader, enabled.geometryShader);
	AddFeature(features, "depthBounds", available.depthBounds, enabled.depthBounds);
	AddFeature(features, "depthClamp", available.depthClamp, enabled.depthClamp);
	AddFeature(features, "pipelineStatisticsQuery", available.pipelineStatisticsQuery, enabled.pipelineStatisticsQuery);
	AddFeature(features, "samplerAnisotropy", available.samplerAnisotropy, enabled.samplerAnisotropy);
	AddFeature(features, "textureCompressionBC", available.textureCompressionBC, enabled.textureCompressionBC);
	AddFeature(features, "textureCompressionETC2", available.textureCompressionETC2, enabled.textureCompressionETC2);
	AddFeature(features, "textureCompressionASTC_LDR", available.textureCompressionASTC_LDR, enabled.textureCompressionASTC_LDR);
	AddFeature(features, "shaderClipDistance", available.shaderClipDistance, enabled.shaderClipDistance);
	AddFeature(features, "shaderCullDistance", available.shaderCullDistance, enabled.shaderCullDistance);
	AddFeature(features, "occlusionQueryPrecise", available.occlusionQueryPrecise, enabled.occlusionQueryPrecise);
	AddFeature(features, "multiDrawIndirect", available.multiDrawIndirect, enabled.multiDrawIndirect);
	AddFeature(features, "robustBufferAccess", available.robustBufferAccess, enabled.robustBufferAccess);
	AddFeature(features, "fullDrawIndexUint32", available.fullDrawIndexUint32, enabled.fullDrawIndexUint32);
	AddFeature(features, "fragmentStoresAndAtomics", available.fragmentStoresAndAtomics, enabled.fragmentStoresAndAtomics);
	AddFeature(features, "shaderInt16", available.shaderInt16, enabled.shaderInt16);

	AddFeature(features, "multiview", vulkan_->GetDeviceFeatures().available.multiview.multiview, vulkan_->GetDeviceFeatures().enabled.multiview.multiview);
	AddFeature(features, "multiviewGeometryShader", vulkan_->GetDeviceFeatures().available.multiview.multiviewGeometryShader, vulkan_->GetDeviceFeatures().enabled.multiview.multiviewGeometryShader);
	AddFeature(features, "presentId", vulkan_->GetDeviceFeatures().available.presentId.presentId, vulkan_->GetDeviceFeatures().enabled.presentId.presentId);
	AddFeature(features, "presentWait", vulkan_->GetDeviceFeatures().available.presentWait.presentWait, vulkan_->GetDeviceFeatures().enabled.presentWait.presentWait);
	AddFeature(features, "provokingVertexLast", vulkan_->GetDeviceFeatures().available.provokingVertex.provokingVertexLast, vulkan_->GetDeviceFeatures().enabled.provokingVertex.provokingVertexLast);

	features.emplace_back(std::string("Preferred depth buffer format: ") + VulkanFormatToString(vulkan_->GetDeviceInfo().preferredDepthStencilFormat));

	return features;
}

std::vector<std::string> VKContext::GetExtensionList(bool device, bool enabledOnly) const {
	std::vector<std::string> extensions;
	if (enabledOnly) {
		const auto& enabled = (device ? vulkan_->GetDeviceExtensionsEnabled() : vulkan_->GetInstanceExtensionsEnabled());
		extensions.reserve(enabled.size());
		for (auto &iter : enabled) {
			extensions.push_back(iter);
		}
	} else {
		const auto& available = (device ? vulkan_->GetDeviceExtensionsAvailable() : vulkan_->GetInstanceExtensionsAvailable());
		extensions.reserve(available.size());
		for (auto &iter : available) {
			extensions.push_back(iter.extensionName);
		}
	}
	return extensions;
}

uint32_t VKContext::GetDataFormatSupport(DataFormat fmt) const {
	VkFormat vulkan_format = DataFormatToVulkan(fmt);
	VkFormatProperties properties;
	vkGetPhysicalDeviceFormatProperties(vulkan_->GetCurrentPhysicalDevice(), vulkan_format, &properties);
	uint32_t flags = 0;
	if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
		flags |= FMT_RENDERTARGET;
	}
	if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		flags |= FMT_DEPTHSTENCIL;
	}
	if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
		flags |= FMT_TEXTURE;
	}
	if (properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) {
		flags |= FMT_INPUTLAYOUT;
	}
	if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) && (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
		flags |= FMT_BLIT;
	}
	if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) {
		flags |= FMT_STORAGE_IMAGE;
	}
	return flags;
}

// A VKFramebuffer is a VkFramebuffer (note caps difference) plus all the textures it owns.
// It also has a reference to the command buffer that it was last rendered to with.
// If it needs to be transitioned, and the frame number matches, use it, otherwise
// use this frame's init command buffer.
class VKFramebuffer : public Framebuffer {
public:
	VKFramebuffer(VKRFramebuffer *fb, int multiSampleLevel) : buf_(fb) {
		_assert_msg_(fb, "Null fb in VKFramebuffer constructor");
		width_ = fb->width;
		height_ = fb->height;
		layers_ = fb->numLayers;
		multiSampleLevel_ = multiSampleLevel;
	}
	~VKFramebuffer() {
		_assert_msg_(buf_, "Null buf_ in VKFramebuffer - double delete?");
		buf_->Vulkan()->Delete().QueueCallback([](VulkanContext *vulkan, void *fb) {
			VKRFramebuffer *vfb = static_cast<VKRFramebuffer *>(fb);
			delete vfb;
		}, buf_);
		buf_ = nullptr;
	}
	VKRFramebuffer *GetFB() const { return buf_; }
	void UpdateTag(const char *newTag) override {
		buf_->UpdateTag(newTag);
	}
	const char *Tag() const override {
		return buf_->Tag();
	}
private:
	VKRFramebuffer *buf_;
};

Framebuffer *VKContext::CreateFramebuffer(const FramebufferDesc &desc) {
	_assert_(desc.multiSampleLevel >= 0);
	_assert_(desc.numLayers > 0);
	_assert_(desc.width > 0);
	_assert_(desc.height > 0);

	VKRFramebuffer *vkrfb = new VKRFramebuffer(vulkan_, &renderManager_.PostInitBarrier(), desc.width, desc.height, desc.numLayers, desc.multiSampleLevel, desc.z_stencil, desc.tag);
	return new VKFramebuffer(vkrfb, desc.multiSampleLevel);
}

void VKContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	int aspectMask = 0;
	if (aspects & Aspect::COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (aspects & Aspect::DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (aspects & Aspect::STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	renderManager_.CopyFramebuffer(src->GetFB(), VkRect2D{ {x, y}, {(uint32_t)width, (uint32_t)height } }, dst->GetFB(), VkOffset2D{ dstX, dstY }, aspectMask, tag);
}

bool VKContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;
	VKFramebuffer *dst = (VKFramebuffer *)dstfb;

	int aspectMask = 0;
	if (aspects & Aspect::COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (aspects & Aspect::DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (aspects & Aspect::STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	renderManager_.BlitFramebuffer(src->GetFB(), VkRect2D{ {srcX1, srcY1}, {(uint32_t)(srcX2 - srcX1), (uint32_t)(srcY2 - srcY1) } }, dst->GetFB(), VkRect2D{ {dstX1, dstY1}, {(uint32_t)(dstX2 - dstX1), (uint32_t)(dstY2 - dstY1) } }, aspectMask, filter == FB_BLIT_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST, tag);
	return true;
}

bool VKContext::CopyFramebufferToMemory(Framebuffer *srcfb, Aspect aspects, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) {
	VKFramebuffer *src = (VKFramebuffer *)srcfb;

	int aspectMask = 0;
	if (aspects & Aspect::COLOR_BIT) aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (aspects & Aspect::DEPTH_BIT) aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (aspects & Aspect::STENCIL_BIT) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	return renderManager_.CopyFramebufferToMemory(src ? src->GetFB() : nullptr, aspectMask, x, y, w, h, format, (uint8_t *)pixels, pixelStride, mode, tag);
}

DataFormat VKContext::PreferredFramebufferReadbackFormat(Framebuffer *src) {
	if (src) {
		return DrawContext::PreferredFramebufferReadbackFormat(src);
	}

	if (vulkan_->GetSwapchainFormat() == VK_FORMAT_B8G8R8A8_UNORM) {
		return Draw::DataFormat::B8G8R8A8_UNORM;
	}
	return DrawContext::PreferredFramebufferReadbackFormat(src);
}

void VKContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	VKRRenderPassLoadAction color = (VKRRenderPassLoadAction)rp.color;
	VKRRenderPassLoadAction depth = (VKRRenderPassLoadAction)rp.depth;
	VKRRenderPassLoadAction stencil = (VKRRenderPassLoadAction)rp.stencil;

	renderManager_.BindFramebufferAsRenderTarget(fb ? fb->GetFB() : nullptr, color, depth, stencil, rp.clearColor, rp.clearDepth, rp.clearStencil, tag);
	curFramebuffer_ = fb;
}

void VKContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect channelBit, int layer) {
	VKFramebuffer *fb = (VKFramebuffer *)fbo;
	_assert_(binding >= 0 && binding < MAX_BOUND_TEXTURES);

	// TODO: There are cases where this is okay, actually. But requires layout transitions and stuff -
	// we're not ready for this.
	_assert_(fb != curFramebuffer_);

	int aspect = 0;
	switch (channelBit) {
	case Aspect::COLOR_BIT:
		aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		break;
	case Aspect::DEPTH_BIT:
		aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		break;
	default:
		// Hm, can we texture from stencil?
		_assert_(false);
		break;
	}

	boundTextures_[binding].reset(nullptr);
	boundImageView_[binding] = renderManager_.BindFramebufferAsTexture(fb->GetFB(), binding, aspect, layer);
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
		_assert_(false);
		break;
	}
}

void VKContext::InvalidateFramebuffer(FBInvalidationStage stage, Aspect aspects) {
	VkImageAspectFlags flags = 0;
	if (aspects & Aspect::COLOR_BIT)
		flags |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (aspects & Aspect::DEPTH_BIT)
		flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (aspects & Aspect::STENCIL_BIT)
		flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	if (stage == FB_INVALIDATION_LOAD) {
		renderManager_.SetLoadDontCare(flags);
	} else if (stage == FB_INVALIDATION_STORE) {
		renderManager_.SetStoreDontCare(flags);
	}
}

uint64_t VKContext::GetNativeObject(NativeObject obj, void *srcObject) {
	switch (obj) {
	case NativeObject::CONTEXT:
		return (uint64_t)vulkan_;
	case NativeObject::INIT_COMMANDBUFFER:
		return (uint64_t)renderManager_.GetInitCmd();
	case NativeObject::BOUND_TEXTURE0_IMAGEVIEW:
		return (uint64_t)boundImageView_[0];
	case NativeObject::BOUND_TEXTURE1_IMAGEVIEW:
		return (uint64_t)boundImageView_[1];
	case NativeObject::RENDER_MANAGER:
		return (uint64_t)(uintptr_t)&renderManager_;
	case NativeObject::NULL_IMAGEVIEW:
		return (uint64_t)GetNullTexture()->GetImageView();
	case NativeObject::NULL_IMAGEVIEW_ARRAY:
		return (uint64_t)GetNullTexture()->GetImageArrayView();
	case NativeObject::TEXTURE_VIEW:
		return (uint64_t)(((VKTexture *)srcObject)->GetImageView());
	case NativeObject::BOUND_FRAMEBUFFER_COLOR_IMAGEVIEW_ALL_LAYERS:
		return (uint64_t)curFramebuffer_->GetFB()->color.texAllLayersView;
	case NativeObject::BOUND_FRAMEBUFFER_COLOR_IMAGEVIEW_RT:
		return (uint64_t)curFramebuffer_->GetFB()->GetRTView();
	case NativeObject::THIN3D_PIPELINE_LAYOUT:
		return (uint64_t)pipelineLayout_;
	case NativeObject::PUSH_POOL:
		return (uint64_t)push_;
	default:
		Crash();
		return 0;
	}
}

void VKContext::DebugAnnotate(const char *annotation) {
	renderManager_.DebugAnnotate(annotation);
}

}  // namespace Draw
