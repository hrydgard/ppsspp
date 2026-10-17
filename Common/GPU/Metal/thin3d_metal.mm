// Copyright (c) 2012- PPSSPP Project.

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

// thin3d on Metal. Shaders are Vulkan GLSL (the same ones the Vulkan backend gets), translated to MSL,
// so bindings follow the Vulkan layout: the UBO is at buffer index 0 and texture slot n is at index n + 1.

#include "ppsspp_config.h"

#include <algorithm>
#include <cfloat>
#include <map>
#include <unordered_map>

#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/Metal/MetalRenderManager.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

namespace Draw {

static constexpr int THIN3D_TEXTURE_BINDING_BASE = 1;

static MTLPixelFormat DataFormatToMetal(DataFormat format) {
	switch (format) {
	case DataFormat::R8_UNORM: return MTLPixelFormatR8Unorm;
	case DataFormat::R8G8_UNORM: return MTLPixelFormatRG8Unorm;
	case DataFormat::R8G8B8A8_UNORM: return MTLPixelFormatRGBA8Unorm;
	case DataFormat::R8G8B8A8_UNORM_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
	case DataFormat::B8G8R8A8_UNORM: return MTLPixelFormatBGRA8Unorm;
	case DataFormat::B8G8R8A8_UNORM_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
	case DataFormat::R8G8B8A8_SNORM: return MTLPixelFormatRGBA8Snorm;
	case DataFormat::R8G8B8A8_UINT: return MTLPixelFormatRGBA8Uint;
	case DataFormat::R8G8B8A8_SINT: return MTLPixelFormatRGBA8Sint;
	case DataFormat::R16_UNORM: return MTLPixelFormatR16Unorm;
	case DataFormat::R16_FLOAT: return MTLPixelFormatR16Float;
	case DataFormat::R16G16_FLOAT: return MTLPixelFormatRG16Float;
	case DataFormat::R16G16B16A16_FLOAT: return MTLPixelFormatRGBA16Float;
	case DataFormat::R32_FLOAT: return MTLPixelFormatR32Float;
	case DataFormat::R32G32_FLOAT: return MTLPixelFormatRG32Float;
	case DataFormat::R32G32B32A32_FLOAT: return MTLPixelFormatRGBA32Float;
	case DataFormat::BC1_RGBA_UNORM_BLOCK: return MTLPixelFormatBC1_RGBA;
	case DataFormat::BC2_UNORM_BLOCK: return MTLPixelFormatBC2_RGBA;
	case DataFormat::BC3_UNORM_BLOCK: return MTLPixelFormatBC3_RGBA;
	case DataFormat::BC4_UNORM_BLOCK: return MTLPixelFormatBC4_RUnorm;
	case DataFormat::BC5_UNORM_BLOCK: return MTLPixelFormatBC5_RGUnorm;
	case DataFormat::BC7_UNORM_BLOCK: return MTLPixelFormatBC7_RGBAUnorm;
	case DataFormat::ETC2_R8G8B8_UNORM_BLOCK: return MTLPixelFormatETC2_RGB8;
	case DataFormat::ETC2_R8G8B8A1_UNORM_BLOCK: return MTLPixelFormatETC2_RGB8A1;
	case DataFormat::ETC2_R8G8B8A8_UNORM_BLOCK: return MTLPixelFormatEAC_RGBA8;
	case DataFormat::ASTC_4x4_UNORM_BLOCK: return MTLPixelFormatASTC_4x4_LDR;
	case DataFormat::S8: return MTLPixelFormatStencil8;
	case DataFormat::D16: return MTLPixelFormatDepth16Unorm;
	case DataFormat::D32F: return MTLPixelFormatDepth32Float;
	case DataFormat::D32F_S8: return MTLPixelFormatDepth32Float_Stencil8;
	default: return MTLPixelFormatInvalid;
	}
}

static MTLVertexFormat DataFormatToMetalVertex(DataFormat format) {
	switch (format) {
	case DataFormat::R32_FLOAT: return MTLVertexFormatFloat;
	case DataFormat::R32G32_FLOAT: return MTLVertexFormatFloat2;
	case DataFormat::R32G32B32_FLOAT: return MTLVertexFormatFloat3;
	case DataFormat::R32G32B32A32_FLOAT: return MTLVertexFormatFloat4;
	case DataFormat::R8G8B8A8_UNORM: return MTLVertexFormatUChar4Normalized;
	case DataFormat::R8G8B8A8_SNORM: return MTLVertexFormatChar4Normalized;
	case DataFormat::R8G8B8A8_UINT: return MTLVertexFormatUChar4;
	case DataFormat::R8G8B8A8_SINT: return MTLVertexFormatChar4;
	case DataFormat::R16_FLOAT: return MTLVertexFormatHalf;
	case DataFormat::R16G16_FLOAT: return MTLVertexFormatHalf2;
	case DataFormat::R16G16B16A16_FLOAT: return MTLVertexFormatHalf4;
	default: return MTLVertexFormatInvalid;
	}
}

static const MTLCompareFunction compareToMetal[] = {
	MTLCompareFunctionNever,
	MTLCompareFunctionLess,
	MTLCompareFunctionEqual,
	MTLCompareFunctionLessEqual,
	MTLCompareFunctionGreater,
	MTLCompareFunctionNotEqual,
	MTLCompareFunctionGreaterEqual,
	MTLCompareFunctionAlways,
};

static const MTLStencilOperation stencilOpToMetal[] = {
	MTLStencilOperationKeep,
	MTLStencilOperationZero,
	MTLStencilOperationReplace,
	MTLStencilOperationIncrementClamp,
	MTLStencilOperationDecrementClamp,
	MTLStencilOperationInvert,
	MTLStencilOperationIncrementWrap,
	MTLStencilOperationDecrementWrap,
};

static const MTLBlendOperation blendOpToMetal[] = {
	MTLBlendOperationAdd,
	MTLBlendOperationSubtract,
	MTLBlendOperationReverseSubtract,
	MTLBlendOperationMin,
	MTLBlendOperationMax,
};

static const MTLBlendFactor blendFactorToMetal[] = {
	MTLBlendFactorZero,
	MTLBlendFactorOne,
	MTLBlendFactorSourceColor,
	MTLBlendFactorOneMinusSourceColor,
	MTLBlendFactorDestinationColor,
	MTLBlendFactorOneMinusDestinationColor,
	MTLBlendFactorSourceAlpha,
	MTLBlendFactorOneMinusSourceAlpha,
	MTLBlendFactorDestinationAlpha,
	MTLBlendFactorOneMinusDestinationAlpha,
	MTLBlendFactorBlendColor,
	MTLBlendFactorOneMinusBlendColor,
	MTLBlendFactorBlendAlpha,
	MTLBlendFactorOneMinusBlendAlpha,
	MTLBlendFactorSource1Color,
	MTLBlendFactorOneMinusSource1Color,
	MTLBlendFactorSource1Alpha,
	MTLBlendFactorOneMinusSource1Alpha,
};

static const MTLPrimitiveType primToMetal[] = {
	MTLPrimitiveTypePoint,
	MTLPrimitiveTypeLine,
	MTLPrimitiveTypeLineStrip,
	MTLPrimitiveTypeTriangle,
	MTLPrimitiveTypeTriangleStrip,
	MTLPrimitiveTypeTriangle,  // Fans aren't supported.
};

static MTLSamplerAddressMode AddressModeToMetal(TextureAddressMode mode) {
	switch (mode) {
	case TextureAddressMode::REPEAT: return MTLSamplerAddressModeRepeat;
	case TextureAddressMode::REPEAT_MIRROR: return MTLSamplerAddressModeMirrorRepeat;
	case TextureAddressMode::CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
	case TextureAddressMode::CLAMP_TO_BORDER:
		if (@available(iOS 14.0, tvOS 14.0, *)) {
			return MTLSamplerAddressModeClampToBorderColor;
		}
		return MTLSamplerAddressModeClampToZero;
	}
	return MTLSamplerAddressModeClampToEdge;
}

static MTLClearColor ClearColorFromRGBA(uint32_t color) {
	float c[4];
	Uint8x4ToFloat4(c, color);
	return MTLClearColorMake(c[0], c[1], c[2], c[3]);
}

static MTLLoadAction RPActionToMetal(RPAction action) {
	switch (action) {
	case RPAction::CLEAR: return MTLLoadActionClear;
	case RPAction::DONT_CARE: return MTLLoadActionDontCare;
	case RPAction::KEEP:
	default:
		return MTLLoadActionLoad;
	}
}

class MetalBlendState : public BlendState {
public:
	BlendStateDesc desc;
};

class MetalDepthStencilState : public DepthStencilState {
public:
	DepthStencilStateDesc desc;
};

class MetalRasterState : public RasterState {
public:
	MTLCullMode cull = MTLCullModeNone;
	MTLWinding winding = MTLWindingCounterClockwise;
};

class MetalSamplerState : public SamplerState {
public:
	id<MTLSamplerState> sampler;
};

class MetalInputLayout : public InputLayout {
public:
	MTLVertexDescriptor *descriptor;
	int stride = 0;
};

class MetalShaderModule : public ShaderModule {
public:
	MetalShaderModule(ShaderStage stage, std::string_view tag) : stage_(stage), tag_(tag) {}
	ShaderStage GetStage() const override { return stage_; }

