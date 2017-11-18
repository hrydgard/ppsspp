#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <cassert>

#include "base/logging.h"
#include "math/dataconv.h"
#include "math/lin/matrix4x4.h"
#include "thin3d/thin3d.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"
#include "gfx/GLStateCache.h"
#include "gfx_es2/gpu_features.h"

#include "thin3d/GLRenderManager.h"

#ifdef IOS
extern void bindDefaultFBO();
#endif

// #define DEBUG_READ_PIXELS 1

// Workaround for Retroarch. Simply declare
//   extern GLuint g_defaultFBO;
// and set is as appropriate. Can adjust the variables in ext/native/base/display.h as
// appropriate.
GLuint g_defaultFBO = 0;

namespace Draw {

static const unsigned short compToGL[] = {
	GL_NEVER,
	GL_LESS,
	GL_EQUAL,
	GL_LEQUAL,
	GL_GREATER,
	GL_NOTEQUAL,
	GL_GEQUAL,
	GL_ALWAYS
};

static const unsigned short blendEqToGL[] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_MIN,
	GL_MAX,
};

static const unsigned short blendFactorToGL[] = {
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	GL_CONSTANT_ALPHA,
	GL_ONE_MINUS_CONSTANT_ALPHA,
#if !defined(USING_GLES2)   // TODO: Remove when we have better headers
	GL_SRC1_COLOR,
	GL_ONE_MINUS_SRC1_COLOR,
	GL_SRC1_ALPHA,
	GL_ONE_MINUS_SRC1_ALPHA,
#elif !defined(IOS)
	GL_SRC1_COLOR_EXT,
	GL_ONE_MINUS_SRC1_COLOR_EXT,
	GL_SRC1_ALPHA_EXT,
	GL_ONE_MINUS_SRC1_ALPHA_EXT,
#else
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
#endif
};

static const unsigned short texWrapToGL[] = {
	GL_REPEAT,
	GL_MIRRORED_REPEAT,
	GL_CLAMP_TO_EDGE,
#if !defined(USING_GLES2)
	GL_CLAMP_TO_BORDER,
#else
	GL_CLAMP_TO_EDGE,
#endif
};

static const unsigned short texFilterToGL[] = {
	GL_NEAREST,
	GL_LINEAR,
};

static const unsigned short texMipFilterToGL[2][2] = {
	// Min nearest:
	{ GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR },
	// Min linear:
	{ GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR },
};

#ifndef USING_GLES2
static const unsigned short logicOpToGL[] = {
	GL_CLEAR,
	GL_SET,
	GL_COPY,
	GL_COPY_INVERTED,
	GL_NOOP,
	GL_INVERT,
	GL_AND,
	GL_NAND,
	GL_OR,
	GL_NOR,
	GL_XOR,
	GL_EQUIV,
	GL_AND_REVERSE,
	GL_AND_INVERTED,
	GL_OR_REVERSE,
	GL_OR_INVERTED,
};
#endif

static const GLuint stencilOpToGL[8] = {
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INCR,
	GL_DECR,
	GL_INVERT,
	GL_INCR_WRAP,
	GL_DECR_WRAP,
};

static const unsigned short primToGL[] = {
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
#if !defined(USING_GLES2)   // TODO: Remove when we have better headers
	GL_PATCHES,
	GL_LINES_ADJACENCY,
	GL_LINE_STRIP_ADJACENCY,
	GL_TRIANGLES_ADJACENCY,
	GL_TRIANGLE_STRIP_ADJACENCY,
#elif !defined(IOS)
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
#else
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
	GL_POINTS,
#endif
};

class OpenGLBuffer;

class OpenGLBlendState : public BlendState {
public:
	bool enabled;
	GLuint eqCol, eqAlpha;
	GLuint srcCol, srcAlpha, dstCol, dstAlpha;
	bool logicEnabled;
	GLuint logicOp;
	int colorMask;
	// uint32_t fixedColor;

	void Apply() {
		if (enabled) {
			glEnable(GL_BLEND);
			glBlendEquationSeparate(eqCol, eqAlpha);
			glBlendFuncSeparate(srcCol, dstCol, srcAlpha, dstAlpha);
		} else {
			glDisable(GL_BLEND);
		}
		glColorMask(colorMask & 1, (colorMask >> 1) & 1, (colorMask >> 2) & 1, (colorMask >> 3) & 1);

#if !defined(USING_GLES2)
		if (logicEnabled) {
			glEnable(GL_COLOR_LOGIC_OP);
			glLogicOp(logicOp);
		} else {
			glDisable(GL_COLOR_LOGIC_OP);
		}
#endif
	}
};

class OpenGLSamplerState : public SamplerState {
public:
	GLint wrapU;
	GLint wrapV;
	GLint wrapW;
	GLint magFilt;
	GLint minFilt;
	GLint mipMinFilt;
};

class OpenGLDepthStencilState : public DepthStencilState {
public:
	bool depthTestEnabled;
	bool depthWriteEnabled;
	GLuint depthComp;
	// TODO: Two-sided
	GLboolean stencilEnabled;
	GLuint stencilFail;
	GLuint stencilZFail;
	GLuint stencilPass;
	GLuint stencilCompareOp;
	uint8_t stencilReference;
	uint8_t stencilCompareMask;
	uint8_t stencilWriteMask;

	void Apply() {
		if (depthTestEnabled) {
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(depthComp);
			glDepthMask(depthWriteEnabled);
		} else {
			glDisable(GL_DEPTH_TEST);
		}
		if (stencilEnabled) {
			glEnable(GL_STENCIL_TEST);
			glStencilOpSeparate(GL_FRONT_AND_BACK, stencilFail, stencilZFail, stencilPass);
			glStencilFuncSeparate(GL_FRONT_AND_BACK, stencilCompareOp, stencilReference, stencilCompareMask);
			glStencilMaskSeparate(GL_FRONT_AND_BACK, stencilWriteMask);
		} else {
			glDisable(GL_STENCIL_TEST);
		}
	}
};

class OpenGLRasterState : public RasterState {
public:
	void Apply() {
		glEnable(GL_SCISSOR_TEST);
		if (!cullEnable) {
			glDisable(GL_CULL_FACE);
			return;
		}
		glEnable(GL_CULL_FACE);
		glFrontFace(frontFace);
		glCullFace(cullMode);
	}

	GLboolean cullEnable;
	GLenum cullMode;
	GLenum frontFace;
};

GLuint ShaderStageToOpenGL(ShaderStage stage) {
	switch (stage) {
	case ShaderStage::VERTEX: return GL_VERTEX_SHADER;
#ifndef USING_GLES2
	case ShaderStage::COMPUTE: return GL_COMPUTE_SHADER;
	case ShaderStage::EVALUATION: return GL_TESS_EVALUATION_SHADER;
	case ShaderStage::CONTROL: return GL_TESS_CONTROL_SHADER;
	case ShaderStage::GEOMETRY: return GL_GEOMETRY_SHADER;
#endif
	case ShaderStage::FRAGMENT:
	default:
		return GL_FRAGMENT_SHADER;
	}
}

