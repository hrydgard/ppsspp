// Very thin API wrapper, suitable for porting UI code (like the native/ui framework) and similar but not real rendering.
// Does not involve context creation etc, that should be handled separately - only does drawing.

// The goals may change in the future though.
// MIT licensed, by Henrik Rydgård 2014.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "Common/GPU/DataFormat.h"
#include "Common/GPU/Shader.h"

namespace Lin {
class Matrix4x4;
}

namespace Draw {

// Useful in UBOs
typedef int bool32;

enum class Comparison : int {
	NEVER,
	LESS,
	EQUAL,
	LESS_EQUAL,
	GREATER,
	NOT_EQUAL,
	GREATER_EQUAL,
	ALWAYS,
};

// Had to prefix with LOGIC, too many clashes
enum class LogicOp : int {
	LOGIC_CLEAR,
	LOGIC_SET,
	LOGIC_COPY,
	LOGIC_COPY_INVERTED,
	LOGIC_NOOP,
	LOGIC_INVERT,
	LOGIC_AND,
	LOGIC_NAND,
	LOGIC_OR,
	LOGIC_NOR,
	LOGIC_XOR,
	LOGIC_EQUIV,
	LOGIC_AND_REVERSE,
	LOGIC_AND_INVERTED,
	LOGIC_OR_REVERSE,
	LOGIC_OR_INVERTED,
};

enum class BlendOp : int {
	ADD,
	SUBTRACT,
	REV_SUBTRACT,
	MIN,
	MAX,
};

enum class BlendFactor : uint8_t {
	ZERO,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	CONSTANT_COLOR,
	ONE_MINUS_CONSTANT_COLOR,
	CONSTANT_ALPHA,
	ONE_MINUS_CONSTANT_ALPHA,
	SRC1_COLOR,
	ONE_MINUS_SRC1_COLOR,
	SRC1_ALPHA,
	ONE_MINUS_SRC1_ALPHA,
};

enum class StencilOp {
	KEEP = 0,
	ZERO = 1,
	REPLACE = 2,
	INCREMENT_AND_CLAMP = 3,
	DECREMENT_AND_CLAMP = 4,
	INVERT = 5,
	INCREMENT_AND_WRAP = 6,
	DECREMENT_AND_WRAP = 7,
};

enum class TextureFilter : int {
	NEAREST = 0,
	LINEAR = 1,
};

enum BufferUsageFlag : int {
	VERTEXDATA = 1,
	INDEXDATA = 2,
	GENERIC = 4,
	UNIFORM = 8,

	DYNAMIC = 16,
};

enum Semantic : int {
	SEM_POSITION,
	SEM_COLOR0,
	SEM_TEXCOORD0,
	SEM_TEXCOORD1,
	SEM_NORMAL,
	SEM_TANGENT,
	SEM_BINORMAL,  // really BITANGENT
	SEM_MAX,
};

enum class Primitive {
	POINT_LIST,
	LINE_LIST,
	LINE_STRIP,
	TRIANGLE_LIST,
	TRIANGLE_STRIP,
	TRIANGLE_FAN,
	// Tesselation shader only
	PATCH_LIST,
	// These are for geometry shaders only.
	LINE_LIST_ADJ,
	LINE_STRIP_ADJ,
	TRIANGLE_LIST_ADJ,
	TRIANGLE_STRIP_ADJ,