	id<MTLFunction> function;
	MetalTranslatedShader info;
	ShaderStage stage_;
	std::string tag_;
};

class MetalPipeline : public Pipeline {
public:
	~MetalPipeline() {
		if (vs) vs->Release();
		if (fs) fs->Release();
		delete[] ubo;
	}

	id<MTLRenderPipelineState> GetPSO(id<MTLDevice> device, const MetalRenderTargetFormat &format);

	MetalShaderModule *vs = nullptr;
	MetalShaderModule *fs = nullptr;
	AutoRef<MetalInputLayout> input;
	AutoRef<MetalBlendState> blend;
	AutoRef<MetalDepthStencilState> depthStencil;
	AutoRef<MetalRasterState> raster;
	MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
	uint8_t *ubo = nullptr;
	size_t uboSize = 0;
	std::string tag;
	std::unordered_map<uint32_t, id<MTLRenderPipelineState>> psos;
};

id<MTLRenderPipelineState> MetalPipeline::GetPSO(id<MTLDevice> device, const MetalRenderTargetFormat &format) {
	uint32_t key = format.Key();
	auto iter = psos.find(key);
	if (iter != psos.end()) {
		return iter->second;
	}

	MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
	desc.label = [NSString stringWithUTF8String:tag.c_str()];
	desc.vertexFunction = vs->function;
	desc.fragmentFunction = fs ? fs->function : nil;
	if (input) {
		desc.vertexDescriptor = input->descriptor;
	}
	desc.rasterSampleCount = format.sampleCount;
	desc.depthAttachmentPixelFormat = format.depth;
	desc.stencilAttachmentPixelFormat = format.stencil;
	if (format.color != MTLPixelFormatInvalid) {
		MTLRenderPipelineColorAttachmentDescriptor *att = desc.colorAttachments[0];
		att.pixelFormat = format.color;
		const BlendStateDesc &b = blend->desc;
		MTLColorWriteMask mask = MTLColorWriteMaskNone;
		if (b.colorMask & COLOR_MASK_R) mask |= MTLColorWriteMaskRed;
		if (b.colorMask & COLOR_MASK_G) mask |= MTLColorWriteMaskGreen;
		if (b.colorMask & COLOR_MASK_B) mask |= MTLColorWriteMaskBlue;
		if (b.colorMask & COLOR_MASK_A) mask |= MTLColorWriteMaskAlpha;
		att.writeMask = mask;
		att.blendingEnabled = b.enabled;
		if (b.enabled) {
			att.rgbBlendOperation = blendOpToMetal[(int)b.eqCol];
			att.alphaBlendOperation = blendOpToMetal[(int)b.eqAlpha];
			att.sourceRGBBlendFactor = blendFactorToMetal[(int)b.srcCol];
			att.destinationRGBBlendFactor = blendFactorToMetal[(int)b.dstCol];
			att.sourceAlphaBlendFactor = blendFactorToMetal[(int)b.srcAlpha];
			att.destinationAlphaBlendFactor = blendFactorToMetal[(int)b.dstAlpha];
		}
	}

	NSError *error = nil;
	id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
	if (!pso) {
		ERROR_LOG(Log::G3D, "Failed to create pipeline '%s' (color %s, depth %s): %s", tag.c_str(),
			MetalPixelFormatToString(format.color), MetalPixelFormatToString(format.depth),
			error ? [[error localizedDescription] UTF8String] : "?");
	}
	psos[key] = pso;
	return pso;
}

class MetalBuffer : public Buffer {
public:
	MetalBuffer(size_t size) : data(size) {}

	// Buffers are small and updated every frame where they're used at all, so they live in CPU
	// memory and get copied to the push buffer when (re)used. That makes updating them safe no matter
	// what the GPU is doing.
	void Get(MetalRenderManager *rm, id<MTLBuffer> *buffer, uint32_t *offset) {
		if (dirty || pushedFrame != rm->FrameNumber() || !pushed.buffer) {
			pushed = rm->Push().Push(data.data(), data.size(), 16);
			pushedFrame = rm->FrameNumber();
			dirty = false;
		}
		*buffer = pushed.buffer;
		*offset = pushed.offset;
	}

	std::vector<uint8_t> data;
	MetalPushAlloc pushed;
	uint64_t pushedFrame = 0;
	bool dirty = true;
};

class MetalTexture : public Texture {
public:
	MetalTexture(const TextureDesc &desc) {
		width_ = desc.width;
		height_ = desc.height;
		depth_ = desc.depth;
		format_ = desc.format;
		mipLevels_ = desc.mipLevels;
	}

	id<MTLTexture> texture;
	int mipLevels_ = 1;
	uint64_t lastUsedFrame = (uint64_t)-1;
};

class MetalFramebuffer : public Framebuffer {
public:
	MetalFramebuffer(const FramebufferDesc &desc) : tag_(desc.tag ? desc.tag : "") {
		width_ = desc.width;
		height_ = desc.height;
		layers_ = 1;
		multiSampleLevel_ = 0;
		hasDepthStencil_ = desc.z_stencil;
	}
	void UpdateTag(const char *tag) override {
		tag_ = tag;
		color.label = [NSString stringWithUTF8String:tag];
		if (depth) {
			depth.label = [NSString stringWithFormat:@"%s_depth", tag];
		}
	}
	const char *Tag() const override { return tag_.c_str(); }

	id<MTLTexture> StencilView() {
		if (!stencilView && depth) {
			stencilView = [depth newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
		}
		return stencilView;
	}

	id<MTLTexture> color;
	id<MTLTexture> depth;
	id<MTLTexture> stencilView;
	std::string tag_;
};

// POD, for memcmp.
struct MetalDepthStencilKey {
	DepthStencilStateDesc desc;
	uint8_t writeMask;
	uint8_t compareMask;