class OpenGLShaderModule : public ShaderModule {
public:
	OpenGLShaderModule(ShaderStage stage) : stage_(stage) {
		glstage_ = ShaderStageToOpenGL(stage);
	}

	~OpenGLShaderModule() {
		if (shader_)
			glDeleteShader(shader_);
	}

	bool Compile(ShaderLanguage language, const uint8_t *data, size_t dataSize);
	GLuint GetShader() const {
		return shader_;
	}
	const std::string &GetSource() const { return source_; }

	ShaderLanguage GetLanguage() {
		return language_;
	}
	ShaderStage GetStage() const override {
		return stage_;
	}

private:
	ShaderStage stage_;
	ShaderLanguage language_;
	GLuint shader_ = 0;
	GLuint glstage_ = 0;
	bool ok_ = false;
	std::string source_;  // So we can recompile in case of context loss.
};

bool OpenGLShaderModule::Compile(ShaderLanguage language, const uint8_t *data, size_t dataSize) {
	source_ = std::string((const char *)data);
	shader_ = glCreateShader(glstage_);
	language_ = language;

	std::string temp;
	// Add the prelude on automatically.
	if (glstage_ == GL_FRAGMENT_SHADER || glstage_ == GL_VERTEX_SHADER) {
		temp = ApplyGLSLPrelude(source_, glstage_);
		source_ = temp.c_str();
	}

	const char *code = source_.c_str();
	glShaderSource(shader_, 1, &code, nullptr);
	glCompileShader(shader_);
	GLint success = 0;
	glGetShaderiv(shader_, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len = 0;
		glGetShaderInfoLog(shader_, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
		glDeleteShader(shader_);
		shader_ = 0;
		ILOG("%s Shader compile error:\n%s", glstage_ == GL_FRAGMENT_SHADER ? "Fragment" : "Vertex", infoLog);
	}
	ok_ = success != 0;
	return ok_;
}

class OpenGLInputLayout : public InputLayout {
public:
	~OpenGLInputLayout();

	void Apply(const void *base = nullptr);
	void Unapply();
	void Compile();
	bool RequiresBuffer() {
		return id_ != 0;
	}

	InputLayoutDesc desc;

	int semanticsMask_;  // Fast way to check what semantics to enable/disable.
	int stride_;
	GLuint id_;
	bool needsEnable_;
	intptr_t lastBase_;
};

struct UniformInfo {
	int loc_;
};

class OpenGLPipeline : public Pipeline {
public:
	OpenGLPipeline() {
		program_ = 0;
	}
	~OpenGLPipeline() {
		for (auto &iter : shaders) {
			iter->Release();
		}
		glDeleteProgram(program_);
		if (depthStencil) depthStencil->Release();
		if (blend) blend->Release();
		if (raster) raster->Release();
		if (inputLayout) inputLayout->Release();
	}

	bool LinkShaders();

	int GetUniformLoc(const char *name);

	bool RequiresBuffer() override {
		return inputLayout->RequiresBuffer();
	}

	GLuint prim;
	std::vector<OpenGLShaderModule *> shaders;
	OpenGLInputLayout *inputLayout = nullptr;
	OpenGLDepthStencilState *depthStencil = nullptr;
	OpenGLBlendState *blend = nullptr;
	OpenGLRasterState *raster = nullptr;

	// TODO: Optimize by getting the locations first and putting in a custom struct
	UniformBufferDesc dynamicUniforms;

	GLuint program_;
private:
	std::map<std::string, UniformInfo> uniformCache_;
};

class OpenGLFramebuffer;
class OpenGLTexture;

class OpenGLContext : public DrawContext {
public:
	OpenGLContext();
	virtual ~OpenGLContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
#if defined(USING_GLES2)
		return (uint32_t)ShaderLanguage::GLSL_ES_200 | (uint32_t)ShaderLanguage::GLSL_ES_300;
#else
		return (uint32_t)ShaderLanguage::GLSL_ES_200 | (uint32_t)ShaderLanguage::GLSL_410;
#endif
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
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

	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindSamplerStates(int start, int count, SamplerState **states) override {
		if (boundSamplers_.size() < (size_t)(start + count)) {
			boundSamplers_.resize(start + count);
		}
		for (int i = 0; i < count; i++) {
			int index = i + start;
			boundSamplers_[index] = static_cast<OpenGLSamplerState *>(states[index]);
		}
	}

	void SetScissorRect(int left, int top, int width, int height) override {
		int y = top;
		if (!curFB_) {
			// We render "upside down" to the backbuffer since GL is silly.
			y = targetHeight_ - (top + height);
		}
		glstate.scissorRect.set(left, y, width, height);
	}

	void SetViewports(int count, Viewport *viewports) override {
		// TODO: Use glViewportArrayv.
		glViewport((GLint)viewports[0].TopLeftX, (GLint)viewports[0].TopLeftY, (GLsizei)viewports[0].Width, (GLsizei)viewports[0].Height);
#if defined(USING_GLES2)
		glDepthRangef(viewports[0].MinDepth, viewports[0].MaxDepth);
#else
		glDepthRange(viewports[0].MinDepth, viewports[0].MaxDepth);
#endif
	}

	void SetBlendFactor(float color[4]) override {
		glBlendColor(color[0], color[1], color[2], color[3]);
	}

	void BindTextures(int start, int count, Texture **textures) override;
	void BindPipeline(Pipeline *pipeline) override;
	void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) override {
		for (int i = 0; i < count; i++) {
			curVBuffers_[i + start] = (OpenGLBuffer  *)buffers[i];
			curVBufferOffsets_[i + start] = offsets ? offsets[i] : 0;
		}
	}
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override {
		curIBuffer_ = (OpenGLBuffer  *)indexBuffer;
		curIBufferOffset_ = offset;
	}

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// TODO: Add more sophisticated draws.
	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;

	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
			case APINAME:
				if (gl_extensions.IsGLES) {
					return "OpenGL ES";
				} else {
					return "OpenGL";
				}
			case VENDORSTRING: return (const char *)glGetString(GL_VENDOR);
			case VENDOR:
				switch (caps_.vendor) {
				case GPUVendor::VENDOR_AMD: return "VENDOR_AMD";
				case GPUVendor::VENDOR_IMGTEC: return "VENDOR_POWERVR";
				case GPUVendor::VENDOR_NVIDIA: return "VENDOR_NVIDIA";
				case GPUVendor::VENDOR_INTEL: return "VENDOR_INTEL";
				case GPUVendor::VENDOR_QUALCOMM: return "VENDOR_ADRENO";
				case GPUVendor::VENDOR_ARM: return "VENDOR_ARM";
				case GPUVendor::VENDOR_BROADCOM: return "VENDOR_BROADCOM";
				case GPUVendor::VENDOR_UNKNOWN:
				default:
					return "VENDOR_UNKNOWN";
				}
				break;
			case DRIVER: return (const char *)glGetString(GL_RENDERER);
			case SHADELANGVERSION: return (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
			case APIVERSION: return (const char *)glGetString(GL_VERSION);
			default: return "?";
		}
	}

	uintptr_t GetNativeObject(NativeObject obj) override {
		return 0;
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override {}

private:
	OpenGLFramebuffer *fbo_ext_create(const FramebufferDesc &desc);
	void fbo_bind_fb_target(bool read, GLuint name);
	GLenum fbo_get_fb_target(bool read, GLuint **cached);
	void fbo_unbind();
	void ApplySamplers();

	GLRenderManager renderManager_;

	std::vector<OpenGLSamplerState *> boundSamplers_;
	OpenGLTexture *boundTextures_[8]{};
	int maxTextures_ = 0;
	DeviceCaps caps_{};
	
	// Bound state
	OpenGLPipeline *curPipeline_ = nullptr;
	OpenGLBuffer *curVBuffers_[4]{};
	int curVBufferOffsets_[4]{};
	OpenGLBuffer *curIBuffer_ = nullptr;
	int curIBufferOffset_ = 0;
	OpenGLFramebuffer *curFB_;

	// Framebuffer state
	GLuint currentDrawHandle_ = 0;
	GLuint currentReadHandle_ = 0;
};