	UNDEFINED,
	PRIMITIVE_TYPE_COUNT,
};

enum VertexShaderPreset : int {
	VS_COLOR_2D,
	VS_TEXTURE_COLOR_2D,
	VS_MAX_PRESET,
};

enum FragmentShaderPreset : int {
	FS_COLOR_2D,
	FS_TEXTURE_COLOR_2D,
	FS_TEXTURE_COLOR_2D_RB_SWIZZLE,
	FS_MAX_PRESET,
};

enum class TextureType : uint8_t {
	UNKNOWN,
	LINEAR1D,
	LINEAR2D,
	LINEAR3D,
	CUBE,
	ARRAY1D,
	ARRAY2D,
};

enum class CullMode : uint8_t {
	NONE,
	FRONT,
	BACK,
	FRONT_AND_BACK,  // Not supported on D3D9
};

enum class Facing {
	CCW,
	CW,
};

enum BorderColor {
	DONT_CARE,
	TRANSPARENT_BLACK,
	OPAQUE_BLACK,
	OPAQUE_WHITE,
};

enum {
	COLOR_MASK_R = 1,
	COLOR_MASK_G = 2,
	COLOR_MASK_B = 4,
	COLOR_MASK_A = 8,
};

enum class TextureAddressMode {
	REPEAT = 0,
	REPEAT_MIRROR,
	CLAMP_TO_EDGE,
	CLAMP_TO_BORDER,
};

enum FormatSupport {
	FMT_RENDERTARGET = 1,
	FMT_TEXTURE = 2,
	FMT_INPUTLAYOUT = 4,
	FMT_DEPTHSTENCIL = 8,
	FMT_AUTOGEN_MIPS = 16,
};

enum InfoField {
	APINAME,
	APIVERSION,
	VENDORSTRING,
	VENDOR,
	SHADELANGVERSION,
	DRIVER,
};

enum class GPUVendor {
	VENDOR_UNKNOWN,
	VENDOR_NVIDIA,
	VENDOR_INTEL,
	VENDOR_AMD,
	VENDOR_ARM,  // Mali
	VENDOR_QUALCOMM,
	VENDOR_IMGTEC,  // PowerVR
	VENDOR_BROADCOM,  // Raspberry
	VENDOR_VIVANTE,
	VENDOR_APPLE,
};

enum class NativeObject {
	CONTEXT,
	CONTEXT_EX,
	DEVICE,
	DEVICE_EX,
	BACKBUFFER_COLOR_VIEW,
	BACKBUFFER_DEPTH_VIEW,
	BACKBUFFER_COLOR_TEX,
	BACKBUFFER_DEPTH_TEX,
	FEATURE_LEVEL,
	COMPATIBLE_RENDERPASS,
	BACKBUFFER_RENDERPASS,
	FRAMEBUFFER_RENDERPASS,
	INIT_COMMANDBUFFER,
	BOUND_TEXTURE0_IMAGEVIEW,
	BOUND_TEXTURE1_IMAGEVIEW,
	RENDER_MANAGER,
	NULL_IMAGEVIEW,
};

enum FBChannel {
	FB_COLOR_BIT = 1,
	FB_DEPTH_BIT = 2,
	FB_STENCIL_BIT = 4,

	// Implementation specific
	FB_SURFACE_BIT = 32,  // Used in conjunction with the others in D3D9 to get surfaces through get_api_texture
	FB_VIEW_BIT = 64,     // Used in conjunction with the others in D3D11 to get shader resource views through get_api_texture
	FB_FORMAT_BIT = 128,  // Actually retrieves the native format instead. D3D11 only.
};

enum FBBlitFilter {
	FB_BLIT_NEAREST = 0,
	FB_BLIT_LINEAR = 1,
};

enum UpdateBufferFlags {
	UPDATE_DISCARD = 1,
};

enum class Event {
	// These happen on D3D resize. Only the backbuffer needs to be resized.
	LOST_BACKBUFFER,
	GOT_BACKBUFFER,

	// These are a bit more serious...
	LOST_DEVICE,
	GOT_DEVICE,

	RESIZED,
	PRESENTED,
};

constexpr uint32_t MAX_TEXTURE_SLOTS = 3;

struct FramebufferDesc {
	int width;
	int height;
	int depth;
	int numColorAttachments;
	bool z_stencil;
	const char *tag;  // For graphics debuggers
};

// Binary compatible with D3D11 viewport.
struct Viewport {
	float TopLeftX;
	float TopLeftY;
	float Width;
	float Height;
	float MinDepth;
	float MaxDepth;
};

class Bugs {
public:
	bool Has(uint32_t bug) const {
		return (flags_ & (1 << bug)) != 0;
	}
	void Infest(uint32_t bug) {
		flags_ |= (1 << bug);
	}
	uint32_t MaxBugIndex() const {
		return (uint32_t)MAX_BUG;
	}
	const char *GetBugName(uint32_t bug);

	enum : uint32_t {
		NO_DEPTH_CANNOT_DISCARD_STENCIL = 0,
		DUAL_SOURCE_BLENDING_BROKEN = 1,
		ANY_MAP_BUFFER_RANGE_SLOW = 2,
		PVR_GENMIPMAP_HEIGHT_GREATER = 3,
		BROKEN_NAN_IN_CONDITIONAL = 4,
		COLORWRITEMASK_BROKEN_WITH_DEPTHTEST = 5,
		BROKEN_FLAT_IN_SHADER = 6,
		EQUAL_WZ_CORRUPTS_DEPTH = 7,
		MALI_STENCIL_DISCARD_BUG = 8,
		RASPBERRY_SHADER_COMP_HANG = 9,
		MAX_BUG,
	};

protected:
	uint32_t flags_ = 0;
};

class RefCountedObject {
public:
	RefCountedObject() {
		refcount_ = 1;
	}
	virtual ~RefCountedObject() {}