	bool operator < (const MetalDepthStencilKey &other) const {
		return memcmp(this, &other, sizeof(MetalDepthStencilKey)) < 0;
	}
};

class MetalDrawContext : public DrawContext {
public:
	MetalDrawContext(MetalRenderManager *renderManager);
	~MetalDrawContext();

	const DeviceCaps &GetDeviceCaps() const override { return caps_; }
	uint32_t GetSupportedShaderLanguages() const override { return (uint32_t)ShaderLanguage::GLSL_VULKAN; }
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;
	std::vector<std::string> GetDeviceList() const override { return { [[device_ name] UTF8String] }; }
	std::vector<std::string> GetFeatureList() const override;

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) override;

	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Texture *CreateTexture(const TextureDesc &desc) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;
	void UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) override;
	void UpdateTextureRegions(Texture *texture, int level, const TextureRegionUpdate *regions, int numRegions) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemory(Framebuffer *src, Aspect aspect, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) override;
	DataFormat PreferredFramebufferReadbackFormat(Framebuffer *src) override {
		return src ? DataFormat::R8G8B8A8_UNORM : DataFormat::B8G8R8A8_UNORM;
	}

	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect aspect, int layer) override;
	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;
	void InvalidateFramebuffer(FBInvalidationStage stage, Aspect aspects) override {}

	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewport(const Viewport &viewport) override;
	void SetBlendFactor(float color[4]) override {
		memcpy(blendFactor_, color, sizeof(blendFactor_));
	}
	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override {
		stencilRef_ = refValue;
		stencilWriteMask_ = writeMask;
		stencilCompareMask_ = compareMask;
	}

	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) override;
	void BindVertexBuffer(Buffer *vertexBuffer, int offset) override {
		curVBuffer_ = (MetalBuffer *)vertexBuffer;
		curVBufferOffset_ = offset;
	}
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override {
		curIBuffer_ = (MetalBuffer *)indexBuffer;
		curIBufferOffset_ = offset;
	}
	void BindNativeTexture(int sampler, void *nativeTexture) override;
	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	void Invalidate(InvalidationFlags flags) override;
	void BindPipeline(Pipeline *pipeline) override {
		curPipeline_ = (MetalPipeline *)pipeline;
	}

	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int indexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) override;
	void DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) override;

	void BeginFrame(DebugFlags debugFlags) override;
	void EndFrame() override;
	void Present(PresentMode presentMode) override;
	PresentMode GetCurrentPresentMode() const override { return presentMode_; }

	void Clear(Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) override;

	std::string GetInfoString(InfoField info) const override;
	uint64_t GetNativeObject(NativeObject obj, void *srcObject) override;
	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

	void SetInvalidationCallback(InvalidationCallback callback) override {
		invalidationCallback_ = callback;
	}
	int GetFrameCount() override { return frameCount_; }
	std::string GetGpuProfileString() const override;

private:
	bool ApplyState();
	void DrawIndexedInternal(id<MTLBuffer> ibuf, uint32_t ibufOffset, int indexCount);
	id<MTLDepthStencilState> GetDepthStencilState(const DepthStencilStateDesc &desc);
	void UploadTextureData(MetalTexture *tex, int level, int x, int y, int w, int h, const uint8_t *data, int srcStride, TextureCallback callback, bool useUploadBuffer);
	void ResetViewportScissor();

	MetalRenderManager *rm_;
	id<MTLDevice> device_;
	DeviceCaps caps_{};

	AutoRef<MetalPipeline> curPipeline_;
	MetalBuffer *curVBuffer_ = nullptr;
	int curVBufferOffset_ = 0;
	MetalBuffer *curIBuffer_ = nullptr;
	int curIBufferOffset_ = 0;

	id<MTLTexture> boundTextures_[MAX_TEXTURE_SLOTS];
	MetalTexture *boundThin3DTextures_[MAX_TEXTURE_SLOTS]{};
	id<MTLSamplerState> boundSamplers_[MAX_TEXTURE_SLOTS];
	id<MTLTexture> nullTexture_;
	id<MTLSamplerState> defaultSampler_;

	std::map<MetalDepthStencilKey, id<MTLDepthStencilState>> depthStencilCache_;

	float blendFactor_[4]{};
	uint8_t stencilRef_ = 0;
	uint8_t stencilWriteMask_ = 0xFF;
	uint8_t stencilCompareMask_ = 0xFF;

	Viewport viewport_{};
	int scissor_[4]{};

	// For knowing when the UBO needs to be resent.
	uint32_t uboGeneration_ = 0xFFFFFFFF;
	MetalPipeline *uboPipeline_ = nullptr;
	bool uboDirty_ = true;

	id<MTLBuffer> readbackBuffer_;

	InvalidationCallback invalidationCallback_;
	PresentMode presentMode_ = PresentMode::FIFO;
	int frameCount_ = FRAME_TIME_HISTORY_LENGTH;
};

MetalDrawContext::MetalDrawContext(MetalRenderManager *renderManager) : rm_(renderManager) {
	device_ = rm_->Device();

	caps_.coordConvention = CoordConvention::Vulkan;
	caps_.vendor = GPUVendor::VENDOR_APPLE;
	NSString *name = [device_ name];
	if ([name rangeOfString:@"AMD"].location != NSNotFound || [name rangeOfString:@"Radeon"].location != NSNotFound) {
		caps_.vendor = GPUVendor::VENDOR_AMD;
	} else if ([name rangeOfString:@"Intel"].location != NSNotFound) {
		caps_.vendor = GPUVendor::VENDOR_INTEL;
	} else if ([name rangeOfString:@"NVIDIA"].location != NSNotFound) {
		caps_.vendor = GPUVendor::VENDOR_NVIDIA;
	}
	caps_.deviceName = [name UTF8String];

	caps_.maxTextureSize = 16384;
	caps_.maxClipDistances = 8;
	caps_.maxCullDistances = 0;
	caps_.preferredDepthBufferFormat = DataFormat::D32F_S8;
	caps_.preferredShadowMapFormatLow = DataFormat::D16;
	caps_.preferredShadowMapFormatHigh = DataFormat::D32F;
	caps_.anisoSupported = true;
	caps_.depthRangeMinusOneToOne = false;
	caps_.dualSourceBlend = true;
	caps_.logicOpSupported = false;
	caps_.depthClampSupported = true;
	caps_.framebufferCopySupported = true;
	caps_.framebufferBlitSupported = false;
	caps_.framebufferDepthCopySupported = true;
	caps_.framebufferSeparateDepthCopySupported = false;
	caps_.framebufferDepthBlitSupported = false;
	caps_.framebufferStencilBlitSupported = false;
	caps_.framebufferFetchSupported = false;
	caps_.texture3DSupported = true;
	caps_.fragmentShaderFullPrecisionFloat = true;
	caps_.fragmentShaderInt32Supported = true;
	caps_.fragmentShaderDepthWriteSupported = true;
	caps_.fragmentShaderStencilWriteSupported = false;
	caps_.textureDepthSupported = true;
	caps_.blendMinMaxSupported = true;
	caps_.multiViewSupported = false;
	caps_.isTilingGPU = rm_->IsAppleGPU();
	caps_.sampleRateShadingSupported = false;
	caps_.setMaxFrameLatencySupported = true;
	caps_.textureSwizzleSupported = true;
	caps_.requiresHalfPixelOffset = false;
	caps_.provokingVertexLast = false;
	caps_.verySlowShaderCompiler = false;
	caps_.fullScreenExclusiveSupported = false;
	caps_.samplerLodControl = true;
	// Metal samplers have no LOD bias.
	caps_.samplerLodBias = false;
	caps_.clipPlanesSupported = 0;
	caps_.presentMaxInterval = 1;
	caps_.presentInstantModeChange = true;
#if PPSSPP_PLATFORM(MAC)
	caps_.presentModesSupported = PresentMode::FIFO | PresentMode::IMMEDIATE;
#else
	caps_.presentModesSupported = PresentMode::FIFO;
#endif
	caps_.multiSampleLevelsMask = 1;

	shaderLanguageDesc_.Init(GLSL_VULKAN);

	MTLTextureDescriptor *nullDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
	nullDesc.usage = MTLTextureUsageShaderRead;
	nullTexture_ = [device_ newTextureWithDescriptor:nullDesc];
	nullTexture_.label = @"null_texture";
	const uint32_t zero = 0;
	[nullTexture_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&zero bytesPerRow:4];

	MTLSamplerDescriptor *samplerDesc = [MTLSamplerDescriptor new];
	defaultSampler_ = [device_ newSamplerStateWithDescriptor:samplerDesc];

	for (int i = 0; i < (int)MAX_TEXTURE_SLOTS; i++) {
		boundTextures_[i] = nullTexture_;
		boundSamplers_[i] = defaultSampler_;
	}
}