OpenGLContext::OpenGLContext() {
	// TODO: Detect more caps
	if (gl_extensions.IsGLES) {
		if (gl_extensions.OES_packed_depth_stencil || gl_extensions.OES_depth24) {
			caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
		} else {
			caps_.preferredDepthBufferFormat = DataFormat::D16;
		}
	} else {
		caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
	}
	caps_.framebufferBlitSupported = gl_extensions.NV_framebuffer_blit || gl_extensions.ARB_framebuffer_object;
	caps_.framebufferDepthBlitSupported = caps_.framebufferBlitSupported;

	switch (gl_extensions.gpuVendor) {
	case GPU_VENDOR_AMD: caps_.vendor = GPUVendor::VENDOR_AMD; break;
	case GPU_VENDOR_NVIDIA: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
	case GPU_VENDOR_ARM: caps_.vendor = GPUVendor::VENDOR_ARM; break;
	case GPU_VENDOR_QUALCOMM: caps_.vendor = GPUVendor::VENDOR_QUALCOMM; break;
	case GPU_VENDOR_BROADCOM: caps_.vendor = GPUVendor::VENDOR_BROADCOM; break;
	case GPU_VENDOR_INTEL: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
	case GPU_VENDOR_IMGTEC: caps_.vendor = GPUVendor::VENDOR_IMGTEC; break;
	case GPU_VENDOR_UNKNOWN:
	default:
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
		break;
	}
}

OpenGLContext::~OpenGLContext() {
	boundSamplers_.clear();
}

InputLayout *OpenGLContext::CreateInputLayout(const InputLayoutDesc &desc) {
	OpenGLInputLayout *fmt = new OpenGLInputLayout();
	fmt->desc = desc;
	fmt->Compile();
	return fmt;
}

GLuint TypeToTarget(TextureType type) {
	switch (type) {
#ifndef USING_GLES2
	case TextureType::LINEAR1D: return GL_TEXTURE_1D;
#endif
	case TextureType::LINEAR2D: return GL_TEXTURE_2D;
	case TextureType::LINEAR3D: return GL_TEXTURE_3D;
	case TextureType::CUBE: return GL_TEXTURE_CUBE_MAP;
#ifndef USING_GLES2
	case TextureType::ARRAY1D: return GL_TEXTURE_1D_ARRAY;
#endif
	case TextureType::ARRAY2D: return GL_TEXTURE_2D_ARRAY;
	default:
		ELOG("Bad texture type %d", (int)type);
		return GL_NONE;
	}
}

inline bool isPowerOf2(int n) {
	return n == 1 || (n & (n - 1)) == 0;
}

class OpenGLTexture : public Texture {
public:
	OpenGLTexture(const TextureDesc &desc);
	~OpenGLTexture();

	bool HasMips() const {
		return mipLevels_ > 1 || generatedMips_;
	}
	bool CanWrap() const {
		return canWrap_;
	}
	TextureType GetType() const { return type_; }
	void Bind() {
		glBindTexture(target_, tex_);
	}

	void AutoGenMipmaps();

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data);

	GLuint tex_ = 0;
	GLuint target_ = 0;

	DataFormat format_;
	TextureType type_;
	int mipLevels_;
	bool generatedMips_;
	bool canWrap_;
};

OpenGLTexture::OpenGLTexture(const TextureDesc &desc) {
	generatedMips_ = false;
	canWrap_ = true;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	format_ = desc.format;
	type_ = desc.type;
	target_ = TypeToTarget(desc.type);
	canWrap_ = isPowerOf2(width_) && isPowerOf2(height_);
	mipLevels_ = desc.mipLevels;
	if (!desc.initData.size())
		return;

	glActiveTexture(GL_TEXTURE0 + 0);
	glGenTextures(1, &tex_);
	glBindTexture(target_, tex_);

	int level = 0;
	for (auto data : desc.initData) {
		SetImageData(0, 0, 0, width_, height_, depth_, level, 0, data);
		width_ = (width_ + 1) / 2;
		height_ = (height_ + 1) / 2;
		level++;
	}
	mipLevels_ = desc.generateMips ? desc.mipLevels : level;

#ifdef USING_GLES2
	if (gl_extensions.GLES3) {
		glTexParameteri(target_, GL_TEXTURE_MAX_LEVEL, mipLevels_ - 1);
	}
#else
	glTexParameteri(target_, GL_TEXTURE_MAX_LEVEL, mipLevels_ - 1);
#endif
	glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, mipLevels_ > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if ((int)desc.initData.size() < desc.mipLevels && desc.generateMips) {
		ILOG("Generating mipmaps");
		AutoGenMipmaps();
	}

	// Unbind.
	glBindTexture(target_, 0);
}

OpenGLTexture::~OpenGLTexture() {
	if (tex_) {
		glDeleteTextures(1, &tex_);
		tex_ = 0;
		generatedMips_ = false;
	}
}

void OpenGLTexture::AutoGenMipmaps() {
	if (!generatedMips_) {
		glBindTexture(target_, tex_);
		glGenerateMipmap(target_);
		generatedMips_ = true;
	}
}

class OpenGLFramebuffer : public Framebuffer {
public:
	OpenGLFramebuffer() {}
	~OpenGLFramebuffer();

	GLuint handle = 0;
	GLuint color_texture = 0;
	GLuint z_stencil_buffer = 0;  // Either this is set, or the two below.
	GLuint z_buffer = 0;
	GLuint stencil_buffer = 0;

	int width;
	int height;
	FBColorDepth colorDepth;
};