	void AddRef() { refcount_++; }
	bool Release();
	bool ReleaseAssertLast();

private:
	std::atomic<int> refcount_;
};

template <typename T>
struct AutoRef {
	AutoRef() {}
	explicit AutoRef(T *p) {
		ptr = p;
		if (ptr)
			ptr->AddRef();
	}
	AutoRef(const AutoRef<T> &p) {
		*this = p.ptr;
	}
	~AutoRef() {
		if (ptr)
			ptr->Release();
	}

	T *operator =(T *p) {
		if (ptr)
			ptr->Release();
		ptr = p;
		if (ptr)
			ptr->AddRef();
		return ptr;
	}
	AutoRef<T> &operator =(const AutoRef<T> &p) {
		*this = p.ptr;
		return *this;
	}

	T *operator->() const {
		return ptr;
	}
	operator T *() {
		return ptr;
	}

	T *ptr = nullptr;
};

class BlendState : public RefCountedObject {
public:
};

class SamplerState : public RefCountedObject {
public:
};

class DepthStencilState : public RefCountedObject {
public:
};

class Framebuffer : public RefCountedObject {
public:
	int Width() { return width_; }
	int Height() { return height_; }
protected:
	int width_ = -1, height_ = -1;
};

class Buffer : public RefCountedObject {
public:
};

class Texture : public RefCountedObject {
public:
	int Width() { return width_; }
	int Height() { return height_; }
	int Depth() { return depth_; }
protected:
	int width_ = -1, height_ = -1, depth_ = -1;
};

struct BindingDesc {
	int stride;
	bool instanceRate;
};

struct AttributeDesc {
	int binding;
	int location;  // corresponds to semantic
	DataFormat format;
	int offset;
};

struct InputLayoutDesc {
	std::vector<BindingDesc> bindings;
	std::vector<AttributeDesc> attributes;
};

class InputLayout : public RefCountedObject { };

// Uniform types have moved to Shader.h.

class ShaderModule : public RefCountedObject {
public:
	virtual ShaderStage GetStage() const = 0;
};

class Pipeline : public RefCountedObject {
public:
	virtual ~Pipeline() {}
	virtual bool RequiresBuffer() = 0;
};

class RasterState : public RefCountedObject {};

struct StencilSide {
	StencilOp failOp;
	StencilOp passOp;
	StencilOp depthFailOp;
	Comparison compareOp;
	uint8_t compareMask;
	uint8_t writeMask;
};

struct DepthStencilStateDesc {
	bool depthTestEnabled;
	bool depthWriteEnabled;
	Comparison depthCompare;
	bool stencilEnabled;
	StencilSide front;
	StencilSide back;
};

struct BlendStateDesc {
	bool enabled;
	int colorMask;
	BlendFactor srcCol;
	BlendFactor dstCol;
	BlendOp eqCol;
	BlendFactor srcAlpha;
	BlendFactor dstAlpha;
	BlendOp eqAlpha;
	bool logicEnabled;
	LogicOp logicOp;
};

struct SamplerStateDesc {
	TextureFilter magFilter;
	TextureFilter minFilter;
	TextureFilter mipFilter;
	float maxAniso;
	TextureAddressMode wrapU;
	TextureAddressMode wrapV;
	TextureAddressMode wrapW;
	float maxLod;
	bool shadowCompareEnabled;
	Comparison shadowCompareFunc;
	BorderColor borderColor;
};

struct RasterStateDesc {
	CullMode cull;
	Facing frontFace;
};

struct PipelineDesc {
	Primitive prim;
	std::vector<ShaderModule *> shaders;
	InputLayout *inputLayout;
	DepthStencilState *depthStencil;
	BlendState *blend;
	RasterState *raster;
	const UniformBufferDesc *uniformDesc;
};

struct DeviceCaps {
	GPUVendor vendor;
	uint32_t deviceID;  // use caution!

