// Very thin API wrapper, suitable for porting UI code (like the native/ui framework) and similar but not real rendering.
// Does not involve context creation etc, that should be handled separately - only does drawing.

// The goals may change in the future though.
// MIT licensed, by Henrik Rydgård 2014.

#pragma once

#include <stdint.h>
#include <cstddef>
#include <vector>
#include <string>

#include "base/logging.h"

class Matrix4x4;

#ifdef _WIN32

struct IDirect3DDevice9;
struct IDirect3D9;
struct IDirect3DDevice9Ex;
struct IDirect3D9Ex;
struct ID3D11Device;
struct ID3D11DeviceContext;

#endif

class VulkanContext;

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
	NEAREST,
	LINEAR,
};

enum BufferUsageFlag : int {
	VERTEXDATA = 1,
	INDEXDATA = 2,
	GENERIC = 4,

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
};

enum VertexShaderPreset : int {
	VS_COLOR_2D,
	VS_TEXTURE_COLOR_2D,
	VS_MAX_PRESET,
};

enum FragmentShaderPreset : int {
	FS_COLOR_2D,
	FS_TEXTURE_COLOR_2D,
	FS_MAX_PRESET,
};

enum ClearFlag : int {
	COLOR = 1,
	DEPTH = 2,
	STENCIL = 4,
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

enum class DataFormat : uint8_t {
	UNDEFINED,

	R8_UNORM,
	R8G8_UNORM,
	R8G8B8_UNORM,

	R8G8B8A8_UNORM,
	R8G8B8A8_UNORM_SRGB,

	R8G8B8A8_SNORM,
	R8G8B8A8_UINT,
	R8G8B8A8_SINT,

	R4G4_UNORM,
	R4G4B4A4_UNORM,

	R16_FLOAT,
	R16G16_FLOAT,
	R16G16B16A16_FLOAT,

	R32_FLOAT,
	R32G32_FLOAT,
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,

	// Block compression formats.
	// These are modern names for DXT and friends, now patent free.
	// https://msdn.microsoft.com/en-us/library/bb694531.aspx
	BC1_RGBA_UNORM_BLOCK,
	BC1_RGBA_SRGB_BLOCK,
	BC2_UNORM_BLOCK,  // 4-bit straight alpha + DXT1 color. Usually not worth using
	BC2_SRGB_BLOCK,
	BC3_UNORM_BLOCK,  // 3-bit alpha with 2 ref values (+ magic) + DXT1 color
	BC3_SRGB_BLOCK,
	BC4_UNORM_BLOCK,  // 1-channel, same storage as BC3 alpha
	BC4_SNORM_BLOCK,
	BC5_UNORM_BLOCK,  // 2-channel RG, each has same storage as BC3 alpha
	BC5_SNORM_BLOCK,
	BC6H_UFLOAT_BLOCK,  // TODO
	BC6H_SFLOAT_BLOCK,
	BC7_UNORM_BLOCK,    // Highly advanced, very expensive to compress, very good quality.
	BC7_SRGB_BLOCK,

	ETC1,

	S8,
	D16,
	D24_S8,
	D32F,
	D32F_S8,
};

enum class ShaderStage {
	VERTEX,
	FRAGMENT,
	GEOMETRY,
	CONTROL,  // HULL
	EVALUATION,  // DOMAIN
	COMPUTE,
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
	REPEAT,
	REPEAT_MIRROR,
	CLAMP_TO_EDGE,
	CLAMP_TO_BORDER,
};

enum class ShaderLanguage {
	GLSL_ES_200 = 1,
	GLSL_ES_300 = 2,
	GLSL_410 = 4,
	GLSL_VULKAN = 8,
	SPIRV_VULKAN = 16,
	HLSL_D3D9 = 32,
	HLSL_D3D11 = 64,
	HLSL_D3D9_BYTECODE = 128,
	HLSL_D3D11_BYTECODE = 256,
	METAL = 512,
	METAL_BYTECODE = 1024,
};

enum InfoField {
	APINAME,
	APIVERSION,
	VENDORSTRING,
	VENDOR,
	SHADELANGVERSION,
	RENDERER,
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

class RefCountedObject {
public:
	RefCountedObject() : refcount_(1) {}
	virtual ~RefCountedObject() {}

	void AddRef() { refcount_++; }
	bool Release();

private:
	int refcount_;
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

class Buffer : public RefCountedObject {
public:
	virtual void SetData(const uint8_t *data, size_t size) = 0;
	virtual void SubData(const uint8_t *data, size_t offset, size_t size) = 0;
};

class Texture : public RefCountedObject {
public:
	virtual void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) = 0;
	virtual void AutoGenMipmaps() = 0;
	virtual void Finalize() = 0;  // TODO: Tidy up