// TODO: Also output storage format (GL_RGBA8 etc) for modern GL usage.
static bool Thin3DFormatToFormatAndType(DataFormat fmt, GLuint &internalFormat, GLuint &format, GLuint &type, int &alignment) {
	alignment = 4;
	switch (fmt) {
	case DataFormat::R8G8B8A8_UNORM:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;

	case DataFormat::D32F:
		internalFormat = GL_DEPTH_COMPONENT;
		format = GL_DEPTH_COMPONENT;
		type = GL_FLOAT;
		break;

#ifndef USING_GLES2
	case DataFormat::S8:
		internalFormat = GL_STENCIL_INDEX;
		format = GL_STENCIL_INDEX;
		type = GL_UNSIGNED_BYTE;
		alignment = 1;
		break;
#endif

	case DataFormat::R8G8B8_UNORM:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_BYTE;
		alignment = 1;
		break;

	case DataFormat::B4G4R4A4_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4;
		alignment = 2;
		break;

	case DataFormat::B5G6R5_UNORM_PACK16:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_SHORT_5_6_5;
		alignment = 2;
		break;

	case DataFormat::B5G5R5A1_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_5_5_5_1;
		alignment = 2;
		break;

#ifndef USING_GLES2
	case DataFormat::A4R4G4B4_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		alignment = 2;
		break;

	case DataFormat::R5G6B5_UNORM_PACK16:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_SHORT_5_6_5_REV;
		alignment = 2;
		break;

	case DataFormat::A1R5G5B5_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		alignment = 2;
		break;
#endif

	default:
		ELOG("Thin3d GL: Unsupported texture format %d", (int)fmt);
		return false;
	}
	return true;
}

void OpenGLTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	if (width != width_ || height != height_ || depth != depth_) {
		// When switching to texStorage we need to handle this correctly.
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	GLuint internalFormat;
	GLuint format;
	GLuint type;
	int alignment;
	if (!Thin3DFormatToFormatAndType(format_, internalFormat, format, type, alignment)) {
		return;
	}

	CHECK_GL_ERROR_IF_DEBUG();
	switch (target_) {
	case GL_TEXTURE_2D:
		glTexImage2D(GL_TEXTURE_2D, level, internalFormat, width_, height_, 0, format, type, data);
		break;
	default:
		ELOG("Thin3D GL: Targets other than GL_TEXTURE_2D not yet supported");
		break;
	}
	CHECK_GL_ERROR_IF_DEBUG();
}


#ifdef DEBUG_READ_PIXELS
// TODO: Make more generic.
static void LogReadPixelsError(GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		break;
	case GL_INVALID_ENUM:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_INVALID_ENUM");
		break;
	case GL_INVALID_VALUE:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_INVALID_VALUE");
		break;
	case GL_INVALID_OPERATION:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_INVALID_OPERATION");
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_INVALID_FRAMEBUFFER_OPERATION");
		break;
	case GL_OUT_OF_MEMORY:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_OUT_OF_MEMORY");
		break;
#ifndef USING_GLES2
	case GL_STACK_UNDERFLOW:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_STACK_UNDERFLOW");
		break;
	case GL_STACK_OVERFLOW:
		ERROR_LOG(FRAMEBUF, "glReadPixels: GL_STACK_OVERFLOW");
		break;
#endif
	default:
		ERROR_LOG(FRAMEBUF, "glReadPixels: %08x", error);
		break;
	}
}
#endif

bool OpenGLContext::CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat dataFormat, void *pixels, int pixelStride) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)src;
	fbo_bind_fb_target(true, fb ? fb->handle : 0);

	// Reads from the "bound for read" framebuffer.
	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		glReadBuffer(GL_COLOR_ATTACHMENT0);

	CHECK_GL_ERROR_IF_DEBUG();

	GLuint internalFormat;
	GLuint format;
	GLuint type;
	int alignment;
	if (!Thin3DFormatToFormatAndType(dataFormat, internalFormat, format, type, alignment)) {
		assert(false);
	}
	// Apply the correct alignment.
	glPixelStorei(GL_PACK_ALIGNMENT, alignment);
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		// Even if not required, some drivers seem to require we specify this.  See #8254.
		glPixelStorei(GL_PACK_ROW_LENGTH, pixelStride);
	}

	glReadPixels(x, y, w, h, format, type, pixels);
#ifdef DEBUG_READ_PIXELS
	LogReadPixelsError(glGetError());
#endif

	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	}
	CHECK_GL_ERROR_IF_DEBUG();
	return true;
}


Texture *OpenGLContext::CreateTexture(const TextureDesc &desc) {
	return new OpenGLTexture(desc);
}

OpenGLInputLayout::~OpenGLInputLayout() {
	if (id_) {
		glDeleteVertexArrays(1, &id_);
	}
}

void OpenGLInputLayout::Compile() {
	int semMask = 0;
	for (int i = 0; i < (int)desc.attributes.size(); i++) {
		semMask |= 1 << desc.attributes[i].location;
	}
	semanticsMask_ = semMask;

	if (gl_extensions.ARB_vertex_array_object && gl_extensions.IsCoreContext) {
		glGenVertexArrays(1, &id_);
	} else {
		id_ = 0;
	}
	needsEnable_ = true;
	lastBase_ = -1;
}

DepthStencilState *OpenGLContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	OpenGLDepthStencilState *ds = new OpenGLDepthStencilState();
	ds->depthTestEnabled = desc.depthTestEnabled;
	ds->depthWriteEnabled = desc.depthWriteEnabled;
	ds->depthComp = compToGL[(int)desc.depthCompare];
	ds->stencilEnabled = desc.stencilEnabled;
	ds->stencilCompareOp = compToGL[(int)desc.front.compareOp];
	ds->stencilPass = stencilOpToGL[(int)desc.front.passOp];
	ds->stencilFail = stencilOpToGL[(int)desc.front.failOp];
	ds->stencilZFail = stencilOpToGL[(int)desc.front.depthFailOp];
	ds->stencilWriteMask = desc.front.writeMask;
	ds->stencilReference = desc.front.reference;
	ds->stencilCompareMask = desc.front.compareMask;
	return ds;
}

BlendState *OpenGLContext::CreateBlendState(const BlendStateDesc &desc) {
	OpenGLBlendState *bs = new OpenGLBlendState();
	bs->enabled = desc.enabled;
	bs->eqCol = blendEqToGL[(int)desc.eqCol];
	bs->srcCol = blendFactorToGL[(int)desc.srcCol];
	bs->dstCol = blendFactorToGL[(int)desc.dstCol];
	bs->eqAlpha = blendEqToGL[(int)desc.eqAlpha];
	bs->srcAlpha = blendFactorToGL[(int)desc.srcAlpha];
	bs->dstAlpha = blendFactorToGL[(int)desc.dstAlpha];
#ifndef USING_GLES2
	bs->logicEnabled = desc.logicEnabled;
	bs->logicOp = logicOpToGL[(int)desc.logicOp];
#endif
	bs->colorMask = desc.colorMask;
	return bs;
}