	DataFormat preferredDepthBufferFormat;
	DataFormat preferredShadowMapFormatLow;
	DataFormat preferredShadowMapFormatHigh;
	bool anisoSupported;
	bool depthRangeMinusOneToOne;  // OpenGL style depth
	bool geometryShaderSupported;
	bool tesselationShaderSupported;
	bool multiViewport;
	bool dualSourceBlend;
	bool logicOpSupported;
	bool depthClampSupported;
	bool clipDistanceSupported;
	bool cullDistanceSupported;
	bool framebufferCopySupported;
	bool framebufferBlitSupported;
	bool framebufferDepthCopySupported;
	bool framebufferDepthBlitSupported;
	bool framebufferFetchSupported;
	std::string deviceName;  // The device name to use when creating the thin3d context, to get the same one.
};

// Use to write data directly to texture memory.  initData is the pointer passed in TextureDesc.
// Important: only write to the provided pointer, don't read from it.
typedef std::function<bool(uint8_t *data, const uint8_t *initData, uint32_t w, uint32_t h, uint32_t d, uint32_t byteStride, uint32_t sliceByteStride)> TextureCallback;

struct TextureDesc {
	TextureType type;
	DataFormat format;
	int width;
	int height;
	int depth;
	int mipLevels;
	bool generateMips;
	// Optional, for tracking memory usage and graphcis debuggers.
	const char *tag;
	// Does not take ownership over pointed-to data.
	std::vector<const uint8_t *> initData;
	TextureCallback initDataCallback;
};

enum class RPAction {
	DONT_CARE,
	CLEAR,
	KEEP,
};

struct RenderPassInfo {
	RPAction color;
	RPAction depth;
	RPAction stencil;
	uint32_t clearColor;
	float clearDepth;
	uint8_t clearStencil;
	const char *tag;
};

class DrawContext {
public:
	virtual ~DrawContext();
	bool CreatePresets();
	void DestroyPresets();

	Bugs GetBugs() const { return bugs_; }

	virtual const DeviceCaps &GetDeviceCaps() const = 0;
	virtual uint32_t GetDataFormatSupport(DataFormat fmt) const = 0;
	virtual std::vector<std::string> GetFeatureList() const { return std::vector<std::string>(); }
	virtual std::vector<std::string> GetExtensionList() const { return std::vector<std::string>(); }
	virtual std::vector<std::string> GetDeviceList() const { return std::vector<std::string>(); }

	// Describes the primary shader language that this implementation prefers.
	const ShaderLanguageDesc &GetShaderLanguageDesc() {
		return shaderLanguageDesc_;
	}

	virtual uint32_t GetSupportedShaderLanguages() const = 0;

	virtual void SetErrorCallback(ErrorCallbackFn callback, void *userdata) {}

	// Partial pipeline state, used to create pipelines. (in practice, in d3d11 they'll use the native state objects directly).
	virtual DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) = 0;
	virtual BlendState *CreateBlendState(const BlendStateDesc &desc) = 0;
	virtual SamplerState *CreateSamplerState(const SamplerStateDesc &desc) = 0;
	virtual RasterState *CreateRasterState(const RasterStateDesc &desc) = 0;
	// virtual ComputePipeline CreateComputePipeline(const ComputePipelineDesc &desc) = 0
	virtual InputLayout *CreateInputLayout(const InputLayoutDesc &desc) = 0;

	// Note that these DO NOT AddRef so you must not ->Release presets unless you manually AddRef them.
	ShaderModule *GetVshaderPreset(VertexShaderPreset preset) { return vsPresets_[preset]; }
	ShaderModule *GetFshaderPreset(FragmentShaderPreset preset) { return fsPresets_[preset]; }

