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

#endif

class VulkanContext;

namespace Draw {

// Useful in UBOs
typedef int bool32;

enum BlendOp : int {
	ADD,
	SUBTRACT,
	REV_SUBTRACT,
	MIN,
	MAX,
};

enum Comparison : int {
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

enum BlendFactor : int {
	ZERO,
	ONE,
	SRC_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_COLOR,
	ONE_MINUS_SRC_ALPHA,
	DST_COLOR,
	DST_ALPHA,
	ONE_MINUS_DST_COLOR,
	ONE_MINUS_DST_ALPHA,
	FIXED_COLOR,
};

enum class TextureAddressMode : int {
	REPEAT,
	CLAMP,
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
	PATCH_LIST,
	// These are for geometry shaders only.
	LINE_LIST_ADJ,
	LINE_STRIP_ADJ,
	TRIANGLE_LIST_ADJ,
	TRIANGLE_STRIP_ADJ,
};

enum T3DVertexShaderPreset : int {
	VS_COLOR_2D,
	VS_TEXTURE_COLOR_2D,
	VS_MAX_PRESET,
};

enum T3DFragmentShaderPreset : int {
	FS_COLOR_2D,
	FS_TEXTURE_COLOR_2D,
	FS_MAX_PRESET,
};

// Predefined full shader setups.
enum T3DShaderSetPreset : int {
	SS_COLOR_2D,
	SS_TEXTURE_COLOR_2D,
	SS_MAX_PRESET,
};

enum ClearFlag : int {
	COLOR = 1,
	DEPTH = 2,
	STENCIL = 4,
};

enum TextureType : uint8_t {
	UNKNOWN,
	LINEAR1D,
	LINEAR2D,
	LINEAR3D,
	CUBE,
	ARRAY1D,
	ARRAY2D,
};

enum class DataFormat : uint8_t {
	UNKNOWN,
	LUMINANCE,
	R8A8G8B8_UNORM,
	R4G4B4A4_UNORM,
	FLOATx2,
	FLOATx3,
	FLOATx4,
	UNORM8x4,

	DXT1,
	ETC1,  // Needs simulation on many platforms
	D16,
	D24S8,
	D24X8,
};

enum ImageFileType {
	PNG,
	JPEG,
	ZIM,
	DETECT,
	TYPE_UNKNOWN,
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

class Thin3DObject {
public:
	Thin3DObject() : refcount_(1) {}
	virtual ~Thin3DObject() {}

	// TODO: Reconsider this annoying ref counting stuff.
	virtual void AddRef() { refcount_++; }
	virtual bool Release() {
		if (refcount_ > 0 && refcount_ < 10000) {
			refcount_--;
			if (refcount_ == 0) {
				delete this;
				return true;
			}
		} else {
			ELOG("Refcount (%d) invalid for object %p - corrupt?", refcount_, this);
		}
		return false;
	}

private:
	int refcount_;
};

class BlendState : public Thin3DObject {
public:
};

class Thin3DSamplerState : public Thin3DObject {
public:
};

class Thin3DDepthStencilState : public Thin3DObject {
public:
};

class Thin3DBuffer : public Thin3DObject {
public:
	virtual void SetData(const uint8_t *data, size_t size) = 0;
	virtual void SubData(const uint8_t *data, size_t offset, size_t size) = 0;
};

class Thin3DTexture : public Thin3DObject {
public:
	bool LoadFromFile(const std::string &filename, ImageFileType type = ImageFileType::DETECT);
	bool LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type = ImageFileType::DETECT);

	virtual bool Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) = 0;
	virtual void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) = 0;
	virtual void AutoGenMipmaps() = 0;
	virtual void Finalize(int zim_flags) = 0;  // TODO: Tidy up

	int Width() { return width_; }
	int Height() { return height_; }
	int Depth() { return depth_; }
protected:
	std::string filename_;  // Textures that are loaded from files can reload themselves automatically.
	int width_, height_, depth_;
};

struct Thin3DVertexComponent {
	Thin3DVertexComponent() : name(nullptr), type(DataFormat::UNKNOWN), semantic(255), offset(255) {}
	Thin3DVertexComponent(const char *name, Semantic semantic, DataFormat dataType, uint8_t offset) {
		this->name = name;
		this->semantic = semantic;
		this->type = dataType;
		this->offset = offset;
	}
	const char *name;
	uint8_t semantic;
	DataFormat type;
	uint8_t offset;
};

class Thin3DVertexFormat : public Thin3DObject {
public:
	virtual bool RequiresBuffer() = 0;
};

class Thin3DShader : public Thin3DObject {
public:
};

class Thin3DShaderSet : public Thin3DObject {
public:
	// TODO: Make some faster way of doing these. Support uniform buffers (and fake them on GL 2.0?)
	virtual void SetVector(const char *name, float *value, int n) = 0;
	virtual void SetMatrix4x4(const char *name, const float value[16]) = 0;
};