MetalDrawContext::~MetalDrawContext() {
	DestroyPresets();
	rm_->WaitIdle();
	curPipeline_ = nullptr;
}

std::vector<std::string> MetalDrawContext::GetFeatureList() const {
	std::vector<std::string> features;
	features.push_back(StringFromFormat("Unified memory: %s", rm_->HasUnifiedMemory() ? "yes" : "no"));
	features.push_back(StringFromFormat("Apple GPU family: %s", rm_->IsAppleGPU() ? "yes" : "no"));
	if (@available(macOS 11.0, iOS 14.0, tvOS 14.0, *)) {
		features.push_back(StringFromFormat("BC texture compression: %s", device_.supportsBCTextureCompression ? "yes" : "no"));
	}
	return features;
}

uint32_t MetalDrawContext::GetDataFormatSupport(DataFormat fmt) const {
	MTLPixelFormat mtl = DataFormatToMetal(fmt);
	uint32_t support = 0;
	if (DataFormatToMetalVertex(fmt) != MTLVertexFormatInvalid) {
		support |= FMT_INPUTLAYOUT;
	}
	if (mtl == MTLPixelFormatInvalid) {
		return support;
	}
	if (DataFormatIsDepthStencil(fmt)) {
		return support | FMT_DEPTHSTENCIL | FMT_TEXTURE;
	}
	int blockSize = 0;
	if (DataFormatIsBlockCompressed(fmt, &blockSize)) {
		bool supported = false;
		switch (fmt) {
		case DataFormat::BC1_RGBA_UNORM_BLOCK:
		case DataFormat::BC2_UNORM_BLOCK:
		case DataFormat::BC3_UNORM_BLOCK:
		case DataFormat::BC4_UNORM_BLOCK:
		case DataFormat::BC5_UNORM_BLOCK:
		case DataFormat::BC7_UNORM_BLOCK:
			if (@available(macOS 11.0, iOS 16.4, tvOS 16.4, *)) {
				supported = device_.supportsBCTextureCompression;
			}
			break;
		default:
			// ETC2 and ASTC.
			supported = rm_->IsAppleGPU();
			break;
		}
		return supported ? (support | FMT_TEXTURE) : support;
	}
	support |= FMT_TEXTURE | FMT_BLIT;
	switch (fmt) {
	case DataFormat::R8G8B8A8_UNORM:
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::R8G8B8A8_UNORM_SRGB:
	case DataFormat::B8G8R8A8_UNORM_SRGB:
	case DataFormat::R16G16B16A16_FLOAT:
	case DataFormat::R8_UNORM:
	case DataFormat::R16_FLOAT:
	case DataFormat::R32_FLOAT:
		support |= FMT_RENDERTARGET | FMT_AUTOGEN_MIPS;
		break;
	default:
		break;
	}
	return support;
}

DepthStencilState *MetalDrawContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	MetalDepthStencilState *dss = new MetalDepthStencilState();
	dss->desc = desc;
	return dss;
}

BlendState *MetalDrawContext::CreateBlendState(const BlendStateDesc &desc) {
	MetalBlendState *bs = new MetalBlendState();
	bs->desc = desc;
	return bs;
}

RasterState *MetalDrawContext::CreateRasterState(const RasterStateDesc &desc) {
	MetalRasterState *rs = new MetalRasterState();
	switch (desc.cull) {
	case CullMode::FRONT: rs->cull = MTLCullModeFront; break;
	case CullMode::BACK: rs->cull = MTLCullModeBack; break;
	default: rs->cull = MTLCullModeNone; break;
	}
	rs->winding = desc.frontFace == Facing::CCW ? MTLWindingCounterClockwise : MTLWindingClockwise;
	return rs;
}

SamplerState *MetalDrawContext::CreateSamplerState(const SamplerStateDesc &desc) {
	MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
	sd.magFilter = desc.magFilter == TextureFilter::LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	sd.minFilter = desc.minFilter == TextureFilter::LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	sd.mipFilter = desc.mipFilter == TextureFilter::LINEAR ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
	sd.sAddressMode = AddressModeToMetal(desc.wrapU);
	sd.tAddressMode = AddressModeToMetal(desc.wrapV);
	sd.rAddressMode = AddressModeToMetal(desc.wrapW);
	sd.maxAnisotropy = std::clamp((int)desc.maxAniso, 1, 16);
	if (desc.shadowCompareEnabled) {
		sd.compareFunction = compareToMetal[(int)desc.shadowCompareFunc];
	}
	if (@available(iOS 14.0, tvOS 14.0, *)) {
		switch (desc.borderColor) {
		case BorderColor::OPAQUE_BLACK: sd.borderColor = MTLSamplerBorderColorOpaqueBlack; break;
		case BorderColor::OPAQUE_WHITE: sd.borderColor = MTLSamplerBorderColorOpaqueWhite; break;
		default: sd.borderColor = MTLSamplerBorderColorTransparentBlack; break;
		}
	}
	sd.lodMinClamp = 0.0f;
	sd.lodMaxClamp = FLT_MAX;
	MetalSamplerState *ss = new MetalSamplerState();
	ss->sampler = [device_ newSamplerStateWithDescriptor:sd];
	return ss;
}

InputLayout *MetalDrawContext::CreateInputLayout(const InputLayoutDesc &desc) {
	MetalInputLayout *layout = new MetalInputLayout();
	MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
	for (const AttributeDesc &attr : desc.attributes) {
		vd.attributes[attr.location].format = DataFormatToMetalVertex(attr.format);
		vd.attributes[attr.location].offset = attr.offset;
		vd.attributes[attr.location].bufferIndex = METAL_VERTEX_BUFFER_INDEX;
	}
	vd.layouts[METAL_VERTEX_BUFFER_INDEX].stride = desc.stride;
	vd.layouts[METAL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
	layout->descriptor = vd;
	layout->stride = desc.stride;
	return layout;
}

ShaderModule *MetalDrawContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) {
	if (language != GLSL_VULKAN) {
		ERROR_LOG(Log::G3D, "Metal: Unsupported shader language %s", ShaderLanguageAsString(language));
		return nullptr;
	}
	std::string source((const char *)data, dataSize);
	MetalShaderModule *module = new MetalShaderModule(stage, tag);
	std::string errorMessage;
	if (!TranslateVulkanGLSLToMSL(stage, source.c_str(), &module->info, &errorMessage)) {
		ERROR_LOG(Log::G3D, "Failed to translate shader %s: %s\n%s", tag, errorMessage.c_str(), LineNumberString(source).c_str());
		module->Release();
		return nullptr;
	}
	module->function = CreateMetalFunction(device_, module->info, tag, &errorMessage);
	if (!module->function) {
		ERROR_LOG(Log::G3D, "%s\n%s", errorMessage.c_str(), LineNumberString(module->info.msl).c_str());
		module->Release();
		return nullptr;
	}
	return module;
}