	// Resources
	virtual Buffer *CreateBuffer(size_t size, uint32_t usageFlags) = 0;
	// Does not take ownership over pointed-to initData. After this returns, can dispose of it.
	virtual Texture *CreateTexture(const TextureDesc &desc) = 0;
	// On some hardware, you might get a 24-bit depth buffer even though you only wanted a 16-bit one.
	virtual Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) = 0;

	virtual ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const std::string &tag = "thin3d") = 0;
	virtual Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) = 0;

	// Copies data from the CPU over into the buffer, at a specific offset. This does not change the size of the buffer and cannot write outside it.
	virtual void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) = 0;

	virtual void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits, const char *tag) = 0;
	virtual bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) = 0;
	virtual bool CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, const char *tag) {
		return false;
	}
	virtual DataFormat PreferredFramebufferReadbackFormat(Framebuffer *src) {
		return DataFormat::R8G8B8A8_UNORM;
	}

	// These functions should be self explanatory.
	// Binding a zero render target means binding the backbuffer.
	virtual void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) = 0;
	virtual Framebuffer *GetCurrentRenderTarget() = 0;

	// binding must be < MAX_TEXTURE_SLOTS (0, 1 are okay if it's 2).
	virtual void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) = 0;

	// deprecated
	virtual uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) {
		return 0;
	}

	virtual void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) = 0;

	// Useful in OpenGL ES to give hints about framebuffers on tiler GPUs.
	virtual void InvalidateFramebuffer(Framebuffer *fbo) {}

	// Dynamic state
	virtual void SetScissorRect(int left, int top, int width, int height) = 0;
	virtual void SetViewports(int count, Viewport *viewports) = 0;
	virtual void SetBlendFactor(float color[4]) = 0;
	virtual void SetStencilRef(uint8_t ref) = 0;

	virtual void BindSamplerStates(int start, int count, SamplerState **state) = 0;
	virtual void BindTextures(int start, int count, Texture **textures) = 0;
	virtual void BindVertexBuffers(int start, int count, Buffer **buffers, const int *offsets) = 0;
	virtual void BindIndexBuffer(Buffer *indexBuffer, int offset) = 0;

	// Only supports a single dynamic uniform buffer, for maximum compatibility with the old APIs and ease of emulation.
	// More modern methods will be added later.
	virtual void UpdateDynamicUniformBuffer(const void *ub, size_t size) = 0;

	void BindTexture(int stage, Texture *texture) {
		Texture *textures[1] = { texture };
		BindTextures(stage, 1, textures);
	}  // from sampler 0 and upwards

	// Clear state cached within thin3d. Must be called after directly calling API functions.
	// Note that framebuffer state (which framebuffer is bounds) may not be cached.
	// Must not actually perform any API calls itself since this can be called when no framebuffer is bound for rendering.
	virtual void InvalidateCachedState() = 0;

	virtual void BindPipeline(Pipeline *pipeline) = 0;

	virtual void Draw(int vertexCount, int offset) = 0;
	virtual void DrawIndexed(int vertexCount, int offset) = 0;  // Always 16-bit indices.
	virtual void DrawUP(const void *vdata, int vertexCount) = 0;
	
	// Frame management (for the purposes of sync and resource management, necessary with modern APIs). Default implementations here.
	virtual void BeginFrame() {}
	virtual void EndFrame() = 0;
	virtual void WipeQueue() {}

	// This should be avoided as much as possible, in favor of clearing when binding a render target, which is native
	// on Vulkan.
	virtual void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) = 0;

	// Necessary to correctly flip scissor rectangles etc for OpenGL.
	virtual void SetTargetSize(int w, int h) {
		targetWidth_ = w;
		targetHeight_ = h;
	}

	virtual std::string GetInfoString(InfoField info) const = 0;
	virtual uint64_t GetNativeObject(NativeObject obj) = 0;

	virtual void HandleEvent(Event ev, int width, int height, void *param1 = nullptr, void *param2 = nullptr) = 0;

	// Flush state like scissors etc so the caller can do its own custom drawing.
	virtual void FlushState() {}

	virtual int GetCurrentStepId() const = 0;

protected:
	ShaderModule *vsPresets_[VS_MAX_PRESET];
	ShaderModule *fsPresets_[FS_MAX_PRESET];

	ShaderLanguageDesc shaderLanguageDesc_;

	int targetWidth_;
	int targetHeight_;

	Bugs bugs_;
};

extern const UniformBufferDesc UBPresetDesc;

// UBs for the preset shaders

struct VsTexColUB {
	float WorldViewProj[16];
	float tint;
	float saturation;
	float pad[2];
};
extern const UniformBufferDesc vsTexColBufDesc;
struct VsColUB {
	float WorldViewProj[16];
	float tint;
	float saturation;
	float pad[2];
};
extern const UniformBufferDesc vsColBufDesc;

// Useful utility for specifying a shader in multiple languages.

struct ShaderSource {
	ShaderLanguage lang;
	const char *src;
};

ShaderModule *CreateShader(DrawContext *draw, ShaderStage stage, const std::vector<ShaderSource> &sources);

}  // namespace Draw