class Thin3DRasterState : public Thin3DObject {
public:
};

enum class ShaderStage {
	VERTEX,
	FRAGMENT,
	GEOMETRY,
	CONTROL,  // HULL
	EVALUATION,  // DOMAIN
	COMPUTE,
};

enum class ShaderLanguage {
	GLSL_ES_200,
	GLSL_ES_300,
	GLSL_410,
	GLSL_VULKAN,
	HLSL_D3D9,
	HLSL_D3D11,
};

struct BlendStateDesc {
	bool enabled;
	BlendFactor srcCol;
	BlendFactor dstCol;
	BlendOp eqCol;
	BlendFactor srcAlpha;
	BlendFactor dstAlpha;
	BlendOp eqAlpha;
	bool logicEnabled;
	LogicOp logicOp;
	// int colorMask;
};

struct T3DSamplerStateDesc {
	TextureFilter magFilt;
	TextureFilter minFilt;
	TextureFilter mipFilt;
	TextureAddressMode wrapS;
	TextureAddressMode wrapT;
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

struct T3DRasterStateDesc {
	CullMode cull;
	Facing facing;
};

class Thin3DContext : public Thin3DObject {
public:
	virtual ~Thin3DContext();

	virtual std::vector<std::string> GetFeatureList() { return std::vector<std::string>(); }

	virtual Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, Comparison depthCompare) = 0;
	virtual BlendState *CreateBlendState(const BlendStateDesc &desc) = 0;
	virtual Thin3DSamplerState *CreateSamplerState(const T3DSamplerStateDesc &desc) = 0;
	virtual Thin3DRasterState *CreateRasterState(const T3DRasterStateDesc &desc) = 0;
	virtual Thin3DBuffer *CreateBuffer(size_t size, uint32_t usageFlags) = 0;
	virtual Thin3DShaderSet *CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) = 0;
	virtual Thin3DVertexFormat *CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride, Thin3DShader *vshader) = 0;

	virtual Thin3DTexture *CreateTexture() = 0;  // To be later filled in by ->LoadFromFile or similar.
	virtual Thin3DTexture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) = 0;

	// Common Thin3D function, uses CreateTexture
	Thin3DTexture *CreateTextureFromFile(const char *filename, ImageFileType fileType);
	Thin3DTexture *CreateTextureFromFileData(const uint8_t *data, int size, ImageFileType fileType);

	// Note that these DO NOT AddRef so you must not ->Release presets unless you manually AddRef them.
	Thin3DShader *GetVshaderPreset(T3DVertexShaderPreset preset) { return fsPresets_[preset]; }
	Thin3DShader *GetFshaderPreset(T3DFragmentShaderPreset preset) { return vsPresets_[preset]; }
	Thin3DShaderSet *GetShaderSetPreset(T3DShaderSetPreset preset) { return ssPresets_[preset]; }

	// The implementation makes the choice of which shader code to use.
	virtual Thin3DShader *CreateShader(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) = 0;

	// Bound state objects. Too cumbersome to add them all as parameters to Draw.
	virtual void SetBlendState(BlendState *state) = 0;
	virtual void SetSamplerStates(int start, int count, Thin3DSamplerState **state) = 0;
	virtual void SetDepthStencilState(Thin3DDepthStencilState *state) = 0;
	virtual void SetRasterState(Thin3DRasterState *state) = 0;

	virtual void BindTextures(int start, int count, Thin3DTexture **textures) = 0;
	void BindTexture(int stage, Thin3DTexture *texture) {
		BindTextures(stage, 1, &texture);
	}  // from sampler 0 and upwards

	// Raster state
	virtual void SetScissorEnabled(bool enable) = 0;
	virtual void SetScissorRect(int left, int top, int width, int height) = 0;
	virtual void SetViewports(int count, Viewport *viewports) = 0;

	// TODO: Add more sophisticated draws with buffer offsets, and multidraws.
	virtual void Draw(Primitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) = 0;
	virtual void DrawIndexed(Primitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) = 0;
	virtual void DrawUP(Primitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, const void *vdata, int vertexCount) = 0;
	
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

	Thin3DShader *vsPresets_[VS_MAX_PRESET];
	Thin3DShader *fsPresets_[FS_MAX_PRESET];
	Thin3DShaderSet *ssPresets_[SS_MAX_PRESET];

	int targetWidth_;
	int targetHeight_;

private:
};

Thin3DContext *T3DCreateGLContext();

#ifdef _WIN32
Thin3DContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx);
#endif

Thin3DContext *T3DCreateVulkanContext(VulkanContext *context);
Thin3DContext *T3DCreateD3D11Context();

}  // namespace Draw