Pipeline *MetalDrawContext::CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) {
	MetalPipeline *pipeline = new MetalPipeline();
	pipeline->tag = tag;
	for (ShaderModule *module : desc.shaders) {
		MetalShaderModule *mm = (MetalShaderModule *)module;
		mm->AddRef();
		if (mm->GetStage() == ShaderStage::Vertex) {
			pipeline->vs = mm;
		} else if (mm->GetStage() == ShaderStage::Fragment) {
			pipeline->fs = mm;
		} else {
			mm->Release();
		}
	}
	if (!pipeline->vs) {
		ERROR_LOG(Log::G3D, "Pipeline %s lacks a vertex shader", tag);
		pipeline->Release();
		return nullptr;
	}
	pipeline->input = (MetalInputLayout *)desc.inputLayout;
	pipeline->blend = (MetalBlendState *)desc.blend;
	pipeline->depthStencil = (MetalDepthStencilState *)desc.depthStencil;
	pipeline->raster = (MetalRasterState *)desc.raster;
	pipeline->prim = primToMetal[(int)desc.prim];
	if (desc.uniformDesc) {
		pipeline->uboSize = desc.uniformDesc->uniformBufferSize;
		pipeline->ubo = new uint8_t[pipeline->uboSize]{};
	}
	// Create the pipeline state for the most likely target up front, to catch errors early.
	MetalRenderTargetFormat fmt;
	fmt.color = MTLPixelFormatRGBA8Unorm;
	fmt.depth = MTLPixelFormatDepth32Float_Stencil8;
	fmt.stencil = MTLPixelFormatDepth32Float_Stencil8;
	if (!pipeline->GetPSO(device_, fmt)) {
		pipeline->Release();
		return nullptr;
	}
	return pipeline;
}

Buffer *MetalDrawContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new MetalBuffer(size);
}

void MetalDrawContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	MetalBuffer *buf = (MetalBuffer *)buffer;
	_dbg_assert_(offset + size <= buf->data.size());
	memcpy(buf->data.data() + offset, data, size);
	buf->dirty = true;
}

static void GetLevelLayout(DataFormat format, int w, int h, int *bytesPerRow, int *rows) {
	int blockSize = 0;
	if (DataFormatIsBlockCompressed(format, &blockSize)) {
		*bytesPerRow = ((w + 3) / 4) * blockSize;
		*rows = (h + 3) / 4;
	} else {
		*bytesPerRow = w * (int)DataFormatSizeInBytes(format);
		*rows = h;
	}
}

void MetalDrawContext::UploadTextureData(MetalTexture *tex, int level, int x, int y, int w, int h, const uint8_t *data, int srcStride, TextureCallback callback, bool useUploadBuffer) {
	int bytesPerRow, rows;
	GetLevelLayout(tex->Format(), w, h, &bytesPerRow, &rows);
	MetalPushAlloc alloc = rm_->Push().Allocate(bytesPerRow * rows, 16);
	if (!callback || !callback(alloc.ptr, data, w, h, 1, bytesPerRow, bytesPerRow * rows)) {
		if (!srcStride || srcStride == bytesPerRow) {
			memcpy(alloc.ptr, data, bytesPerRow * rows);
		} else {
			for (int row = 0; row < rows; row++) {
				memcpy(alloc.ptr + row * bytesPerRow, data + row * srcStride, bytesPerRow);
			}
		}
	}
	id<MTLBlitCommandEncoder> blit = useUploadBuffer ? rm_->UploadEncoder() : rm_->BlitEncoder();
	[blit copyFromBuffer:alloc.buffer sourceOffset:alloc.offset sourceBytesPerRow:bytesPerRow sourceBytesPerImage:bytesPerRow * rows sourceSize:MTLSizeMake(w, h, 1)
		toTexture:tex->texture destinationSlice:0 destinationLevel:level destinationOrigin:MTLOriginMake(x, y, 0)];
}

Texture *MetalDrawContext::CreateTexture(const TextureDesc &desc) {
	MTLPixelFormat format = DataFormatToMetal(desc.format);
	if (format == MTLPixelFormatInvalid || !(GetDataFormatSupport(desc.format) & FMT_TEXTURE)) {
		ERROR_LOG(Log::G3D, "Metal: Unsupported texture format %s", DataFormatToString(desc.format));
		return nullptr;
	}
	if (desc.type != TextureType::LINEAR2D) {
		// TODO: 3D textures. Not used by the UI.
		ERROR_LOG(Log::G3D, "Metal: Unsupported texture type %d", (int)desc.type);
		return nullptr;
	}
	MetalTexture *tex = new MetalTexture(desc);
	bool generateMips = desc.generateMips && (GetDataFormatSupport(desc.format) & FMT_AUTOGEN_MIPS);
	int mipLevels = std::max(1, desc.mipLevels);
	if (desc.generateMips && mipLevels == 1) {
		mipLevels = 1;
		int dim = std::max(desc.width, desc.height);
		while (dim > 1) {
			dim >>= 1;
			mipLevels++;
		}
	}
	tex->mipLevels_ = mipLevels;

	MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:desc.width height:desc.height mipmapped:NO];
	td.mipmapLevelCount = mipLevels;
	td.usage = MTLTextureUsageShaderRead;
	td.storageMode = MTLStorageModePrivate;
	switch (desc.swizzle) {
	case TextureSwizzle::R8_AS_ALPHA:
		td.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleOne, MTLTextureSwizzleOne, MTLTextureSwizzleOne, MTLTextureSwizzleRed);
		break;
	case TextureSwizzle::R8_AS_GRAYSCALE:
		td.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleOne);
		break;
	case TextureSwizzle::R8_AS_PREMUL_ALPHA:
		td.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed);
		break;
	default:
		break;
	}
	tex->texture = [device_ newTextureWithDescriptor:td];
	if (!tex->texture) {
		tex->Release();
		return nullptr;
	}
	if (desc.tag) {
		tex->texture.label = [NSString stringWithUTF8String:desc.tag];
	}

	int w = desc.width;
	int h = desc.height;
	for (int i = 0; i < (int)desc.initData.size(); i++) {
		UploadTextureData(tex, i, 0, 0, w, h, desc.initData[i], 0, desc.initDataCallback, true);
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
		if (generateMips) {
			break;
		}
	}
	if (generateMips && !desc.initData.empty()) {
		[rm_->UploadEncoder() generateMipmapsForTexture:tex->texture];
	}
	return tex;
}

void MetalDrawContext::UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) {
	MetalTexture *tex = (MetalTexture *)texture;
	// If the texture has been drawn with this frame, the update has to go in order with the draws.
	bool useUpload = tex->lastUsedFrame != rm_->FrameNumber();
	int w = tex->Width();
	int h = tex->Height();
	for (int i = 0; i < numLevels; i++) {
		UploadTextureData(tex, i, 0, 0, w, h, data[i], 0, initDataCallback, useUpload);
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
	}
}

void MetalDrawContext::UpdateTextureRegions(Texture *texture, int level, const TextureRegionUpdate *regions, int numRegions) {
	MetalTexture *tex = (MetalTexture *)texture;
	bool useUpload = tex->lastUsedFrame != rm_->FrameNumber();
	for (int i = 0; i < numRegions; i++) {
		const TextureRegionUpdate &r = regions[i];
		UploadTextureData(tex, level, r.x, r.y, r.w, r.h, r.data, r.byteStride, nullptr, useUpload);
	}
}