SamplerState *OpenGLContext::CreateSamplerState(const SamplerStateDesc &desc) {
	OpenGLSamplerState *samps = new OpenGLSamplerState();
	samps->wrapU = texWrapToGL[(int)desc.wrapU];
	samps->wrapV = texWrapToGL[(int)desc.wrapV];
	samps->wrapW = texWrapToGL[(int)desc.wrapW];
	samps->magFilt = texFilterToGL[(int)desc.magFilter];
	samps->minFilt = texFilterToGL[(int)desc.minFilter];
	samps->mipMinFilt = texMipFilterToGL[(int)desc.minFilter][(int)desc.mipFilter];
	return samps;
}

RasterState *OpenGLContext::CreateRasterState(const RasterStateDesc &desc) {
	OpenGLRasterState *rs = new OpenGLRasterState();
	if (desc.cull == CullMode::NONE) {
		rs->cullEnable = GL_FALSE;
		return rs;
	}
	rs->cullEnable = GL_TRUE;
	switch (desc.frontFace) {
	case Facing::CW:
		rs->frontFace = GL_CW;
		break;
	case Facing::CCW:
		rs->frontFace = GL_CCW;
		break;
	}
	switch (desc.cull) {
	case CullMode::FRONT:
		rs->cullMode = GL_FRONT;
		break;
	case CullMode::BACK:
		rs->cullMode = GL_BACK;
		break;
	case CullMode::FRONT_AND_BACK:
		rs->cullMode = GL_FRONT_AND_BACK;
		break;
	case CullMode::NONE:
		// Unsupported
		break;
	}
	return rs;
}

class OpenGLBuffer : public Buffer {
public:
	OpenGLBuffer(size_t size, uint32_t flags) {
		glGenBuffers(1, &buffer_);
		target_ = (flags & BufferUsageFlag::INDEXDATA) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
		usage_ = 0;
		if (flags & BufferUsageFlag::DYNAMIC)
			usage_ = GL_STREAM_DRAW;
		else
			usage_ = GL_STATIC_DRAW;
		totalSize_ = size;
		glBindBuffer(target_, buffer_);
		glBufferData(target_, size, NULL, usage_);
	}
	~OpenGLBuffer() override {
		glDeleteBuffers(1, &buffer_);
	}

	void Bind(int offset) {
		// TODO: Can't support offset using ES 2.0
		glBindBuffer(target_, buffer_);
	}

	GLuint buffer_;
	GLuint target_;
	GLuint usage_;

	size_t totalSize_;
};

Buffer *OpenGLContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new OpenGLBuffer(size, usageFlags);
}

void OpenGLContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	OpenGLBuffer *buf = (OpenGLBuffer *)buffer;

	buf->Bind(0);
	if (size + offset > buf->totalSize_) {
		Crash();
	}
	// if (flags & UPDATE_DISCARD) we could try to orphan the buffer using glBufferData.
	glBufferSubData(buf->target_, offset, size, data);
}

Pipeline *OpenGLContext::CreateGraphicsPipeline(const PipelineDesc &desc) {
	if (!desc.shaders.size()) {
		ELOG("Pipeline requires at least one shader");
		return NULL;
	}
	OpenGLPipeline *pipeline = new OpenGLPipeline();
	for (auto iter : desc.shaders) {
		iter->AddRef();
		pipeline->shaders.push_back(static_cast<OpenGLShaderModule *>(iter));
	}
	if (pipeline->LinkShaders()) {
		// Build the rest of the virtual pipeline object.
		pipeline->prim = primToGL[(int)desc.prim];
		pipeline->depthStencil = (OpenGLDepthStencilState *)desc.depthStencil;
		pipeline->blend = (OpenGLBlendState *)desc.blend;
		pipeline->raster = (OpenGLRasterState *)desc.raster;
		pipeline->inputLayout = (OpenGLInputLayout *)desc.inputLayout;
		pipeline->depthStencil->AddRef();
		pipeline->blend->AddRef();
		pipeline->raster->AddRef();
		pipeline->inputLayout->AddRef();
		if (desc.uniformDesc)
			pipeline->dynamicUniforms = *desc.uniformDesc;
		return pipeline;
	} else {
		ELOG("Failed to create pipeline - shaders failed to link");
		delete pipeline;
		return NULL;
	}
}

void OpenGLContext::BindTextures(int start, int count, Texture **textures) {
	maxTextures_ = std::max(maxTextures_, start + count);
	for (int i = start; i < start + count; i++) {
		OpenGLTexture *glTex = static_cast<OpenGLTexture *>(textures[i]);
		glActiveTexture(GL_TEXTURE0 + i);
		if (!glTex) {
			boundTextures_[i] = 0;
			glBindTexture(GL_TEXTURE_2D, 0);
			continue;
		}
		glTex->Bind();
		boundTextures_[i] = glTex;
	}
	glActiveTexture(GL_TEXTURE0);
}

void OpenGLContext::ApplySamplers() {
	for (int i = 0; i < maxTextures_; i++) {
		if ((int)boundSamplers_.size() > i && boundSamplers_[i]) {
			const OpenGLSamplerState *samp = boundSamplers_[i];
			const OpenGLTexture *tex = boundTextures_[i];
			if (!tex)
				continue;
			if (tex->CanWrap()) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, samp->wrapU);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, samp->wrapV);
#ifndef USING_GLES2
				if (tex->GetType() == TextureType::LINEAR3D)
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, samp->wrapW);
#endif
			} else {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, samp->magFilt);
			if (tex->HasMips()) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samp->mipMinFilt);
			} else {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samp->minFilt);
			}
		}
	}
}

ShaderModule *OpenGLContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) {
	OpenGLShaderModule *shader = new OpenGLShaderModule(stage);
	if (shader->Compile(language, data, dataSize)) {
		return shader;
	} else {
		shader->Release();
		return nullptr;
	}
}

bool OpenGLPipeline::LinkShaders() {
	program_ = glCreateProgram();
	for (auto iter : shaders) {
		glAttachShader(program_, iter->GetShader());
	}

	// Bind all the common vertex data points. Mismatching ones will be ignored.
	glBindAttribLocation(program_, SEM_POSITION, "Position");
	glBindAttribLocation(program_, SEM_COLOR0, "Color0");
	glBindAttribLocation(program_, SEM_TEXCOORD0, "TexCoord0");
	glBindAttribLocation(program_, SEM_NORMAL, "Normal");
	glBindAttribLocation(program_, SEM_TANGENT, "Tangent");
	glBindAttribLocation(program_, SEM_BINORMAL, "Binormal");
	glLinkProgram(program_);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(program_, bufLength, NULL, buf);
			ELOG("Could not link program:\n %s", buf);
			// We've thrown out the source at this point. Might want to do something about that.
#ifdef _WIN32
			OutputDebugStringUTF8(buf);
#endif
			delete[] buf;
		} else {
			ELOG("Could not link program with %d shaders for unknown reason:", (int)shaders.size());
		}
		return false;
	}

	// Auto-initialize samplers.
	glUseProgram(program_);
	for (int i = 0; i < 4; i++) {
		char temp[256];
		sprintf(temp, "Sampler%i", i);
		int samplerLoc = GetUniformLoc(temp);
		if (samplerLoc != -1) {
			glUniform1i(samplerLoc, i);
		}
	}

	// Here we could (using glGetAttribLocation) save a bitmask about which pieces of vertex data are used in the shader
	// and then AND it with the vertex format bitmask later...
	return true;
}