	int Width() { return width_; }
	int Height() { return height_; }
	int Depth() { return depth_; }
protected:
	int width_, height_, depth_;
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

enum class UniformType : int8_t {
	FLOAT, FLOAT2, FLOAT3, FLOAT4,
	MATRIX4X4,
};

// For emulation of uniform buffers on D3D9/GL
struct UniformDesc {
	int16_t offset;
	UniformType type;
	int8_t reg;  // For D3D

	// TODO: Support array elements etc.
};

struct UniformBufferDesc {
	std::vector<UniformDesc> uniforms;
};

class ShaderModule : public RefCountedObject {
public:
	virtual ShaderStage GetStage() const = 0;
};

class Pipeline : public RefCountedObject {
public:
	// TODO: Use a uniform-buffer based interface instead.
	virtual void SetVector(const char *name, float *value, int n) = 0;
	virtual void SetMatrix4x4(const char *name, const float value[16]) = 0;
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
	uint8_t reference;
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
};

struct DeviceCaps {
	DataFormat preferredDepthBufferFormat;
	DataFormat preferredShadowMapFormatLow;
	DataFormat preferredShadowMapFormatHigh;
	bool anisoSupported;
	bool depthRangeMinusOneToOne;  // OpenGL style depth
	bool geometryShaderSupported;
	bool tesselationShaderSupported;
	bool multiViewport;
	bool dualSourceBlend;
};

struct TextureDesc {
	TextureType type;
	DataFormat format;
	int width;
	int height;
	int depth;
	int mipLevels;
};

class DrawContext : public RefCountedObject {
public:
	virtual ~DrawContext();

	virtual const DeviceCaps &GetDeviceCaps() const = 0;
	virtual std::vector<std::string> GetFeatureList() const { return std::vector<std::string>(); }

	virtual uint32_t GetSupportedShaderLanguages() const = 0;

	// Partial pipeline state, used to create pipelines. (in practice, in d3d11 they'll use the native state objects directly).
	virtual DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) = 0;
	virtual BlendState *CreateBlendState(const BlendStateDesc &desc) = 0;
	virtual SamplerState *CreateSamplerState(const SamplerStateDesc &desc) = 0;
	virtual RasterState *CreateRasterState(const RasterStateDesc &desc) = 0;
	// virtual ComputePipeline CreateComputePipeline(const ComputePipelineDesc &desc) = 0
	virtual InputLayout *CreateInputLayout(const InputLayoutDesc &desc) = 0;

	// Note that these DO NOT AddRef so you must not ->Release presets unless you manually AddRef them.
	ShaderModule *GetVshaderPreset(VertexShaderPreset preset) { return fsPresets_[preset]; }
	ShaderModule *GetFshaderPreset(FragmentShaderPreset preset) { return vsPresets_[preset]; }

	// Resources
	virtual Buffer *CreateBuffer(size_t size, uint32_t usageFlags) = 0;
	virtual Texture *CreateTexture(const TextureDesc &desc) = 0;
	virtual ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) = 0;
	virtual Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) = 0;

	// Dynamic state
	virtual void SetScissorRect(int left, int top, int width, int height) = 0;
	virtual void SetViewports(int count, Viewport *viewports) = 0;
	virtual void SetBlendFactor(float color[4]) = 0;

	virtual void BindSamplerStates(int start, int count, SamplerState **state) = 0;
	virtual void BindTextures(int start, int count, Texture **textures) = 0;
	virtual void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) = 0;
	virtual void BindIndexBuffer(Buffer *indexBuffer, int offset) = 0;

	void BindTexture(int stage, Texture *texture) {
		BindTextures(stage, 1, &texture);
	}  // from sampler 0 and upwards

	virtual void BindPipeline(Pipeline *pipeline) = 0;

	// TODO: Add more sophisticated draws with buffer offsets, and multidraws.
	virtual void Draw(int vertexCount, int offset) = 0;
	virtual void DrawIndexed(int vertexCount, int offset) = 0;
	virtual void DrawUP(const void *vdata, int vertexCount) = 0;
	
	// Render pass management. Default implementations here.
	virtual void Begin(bool clear, uint32_t colorval, float depthVal, int stencilVal) {
		Clear(0xF, colorval, depthVal, stencilVal);
	}
	virtual void End() {}
	virtual void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) = 0;
	
	// Necessary to correctly flip scissor rectangles etc for OpenGL.
	void SetTargetSize(int w, int h) {
		targetWidth_ = w;
		targetHeight_ = h;
	}

	virtual std::string GetInfoString(InfoField info) const = 0;

protected:
	void CreatePresets();

	ShaderModule *vsPresets_[VS_MAX_PRESET];
	ShaderModule *fsPresets_[FS_MAX_PRESET];

	int targetWidth_;
	int targetHeight_;
};

DrawContext *T3DCreateGLContext();

#ifdef _WIN32
DrawContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx);
DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context);
#endif

DrawContext *T3DCreateVulkanContext(VulkanContext *context);

}  // namespace Draw