Framebuffer *MetalDrawContext::CreateFramebuffer(const FramebufferDesc &desc) {
	_dbg_assert_(desc.numLayers == 1);
	MetalFramebuffer *fb = new MetalFramebuffer(desc);
	MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:desc.width height:desc.height mipmapped:NO];
	td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	td.storageMode = MTLStorageModePrivate;
	fb->color = [device_ newTextureWithDescriptor:td];
	if (!fb->color) {
		fb->Release();
		return nullptr;
	}
	if (desc.z_stencil) {
		MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:desc.width height:desc.height mipmapped:NO];
		dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
		dd.storageMode = MTLStorageModePrivate;
		fb->depth = [device_ newTextureWithDescriptor:dd];
		if (!fb->depth) {
			fb->Release();
			return nullptr;
		}
	}
	if (desc.tag) {
		fb->UpdateTag(desc.tag);
	}
	return fb;
}

void MetalDrawContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) {
	MetalFramebuffer *src = (MetalFramebuffer *)srcfb;
	MetalFramebuffer *dst = (MetalFramebuffer *)dstfb;

	// Clip the source rectangle.
	if (x < 0) {
		width += x;
		dstX -= x;
		x = 0;
	}
	if (y < 0) {
		height += y;
		dstY -= y;
		y = 0;
	}
	if (dstX < 0) {
		width += dstX;
		x -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		height += dstY;
		y -= dstY;
		dstY = 0;
	}
	width = std::min(width, std::min(src->Width() - x, dst->Width() - dstX));
	height = std::min(height, std::min(src->Height() - y, dst->Height() - dstY));
	if (width <= 0 || height <= 0) {
		return;
	}

	id<MTLBlitCommandEncoder> blit = rm_->BlitEncoder();
	if (aspects & Aspect::COLOR_BIT) {
		[blit copyFromTexture:src->color sourceSlice:0 sourceLevel:level sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(width, height, 1)
			toTexture:dst->color destinationSlice:0 destinationLevel:dstLevel destinationOrigin:MTLOriginMake(dstX, dstY, 0)];
	}
	if ((aspects & (Aspect::DEPTH_BIT | Aspect::STENCIL_BIT)) && src->depth && dst->depth) {
		[blit copyFromTexture:src->depth sourceSlice:0 sourceLevel:level sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(width, height, 1)
			toTexture:dst->depth destinationSlice:0 destinationLevel:dstLevel destinationOrigin:MTLOriginMake(dstX, dstY, 0)];
	}
}

bool MetalDrawContext::BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) {
	// Not supported (framebufferBlitSupported is false). Metal blits can't scale.
	return false;
}

bool MetalDrawContext::CopyFramebufferToMemory(Framebuffer *srcfb, Aspect aspect, int x, int y, int w, int h, Draw::DataFormat destFormat, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) {
	MetalFramebuffer *fb = (MetalFramebuffer *)srcfb;
	id<MTLTexture> srcTex;
	if (aspect == Aspect::COLOR_BIT) {
		srcTex = fb ? fb->color : rm_->BackbufferTexture();
	} else if (aspect == Aspect::DEPTH_BIT || aspect == Aspect::STENCIL_BIT) {
		srcTex = fb ? fb->depth : nil;
	}
	if (!srcTex) {
		return false;
	}

	int texW = (int)srcTex.width;
	int texH = (int)srcTex.height;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	w = std::min(w, texW - x);
	h = std::min(h, texH - y);
	if (w <= 0 || h <= 0) {
		return true;
	}

	int srcPixelSize = 4;
	MTLBlitOption options = MTLBlitOptionNone;
	if (aspect == Aspect::DEPTH_BIT) {
		options = MTLBlitOptionDepthFromDepthStencil;
	} else if (aspect == Aspect::STENCIL_BIT) {
		options = MTLBlitOptionStencilFromDepthStencil;
		srcPixelSize = 1;
	}
	int srcStride = w * srcPixelSize;
	size_t size = (size_t)srcStride * h;
	if (!readbackBuffer_ || readbackBuffer_.length < size) {
		readbackBuffer_ = [device_ newBufferWithLength:std::max(size, (size_t)(1024 * 1024)) options:MTLResourceStorageModeShared];
		readbackBuffer_.label = @"readback";
	}

	id<MTLBlitCommandEncoder> blit = rm_->BlitEncoder();
	[blit copyFromTexture:srcTex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(w, h, 1)
		toBuffer:readbackBuffer_ destinationOffset:0 destinationBytesPerRow:srcStride destinationBytesPerImage:size options:options];
	rm_->Flush(true);

	const uint8_t *src = (const uint8_t *)readbackBuffer_.contents;
	switch (aspect) {
	case Aspect::COLOR_BIT:
		if (srcTex.pixelFormat == MTLPixelFormatBGRA8Unorm) {
			ConvertFromBGRA8888((uint8_t *)pixels, src, pixelStride, w, w, h, destFormat);
		} else {
			ConvertFromRGBA8888((uint8_t *)pixels, src, pixelStride, w, w, h, destFormat);
		}
		break;
	case Aspect::DEPTH_BIT:
		if (destFormat == DataFormat::D32F) {
			ConvertToD32F((uint8_t *)pixels, src, pixelStride, w, w, h, DataFormat::D32F);
		} else if (destFormat == DataFormat::D16) {
			ConvertToD16((uint8_t *)pixels, src, pixelStride, w, w, h, DataFormat::D32F);
		} else {
			_dbg_assert_(false);
			return false;
		}
		break;
	case Aspect::STENCIL_BIT:
		_dbg_assert_(destFormat == DataFormat::S8);
		for (int row = 0; row < h; row++) {
			memcpy((uint8_t *)pixels + row * pixelStride, src + row * srcStride, w);
		}
		break;
	default:
		return false;
	}
	return true;
}

void MetalDrawContext::ResetViewportScissor() {
	int w = rm_->CurPassWidth();
	int h = rm_->CurPassHeight();
	viewport_ = Viewport{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
	scissor_[0] = 0;
	scissor_[1] = 0;
	scissor_[2] = w;
	scissor_[3] = h;
}

void MetalDrawContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) {
	MetalFramebuffer *fb = (MetalFramebuffer *)fbo;
	if (fb) {
		MetalRenderPassDesc desc{};
		desc.color = fb->color;
		desc.depth = fb->depth;
		desc.colorLoad = RPActionToMetal(rp.color);
		desc.depthLoad = RPActionToMetal(rp.depth);
		desc.stencilLoad = RPActionToMetal(rp.stencil);
		desc.clearColor = ClearColorFromRGBA(rp.clearColor);
		desc.clearDepth = rp.clearDepth;
		desc.clearStencil = rp.clearStencil;
		desc.tag = tag;
		rm_->BindRenderPass(desc);
	} else {
		rm_->BindBackbufferPass(RPActionToMetal(rp.color), ClearColorFromRGBA(rp.clearColor), tag);
	}
	ResetViewportScissor();
	if (invalidationCallback_) {
		invalidationCallback_(InvalidationCallbackFlags::RENDER_PASS_STATE);
	}
}

void MetalDrawContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect aspect, int layer) {
	_dbg_assert_(binding < (int)MAX_TEXTURE_SLOTS);
	MetalFramebuffer *fb = (MetalFramebuffer *)fbo;
	id<MTLTexture> tex;
	switch (aspect) {
	case Aspect::COLOR_BIT: tex = fb->color; break;
	case Aspect::DEPTH_BIT: tex = fb->depth; break;
	case Aspect::STENCIL_BIT: tex = fb->StencilView(); break;
	default: break;
	}
	boundTextures_[binding] = tex ? tex : nullTexture_;
	boundThin3DTextures_[binding] = nullptr;
}

void MetalDrawContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	if (fbo) {
		*w = fbo->Width();
		*h = fbo->Height();
	} else {
		*w = rm_->BackbufferWidth();
		*h = rm_->BackbufferHeight();
	}
}

void MetalDrawContext::SetScissorRect(int left, int top, int width, int height) {
	scissor_[0] = left;
	scissor_[1] = top;
	scissor_[2] = width;
	scissor_[3] = height;
}

void MetalDrawContext::SetViewport(const Viewport &viewport) {
	viewport_ = viewport;
}

void MetalDrawContext::BindSamplerStates(int start, int count, SamplerState **states) {
	_dbg_assert_(start + count <= (int)MAX_TEXTURE_SLOTS);
	for (int i = 0; i < count; i++) {
		MetalSamplerState *s = (MetalSamplerState *)states[i];
		boundSamplers_[start + i] = s ? s->sampler : defaultSampler_;
	}
}

void MetalDrawContext::BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) {
	_dbg_assert_(start + count <= (int)MAX_TEXTURE_SLOTS);
	for (int i = 0; i < count; i++) {
		MetalTexture *tex = (MetalTexture *)textures[i];
		boundTextures_[start + i] = tex ? tex->texture : nullTexture_;
		boundThin3DTextures_[start + i] = tex;
	}
}

void MetalDrawContext::BindNativeTexture(int sampler, void *nativeTexture) {
	_dbg_assert_(sampler < (int)MAX_TEXTURE_SLOTS);
	id<MTLTexture> tex = (__bridge id<MTLTexture>)nativeTexture;
	boundTextures_[sampler] = tex ? tex : nullTexture_;
	boundThin3DTextures_[sampler] = nullptr;
}

void MetalDrawContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	_dbg_assert_(curPipeline_ && size <= curPipeline_->uboSize);
	memcpy(curPipeline_->ubo, ub, size);
	uboDirty_ = true;
}

void MetalDrawContext::Invalidate(InvalidationFlags flags) {
	if (flags & InvalidationFlags::CACHED_RENDER_STATE) {
		uboDirty_ = true;
		uboPipeline_ = nullptr;
	}
}

id<MTLDepthStencilState> MetalDrawContext::GetDepthStencilState(const DepthStencilStateDesc &desc) {
	MetalDepthStencilKey key{};
	key.desc = desc;
	key.writeMask = desc.stencilEnabled ? stencilWriteMask_ : 0;
	key.compareMask = desc.stencilEnabled ? stencilCompareMask_ : 0;
	auto iter = depthStencilCache_.find(key);
	if (iter != depthStencilCache_.end()) {
		return iter->second;
	}
	MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
	dd.depthCompareFunction = desc.depthTestEnabled ? compareToMetal[(int)desc.depthCompare] : MTLCompareFunctionAlways;
	dd.depthWriteEnabled = desc.depthTestEnabled && desc.depthWriteEnabled;
	if (desc.stencilEnabled) {
		MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
		sd.stencilCompareFunction = compareToMetal[(int)desc.stencil.compareOp];
		sd.stencilFailureOperation = stencilOpToMetal[(int)desc.stencil.failOp];
		sd.depthFailureOperation = stencilOpToMetal[(int)desc.stencil.depthFailOp];
		sd.depthStencilPassOperation = stencilOpToMetal[(int)desc.stencil.passOp];
		sd.readMask = stencilCompareMask_;
		sd.writeMask = stencilWriteMask_;
		dd.frontFaceStencil = sd;
		dd.backFaceStencil = sd;
	}
	id<MTLDepthStencilState> dss = [device_ newDepthStencilStateWithDescriptor:dd];
	depthStencilCache_[key] = dss;
	return dss;
}

bool MetalDrawContext::ApplyState() {
	if (!curPipeline_) {
		return false;
	}
	id<MTLRenderCommandEncoder> enc = rm_->RenderEncoder();
	if (!enc) {
		return false;
	}

	id<MTLRenderPipelineState> pso = curPipeline_->GetPSO(device_, rm_->CurTargetFormat());
	if (!pso) {
		return false;
	}
	rm_->SetPipeline(pso);
	const DepthStencilStateDesc &dsDesc = curPipeline_->depthStencil->desc;
	rm_->SetDepthStencilState(GetDepthStencilState(dsDesc));
	if (dsDesc.stencilEnabled) {
		rm_->SetStencilRef(stencilRef_);
	}
	rm_->SetBlendColor(blendFactor_[0], blendFactor_[1], blendFactor_[2], blendFactor_[3]);
	rm_->SetCullMode(curPipeline_->raster->cull);
	rm_->SetFrontFacing(curPipeline_->raster->winding);
	rm_->SetDepthClip(MTLDepthClipModeClip);

	MTLViewport vp{ viewport_.TopLeftX, viewport_.TopLeftY, viewport_.Width, viewport_.Height, viewport_.MinDepth, viewport_.MaxDepth };
	rm_->SetViewport(vp);
	if (!rm_->SetScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3])) {
		return false;
	}

	if (curPipeline_->fs) {
		const MetalTranslatedShader &info = curPipeline_->fs->info;
		for (int i = 0; i < (int)MAX_TEXTURE_SLOTS; i++) {
			int index = i + THIN3D_TEXTURE_BINDING_BASE;
			if (!(info.textureMask & (1 << index))) {
				continue;
			}
			id<MTLTexture> tex = boundTextures_[i];
			if (info.arrayTextureMask & (1 << index)) {
				tex = rm_->ArrayView(tex);
			}
			rm_->SetFragmentTexture(tex, index);
			rm_->SetFragmentSampler(boundSamplers_[i], index);
			if (boundThin3DTextures_[i]) {
				boundThin3DTextures_[i]->lastUsedFrame = rm_->FrameNumber();
			}
		}
	}

	if (curPipeline_->uboSize) {
		uint32_t gen = rm_->EncoderGeneration();
		if (uboDirty_ || uboPipeline_ != curPipeline_.ptr || uboGeneration_ != gen) {
			rm_->SetVertexBytes(curPipeline_->ubo, curPipeline_->uboSize, 0);
			rm_->SetFragmentBytes(curPipeline_->ubo, curPipeline_->uboSize, 0);
			uboDirty_ = false;
			uboPipeline_ = curPipeline_.ptr;
			uboGeneration_ = gen;
		}
	}
	return true;
}

void MetalDrawContext::Draw(int vertexCount, int offset) {
	if (!ApplyState()) {
		return;
	}
	if (curVBuffer_) {
		id<MTLBuffer> buf;
		uint32_t bufOffset;
		curVBuffer_->Get(rm_, &buf, &bufOffset);
		rm_->SetVertexBuffer(buf, bufOffset + curVBufferOffset_, METAL_VERTEX_BUFFER_INDEX);
	}
	[rm_->RenderEncoder() drawPrimitives:curPipeline_->prim vertexStart:offset vertexCount:vertexCount];
}