int OpenGLPipeline::GetUniformLoc(const char *name) {
	auto iter = uniformCache_.find(name);
	int loc = -1;
	if (iter != uniformCache_.end()) {
		loc = iter->second.loc_;
	} else {
		loc = glGetUniformLocation(program_, name);
		UniformInfo info;
		info.loc_ = loc;
		uniformCache_[name] = info;
	}
	return loc;
}

void OpenGLContext::BindPipeline(Pipeline *pipeline) {
	curPipeline_ = (OpenGLPipeline *)pipeline;
	curPipeline_->blend->Apply();
	curPipeline_->depthStencil->Apply();
	curPipeline_->raster->Apply();
	glUseProgram(curPipeline_->program_);
}

void OpenGLContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	if (curPipeline_->dynamicUniforms.uniformBufferSize != size) {
		Crash();
	}

	for (auto &uniform : curPipeline_->dynamicUniforms.uniforms) {
		GLuint loc = curPipeline_->GetUniformLoc(uniform.name);
		if (loc == -1)
			Crash();
		const float *data = (const float *)((uint8_t *)ub + uniform.offset);
		switch (uniform.type) {
		case UniformType::FLOAT4:
			glUniform1fv(loc, 4, data);
			break;
		case UniformType::MATRIX4X4:
			glUniformMatrix4fv(loc, 1, false, data);
			break;
		}
	}
}

void OpenGLContext::Draw(int vertexCount, int offset) {
	curVBuffers_[0]->Bind(curVBufferOffsets_[0]);
	curPipeline_->inputLayout->Apply();
	ApplySamplers();

	glDrawArrays(curPipeline_->prim, offset, vertexCount);

	curPipeline_->inputLayout->Unapply();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLContext::DrawIndexed(int vertexCount, int offset) {
	curVBuffers_[0]->Bind(curVBufferOffsets_[0]);
	curPipeline_->inputLayout->Apply();
	ApplySamplers();
	// Note: ibuf binding is stored in the VAO, so call this after binding the fmt.
	curIBuffer_->Bind(curIBufferOffset_);

	glDrawElements(curPipeline_->prim, vertexCount, GL_UNSIGNED_INT, (const void *)(size_t)offset);
	
	curPipeline_->inputLayout->Unapply();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void OpenGLContext::DrawUP(const void *vdata, int vertexCount) {
	curPipeline_->inputLayout->Apply(vdata);
	ApplySamplers();

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDrawArrays(curPipeline_->prim, 0, vertexCount);

	curPipeline_->inputLayout->Unapply();
}

void OpenGLContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	float col[4];
	Uint8x4ToFloat4(col, colorval);
	GLuint glMask = 0;
	if (mask & FBChannel::FB_COLOR_BIT) {
		glClearColor(col[0], col[1], col[2], col[3]);
		glMask |= GL_COLOR_BUFFER_BIT;
	}
	if (mask & FBChannel::FB_DEPTH_BIT) {
#if defined(USING_GLES2)
		glClearDepthf(depthVal);
#else
		glClearDepth(depthVal);
#endif
		glMask |= GL_DEPTH_BUFFER_BIT;
	}
	if (mask & FBChannel::FB_STENCIL_BIT) {
		glClearStencil(stencilVal);
		glMask |= GL_STENCIL_BUFFER_BIT;
	}
	glClear(glMask);
}

DrawContext *T3DCreateGLContext() {
	return new OpenGLContext();
}

void OpenGLInputLayout::Apply(const void *base) {
	if (id_ != 0) {
		glBindVertexArray(id_);
	}

	if (needsEnable_ || id_ == 0) {
		for (int i = 0; i < SEM_MAX; i++) {
			if (semanticsMask_ & (1 << i)) {
				glEnableVertexAttribArray(i);
			}
		}
		if (id_ != 0) {
			needsEnable_ = false;
		}
	}

	intptr_t b = (intptr_t)base;
	if (b != lastBase_) {
		for (size_t i = 0; i < desc.attributes.size(); i++) {
			GLsizei stride = (GLsizei)desc.bindings[desc.attributes[i].binding].stride;
			switch (desc.attributes[i].format) {
			case DataFormat::R32G32_FLOAT:
				glVertexAttribPointer(desc.attributes[i].location, 2, GL_FLOAT, GL_FALSE, stride, (void *)(b + (intptr_t)desc.attributes[i].offset));
				break;
			case DataFormat::R32G32B32_FLOAT:
				glVertexAttribPointer(desc.attributes[i].location, 3, GL_FLOAT, GL_FALSE, stride, (void *)(b + (intptr_t)desc.attributes[i].offset));
				break;
			case DataFormat::R32G32B32A32_FLOAT:
				glVertexAttribPointer(desc.attributes[i].location, 4, GL_FLOAT, GL_FALSE, stride, (void *)(b + (intptr_t)desc.attributes[i].offset));
				break;
			case DataFormat::R8G8B8A8_UNORM:
				glVertexAttribPointer(desc.attributes[i].location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)(b + (intptr_t)desc.attributes[i].offset));
				break;
			case DataFormat::UNDEFINED:
			default:
				ELOG("Thin3DGLVertexFormat: Invalid or unknown component type applied.");
				break;
			}
		}
		if (id_ != 0) {
			lastBase_ = b;
		}
	}
}

void OpenGLInputLayout::Unapply() {
	if (id_ == 0) {
		for (int i = 0; i < (int)SEM_MAX; i++) {
			if (semanticsMask_ & (1 << i)) {
				glDisableVertexAttribArray(i);
			}
		}
	} else {
		glBindVertexArray(0);
	}
}

// On PC, we always use GL_DEPTH24_STENCIL8. 
// On Android, we try to use what's available.

#ifndef USING_GLES2
OpenGLFramebuffer *OpenGLContext::fbo_ext_create(const FramebufferDesc &desc) {
	OpenGLFramebuffer *fbo = new OpenGLFramebuffer();
	fbo->width = desc.width;
	fbo->height = desc.height;
	fbo->colorDepth = desc.colorDepth;

	// Color texture is same everywhere
	glGenFramebuffersEXT(1, &fbo->handle);
	glGenTextures(1, &fbo->color_texture);

	// Create the surfaces.
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// TODO: We could opt to only create 16-bit render targets on slow devices. For later.
	switch (fbo->colorDepth) {
	case FBO_8888:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		break;
	case FBO_4444:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
		break;
	case FBO_5551:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, NULL);
		break;
	case FBO_565:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fbo->width, fbo->height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
		break;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	fbo->stencil_buffer = 0;
	fbo->z_buffer = 0;
	// 24-bit Z, 8-bit stencil
	glGenRenderbuffersEXT(1, &fbo->z_stencil_buffer);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_STENCIL_EXT, fbo->width, fbo->height);
	//glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8, width, height);

	// Bind it all together
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->handle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, fbo->color_texture, 0);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);

	GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		// ILOG("Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		ELOG("GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		ELOG("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ");
		break;
	default:
		FLOG("Other framebuffer error: %i", status);
		break;
	}
	// Unbind state we don't need
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	currentDrawHandle_ = fbo->handle;
	currentReadHandle_ = fbo->handle;
	return fbo;
}
#endif