void MetalDrawContext::DrawIndexedInternal(id<MTLBuffer> ibuf, uint32_t ibufOffset, int indexCount) {
	if (ibufOffset & 3) {
		// Metal wants aligned index buffer offsets. Copy the indices to somewhere that is.
		MetalPushAlloc alloc = rm_->Push().Push((const uint8_t *)ibuf.contents + ibufOffset, indexCount * sizeof(uint16_t), 4);
		ibuf = alloc.buffer;
		ibufOffset = alloc.offset;
	}
	[rm_->RenderEncoder() drawIndexedPrimitives:curPipeline_->prim indexCount:indexCount indexType:MTLIndexTypeUInt16 indexBuffer:ibuf indexBufferOffset:ibufOffset];
}

void MetalDrawContext::DrawIndexed(int indexCount, int offset) {
	if (!curIBuffer_ || !ApplyState()) {
		return;
	}
	if (curVBuffer_) {
		id<MTLBuffer> buf;
		uint32_t bufOffset;
		curVBuffer_->Get(rm_, &buf, &bufOffset);
		rm_->SetVertexBuffer(buf, bufOffset + curVBufferOffset_, METAL_VERTEX_BUFFER_INDEX);
	}
	id<MTLBuffer> ibuf;
	uint32_t ibufOffset;
	curIBuffer_->Get(rm_, &ibuf, &ibufOffset);
	DrawIndexedInternal(ibuf, ibufOffset + curIBufferOffset_ + offset * sizeof(uint16_t), indexCount);
}

void MetalDrawContext::DrawUP(const void *vdata, int vertexCount) {
	if (!vertexCount || !ApplyState()) {
		return;
	}
	MetalPushAlloc alloc = rm_->Push().Push(vdata, vertexCount * curPipeline_->input->stride, 16);
	rm_->SetVertexBuffer(alloc.buffer, alloc.offset, METAL_VERTEX_BUFFER_INDEX);
	[rm_->RenderEncoder() drawPrimitives:curPipeline_->prim vertexStart:0 vertexCount:vertexCount];
}

void MetalDrawContext::DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) {
	if (!indexCount || !ApplyState()) {
		return;
	}
	MetalPushAlloc valloc = rm_->Push().Push(vdata, vertexCount * curPipeline_->input->stride, 16);
	rm_->SetVertexBuffer(valloc.buffer, valloc.offset, METAL_VERTEX_BUFFER_INDEX);
	MetalPushAlloc ialloc = rm_->Push().Push(idata, indexCount * sizeof(uint16_t), 4);
	DrawIndexedInternal(ialloc.buffer, ialloc.offset, indexCount);
}

void MetalDrawContext::DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) {
	if (draws.is_empty() || !vertexCount || !indexCount) {
		return;
	}
	BindPipeline(draws[0].pipeline);
	MetalPushAlloc valloc = rm_->Push().Push(vdata, vertexCount * curPipeline_->input->stride, 16);
	MetalPushAlloc ialloc = rm_->Push().Push(idata, indexCount * sizeof(uint16_t), 4);

	for (size_t i = 0; i < draws.size(); i++) {
		const ClippedDraw &draw = draws[i];
		BindPipeline(draw.pipeline);
		UpdateDynamicUniformBuffer(ub, ubSize);
		if (draw.bindTexture) {
			BindTexture(0, draw.bindTexture);
		} else if (draw.bindFramebufferAsTex) {
			BindFramebufferAsTexture(draw.bindFramebufferAsTex, 0, draw.aspect, 0);
		} else if (draw.bindNativeTexture) {
			BindNativeTexture(0, draw.bindNativeTexture);
		}
		SamplerState *sampler = draw.samplerState;
		BindSamplerStates(0, 1, &sampler);
		SetScissorRect(draw.clipx, draw.clipy, draw.clipw, draw.cliph);
		if (!ApplyState()) {
			continue;
		}
		rm_->SetVertexBuffer(valloc.buffer, valloc.offset, METAL_VERTEX_BUFFER_INDEX);
		DrawIndexedInternal(ialloc.buffer, ialloc.offset + draw.indexOffset * sizeof(uint16_t), draw.indexCount);
	}
}

void MetalDrawContext::Clear(Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) {
	rm_->Clear((aspects & Aspect::COLOR_BIT) != 0, (aspects & Aspect::DEPTH_BIT) != 0, (aspects & Aspect::STENCIL_BIT) != 0,
		ClearColorFromRGBA(colorval), depthVal, stencilVal);
}

void MetalDrawContext::BeginFrame(DebugFlags debugFlags) {
	FrameTimeData &frameTimeData = frameTimeHistory_.Add(frameCount_);
	frameTimeData.frameBegin = time_now_d();
	rm_->BeginFrame();
	frameTimeData.afterFenceWait = time_now_d();
}

void MetalDrawContext::EndFrame() {
	rm_->EndFrame();
	frameTimeHistory_[frameCount_].firstSubmit = time_now_d();
	curPipeline_ = nullptr;
	curVBuffer_ = nullptr;
	curIBuffer_ = nullptr;
	for (int i = 0; i < (int)MAX_TEXTURE_SLOTS; i++) {
		boundTextures_[i] = nullTexture_;
		boundThin3DTextures_[i] = nullptr;
	}
	uboPipeline_ = nullptr;
}

void MetalDrawContext::Present(PresentMode presentMode) {
	frameTimeHistory_[frameCount_].queuePresent = time_now_d();
	presentMode_ = presentMode;
	rm_->Present(presentMode == PresentMode::FIFO);
	rm_->UnbindRenderPass();
	frameCount_++;
}

std::string MetalDrawContext::GetInfoString(InfoField info) const {
	switch (info) {
	case InfoField::APINAME: return "Metal";
	case InfoField::APIVERSION:
		if (@available(macOS 13.0, iOS 16.0, tvOS 16.0, *)) {
			if ([device_ supportsFamily:MTLGPUFamilyMetal3]) {
				return "Metal 3";
			}
		}
		return "Metal 2";
	case InfoField::VENDORSTRING: return [[device_ name] UTF8String];
	case InfoField::VENDOR: return "";
	case InfoField::DRIVER: return "-";
	case InfoField::SHADELANGVERSION:
		if (@available(macOS 11.0, iOS 14.0, tvOS 14.0, *)) {
			return "MSL 2.3";
		}
		return "MSL 2.1";
	default: return "?";
	}
}

uint64_t MetalDrawContext::GetNativeObject(NativeObject obj, void *srcObject) {
	switch (obj) {
	case NativeObject::DEVICE: return (uint64_t)(uintptr_t)(__bridge void *)device_;
	case NativeObject::CONTEXT: return (uint64_t)(uintptr_t)(__bridge void *)rm_->Queue();
	case NativeObject::RENDER_MANAGER: return (uint64_t)(uintptr_t)rm_;
	case NativeObject::TEXTURE_VIEW: return (uint64_t)(uintptr_t)(__bridge void *)((MetalTexture *)srcObject)->texture;
	case NativeObject::NULL_IMAGEVIEW: return (uint64_t)(uintptr_t)(__bridge void *)nullTexture_;
	default: return 0;
	}
}

void MetalDrawContext::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	switch (ev) {
	case Event::LOST_BACKBUFFER:
		if (rm_->IsBackbufferPass()) {
			rm_->UnbindRenderPass();
		}
		break;
	case Event::GOT_BACKBUFFER:
		rm_->Resize(width, height);
		break;
	default:
		break;
	}
}

std::string MetalDrawContext::GetGpuProfileString() const {
	const MetalStats &stats = rm_->Stats();
	return StringFromFormat("Render passes: %d\nBlit passes: %d\nFlushes: %d\n", stats.renderPasses, stats.blitPasses, stats.flushes);
}

DrawContext *T3DCreateMetalContext(MetalRenderManager *renderManager) {
	return new MetalDrawContext(renderManager);
}

}  // namespace Draw