Framebuffer *OpenGLContext::CreateFramebuffer(const FramebufferDesc &desc) {
	CheckGLExtensions();

#ifndef USING_GLES2
	if (!gl_extensions.ARB_framebuffer_object && gl_extensions.EXT_framebuffer_object) {
		return fbo_ext_create(desc);
	} else if (!gl_extensions.ARB_framebuffer_object) {
		return nullptr;
	}
	// If GLES2, we have basic FBO support and can just proceed.
#endif
	CHECK_GL_ERROR_IF_DEBUG();

	OpenGLFramebuffer *fbo = new OpenGLFramebuffer();
	fbo->width = desc.width;
	fbo->height = desc.height;
	fbo->colorDepth = desc.colorDepth;

	// Color texture is same everywhere
	glGenFramebuffers(1, &fbo->handle);
	glGenTextures(1, &fbo->color_texture);

	// Create the surfaces.
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// TODO: We could opt to only create 16-bit render targets on slow devices. For later.
	switch (fbo->colorDepth) {
	case FBO_8888:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		break;
	case FBO_4444:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
		break;
	case FBO_5551:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, NULL);
		break;
	case FBO_565:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fbo->width, fbo->height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
		break;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (gl_extensions.IsGLES) {
		if (gl_extensions.OES_packed_depth_stencil) {
			ILOG("Creating %i x %i FBO using DEPTH24_STENCIL8", fbo->width, fbo->height);
			// Standard method
			fbo->stencil_buffer = 0;
			fbo->z_buffer = 0;
			// 24-bit Z, 8-bit stencil combined
			glGenRenderbuffers(1, &fbo->z_stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, fbo->width, fbo->height);

			// Bind it all together
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
		} else {
			ILOG("Creating %i x %i FBO using separate stencil", fbo->width, fbo->height);
			// TEGRA
			fbo->z_stencil_buffer = 0;
			// 16/24-bit Z, separate 8-bit stencil
			glGenRenderbuffers(1, &fbo->z_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_buffer);
			// Don't forget to make sure fbo_standard_z_depth() matches.
			glRenderbufferStorage(GL_RENDERBUFFER, gl_extensions.OES_depth24 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16, fbo->width, fbo->height);

			// 8-bit stencil buffer
			glGenRenderbuffers(1, &fbo->stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->stencil_buffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, fbo->width, fbo->height);

			// Bind it all together
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_buffer);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->stencil_buffer);
		}
	} else {
		fbo->stencil_buffer = 0;
		fbo->z_buffer = 0;
		// 24-bit Z, 8-bit stencil
		glGenRenderbuffers(1, &fbo->z_stencil_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbo->width, fbo->height);

		// Bind it all together
		glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE:
		// ILOG("Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		ELOG("GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		ELOG("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ");
		break;
	default:
		FLOG("Other framebuffer error: %i", status);
		break;
	}

	// Unbind state we don't need
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	CHECK_GL_ERROR_IF_DEBUG();

	currentDrawHandle_ = fbo->handle;
	currentReadHandle_ = fbo->handle;
	return fbo;
}

GLenum OpenGLContext::fbo_get_fb_target(bool read, GLuint **cached) {
	bool supportsBlit = gl_extensions.ARB_framebuffer_object;
	if (gl_extensions.IsGLES) {
		supportsBlit = (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit);
	}

	// Note: GL_FRAMEBUFFER_EXT and GL_FRAMEBUFFER have the same value, same with _NV.
	if (supportsBlit) {
		if (read) {
			*cached = &currentReadHandle_;
			return GL_READ_FRAMEBUFFER;
		} else {
			*cached = &currentDrawHandle_;
			return GL_DRAW_FRAMEBUFFER;
		}
	} else {
		*cached = &currentDrawHandle_;
		return GL_FRAMEBUFFER;
	}
}

void OpenGLContext::fbo_bind_fb_target(bool read, GLuint name) {
	GLuint *cached;
	GLenum target = fbo_get_fb_target(read, &cached);
	if (*cached != name) {
		if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
			glBindFramebuffer(target, name);
		} else {
#ifndef USING_GLES2
			glBindFramebufferEXT(target, name);
#endif
		}
		*cached = name;
	}
}

void OpenGLContext::fbo_unbind() {
#ifndef USING_GLES2
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
		glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
	} else if (gl_extensions.EXT_framebuffer_object) {
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_defaultFBO);
	}
#else
	glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
#endif

#ifdef IOS
	bindDefaultFBO();
#endif

	currentDrawHandle_ = 0;
	currentReadHandle_ = 0;
}

void OpenGLContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) {
	CHECK_GL_ERROR_IF_DEBUG();
	curFB_ = (OpenGLFramebuffer *)fbo;
	if (fbo) {
		OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
		// Without FBO_ARB / GLES3, this will collide with bind_for_read, but there's nothing
		// in ES 2.0 that actually separate them anyway of course, so doesn't matter.
		fbo_bind_fb_target(false, fb->handle);
		// Always restore viewport after render target binding. Works around driver bugs.
		glstate.viewport.restore();
	} else {
		fbo_unbind();
	}
	int clearFlags = 0;
	if (rp.color == RPAction::CLEAR) {
		float fc[4]{};
		if (rp.clearColor) {
			Uint8x4ToFloat4(fc, rp.clearColor);
		}
		glClearColor(fc[0], fc[1], fc[2], fc[3]);
		clearFlags |= GL_COLOR_BUFFER_BIT;
		glstate.colorMask.force(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	if (rp.depth == RPAction::CLEAR) {
#ifdef USING_GLES2
		glClearDepthf(rp.clearDepth);
#else
		glClearDepth(rp.clearDepth);
#endif
		clearFlags |= GL_DEPTH_BUFFER_BIT;
		glstate.depthWrite.force(GL_TRUE);
	}
	if (rp.stencil == RPAction::CLEAR) {
		glClearStencil(rp.clearStencil);
		clearFlags |= GL_STENCIL_BUFFER_BIT;
		glstate.stencilFunc.force(GL_ALWAYS, 0, 0);
		glstate.stencilMask.force(0xFF);
	}
	if (clearFlags) {
		glstate.scissorTest.force(false);
		glClear(clearFlags);
		glstate.scissorTest.restore();
	}
	if (rp.color == RPAction::CLEAR) {
		glstate.colorMask.restore();
	}
	if (rp.depth == RPAction::CLEAR) {
		glstate.depthWrite.restore();
	}
	if (rp.stencil == RPAction::CLEAR) {
		glstate.stencilFunc.restore();
		glstate.stencilMask.restore();
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void OpenGLContext::CopyFramebufferImage(Framebuffer *fbsrc, int srcLevel, int srcX, int srcY, int srcZ, Framebuffer *fbdst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;
	GLuint srcTex = 0;
	GLuint dstTex = 0;
	GLuint target = GL_TEXTURE_2D;
	switch (channelBits) {
	case FB_COLOR_BIT:
		srcTex = src->color_texture;
		dstTex = dst->color_texture;
		break;
	case FB_DEPTH_BIT:
		target = GL_RENDERBUFFER;
		srcTex = src->z_buffer ? src->z_buffer : src->z_stencil_buffer;
		dstTex = dst->z_buffer ? dst->z_buffer : dst->z_stencil_buffer;
		break;
	}
#if defined(USING_GLES2)
#ifndef IOS
	glCopyImageSubDataOES(
		srcTex, target, srcLevel, srcX, srcY, srcZ,
		dstTex, target, dstLevel, dstX, dstY, dstZ,
		width, height, depth);
#endif
#else
	if (gl_extensions.ARB_copy_image) {
		glCopyImageSubData(
			srcTex, target, srcLevel, srcX, srcY, srcZ,
			dstTex, target, dstLevel, dstX, dstY, dstZ,
			width, height, depth);
	} else if (gl_extensions.NV_copy_image) {
		// Older, pre GL 4.x NVIDIA cards.
		glCopyImageSubDataNV(
			srcTex, target, srcLevel, srcX, srcY, srcZ,
			dstTex, target, dstLevel, dstX, dstY, dstZ,
			width, height, depth);
	}
#endif
	CHECK_GL_ERROR_IF_DEBUG();
}

bool OpenGLContext::BlitFramebuffer(Framebuffer *fbsrc, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *fbdst, int dstX1, int dstY1, int dstX2, int dstY2, int channels, FBBlitFilter linearFilter) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;
	GLuint bits = 0;
	if (channels & FB_COLOR_BIT)
		bits |= GL_COLOR_BUFFER_BIT;
	if (channels & FB_DEPTH_BIT)
		bits |= GL_DEPTH_BUFFER_BIT;
	if (channels & FB_STENCIL_BIT)
		bits |= GL_STENCIL_BUFFER_BIT;
	// Without FBO_ARB / GLES3, this will collide with bind_for_read, but there's nothing
	// in ES 2.0 that actually separate them anyway of course, so doesn't matter.
	fbo_bind_fb_target(false, dst->handle);
	fbo_bind_fb_target(true, src->handle);
	if (gl_extensions.GLES3 || gl_extensions.ARB_framebuffer_object) {
		glBlitFramebuffer(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, bits, linearFilter == FB_BLIT_LINEAR ? GL_LINEAR : GL_NEAREST);
		CHECK_GL_ERROR_IF_DEBUG();
#if defined(USING_GLES2) && defined(__ANDROID__)  // We only support this extension on Android, it's not even available on PC.
		return true;
	} else if (gl_extensions.NV_framebuffer_blit) {
		glBlitFramebufferNV(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, bits, linearFilter == FB_BLIT_LINEAR ? GL_LINEAR : GL_NEAREST);
		CHECK_GL_ERROR_IF_DEBUG();
#endif // defined(USING_GLES2) && defined(__ANDROID__)
		return true;
	} else {
		return false;
	}
}

uintptr_t OpenGLContext::GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	switch (channelBits) {
	case FB_COLOR_BIT: return (uintptr_t)fb->color_texture;
	case FB_DEPTH_BIT: return (uintptr_t)(fb->z_buffer ? fb->z_buffer : fb->z_stencil_buffer);
	default:
		return 0;
	}
}

void OpenGLContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int color) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	if (!fb)
		return;
	if (binding != 0)
		glActiveTexture(GL_TEXTURE0 + binding);
	switch (channelBit) {
	case FB_DEPTH_BIT:
		glBindTexture(GL_TEXTURE_2D, fb->z_buffer ? fb->z_buffer : fb->z_stencil_buffer);
	case FB_COLOR_BIT:
	default:
		glBindTexture(GL_TEXTURE_2D, fb->color_texture);
		break;
	}
	glActiveTexture(GL_TEXTURE0);
}

OpenGLFramebuffer::~OpenGLFramebuffer() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
		if (handle) {
			glBindFramebuffer(GL_FRAMEBUFFER, handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
			glDeleteFramebuffers(1, &handle);
		}
		if (z_stencil_buffer)
			glDeleteRenderbuffers(1, &z_stencil_buffer);
		if (z_buffer)
			glDeleteRenderbuffers(1, &z_buffer);
		if (stencil_buffer)
			glDeleteRenderbuffers(1, &stencil_buffer);
	} else if (gl_extensions.EXT_framebuffer_object) {
#ifndef USING_GLES2
		if (handle) {
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, handle);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER_EXT, 0);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_defaultFBO);
			glDeleteFramebuffersEXT(1, &handle);
		}
		if (z_stencil_buffer)
			glDeleteRenderbuffers(1, &z_stencil_buffer);
		if (z_buffer)
			glDeleteRenderbuffers(1, &z_buffer);
		if (stencil_buffer)
			glDeleteRenderbuffers(1, &stencil_buffer);
#endif
	}

	glDeleteTextures(1, &color_texture);
}

void OpenGLContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	if (fb) {
		*w = fb->width;
		*h = fb->height;
	} else {
		*w = targetWidth_;
		*h = targetHeight_;
	}
}

uint32_t OpenGLContext::GetDataFormatSupport(DataFormat fmt) const {
	switch (fmt) {
	case DataFormat::B8G8R8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_AUTOGEN_MIPS;
	case DataFormat::B4G4R4A4_UNORM_PACK16:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_AUTOGEN_MIPS;  // native support
	case DataFormat::A4R4G4B4_UNORM_PACK16:
#ifndef USING_GLES2
		// Can support this if _REV formats are supported.
		return FMT_TEXTURE;
#endif
		return 0;

	case DataFormat::R8G8B8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_INPUTLAYOUT | FMT_AUTOGEN_MIPS;

	case DataFormat::R32_FLOAT:
	case DataFormat::R32G32_FLOAT:
	case DataFormat::R32G32B32_FLOAT:
	case DataFormat::R32G32B32A32_FLOAT:
		return FMT_INPUTLAYOUT;

	case DataFormat::R8_UNORM:
		return 0;
	case DataFormat::BC1_RGBA_UNORM_BLOCK:
	case DataFormat::BC2_UNORM_BLOCK:
	case DataFormat::BC3_UNORM_BLOCK:
		return FMT_TEXTURE;
	default:
		return 0;
	}
}

}  // namespace Draw
