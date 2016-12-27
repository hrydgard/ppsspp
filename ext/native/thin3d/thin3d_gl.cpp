#include <stdio.h>
#include <vector>
#include <string>
#include <map>

#include "base/logging.h"
#include "image/zim_load.h"
#include "math/dataconv.h"
#include "math/lin/matrix4x4.h"
#include "thin3d/thin3d.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "gfx/gl_lost_manager.h"

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
	GL_REPEAT,
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

static const char *glsl_fragment_prelude =
"#ifdef GL_ES\n"
"precision mediump float;\n"
"#endif\n";

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
	// Old school. Should also support using a sampler object.

	GLint wrapS;
	GLint wrapT;
	GLint magFilt;
	GLint minFilt;
	GLint mipMinFilt;

	void Apply(bool hasMips, bool canWrap) {
		if (canWrap) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
		} else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilt);
		if (hasMips) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipMinFilt);
		} else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilt);
		}
	}
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
		glDisable(GL_STENCIL_TEST);
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

class OpenGLBuffer : public Buffer, GfxResourceHolder {
public:
	OpenGLBuffer(size_t size, uint32_t flags) {
		glGenBuffers(1, &buffer_);
		target_ = (flags & BufferUsageFlag::INDEXDATA) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
		usage_ = 0;
		if (flags & BufferUsageFlag::DYNAMIC)
			usage_ = GL_STREAM_DRAW;
		else
			usage_ = GL_STATIC_DRAW;
		knownSize_ = 0;
		register_gl_resource_holder(this);
	}
	~OpenGLBuffer() override {
		unregister_gl_resource_holder(this);
		glDeleteBuffers(1, &buffer_);
	}

	void SetData(const uint8_t *data, size_t size) override {
		Bind();
		glBufferData(target_, size, data, usage_);
		knownSize_ = size;
	}

	void SubData(const uint8_t *data, size_t offset, size_t size) override {
		Bind();
		if (size + offset > knownSize_) {
			// Allocate the buffer.
			glBufferData(target_, size + offset, NULL, usage_);
			knownSize_ = size + offset;
		}
		glBufferSubData(target_, offset, size, data);
	}
	void Bind() {
		glBindBuffer(target_, buffer_);
	}

	void GLLost() override {
		buffer_ = 0;
	}

	void GLRestore() override {
		ILOG("Recreating vertex buffer after gl_restore");
		knownSize_ = 0;  // Will cause a new glBufferData call. Should genBuffers again though?
		glGenBuffers(1, &buffer_);
	}

private:
	GLuint buffer_;
	GLuint target_;
	GLuint usage_;

	size_t knownSize_;
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

// Not registering this as a resource holder, instead Pipeline is registered. It will
// invoke Compile again to recreate the shader then link them together.
class OpenGLShaderModule : public ShaderModule {
public:
	OpenGLShaderModule(ShaderStage stage) : stage_(stage), shader_(0) {
		glstage_ = ShaderStageToOpenGL(stage);
	}

	~OpenGLShaderModule() {
		glDeleteShader(shader_);
	}

	bool Compile(const char *source);
	GLuint GetShader() const {
		return shader_;
	}
	const std::string &GetSource() const { return source_; }

	void Unset() {
		shader_ = 0;
	}
	ShaderStage GetStage() const override {
		return stage_;
	}

private:
	ShaderStage stage_;
	GLuint shader_;
	GLuint glstage_;
	bool ok_;
	std::string source_;  // So we can recompile in case of context loss.
};

bool OpenGLShaderModule::Compile(const char *source) {
	source_ = source;
	shader_ = glCreateShader(glstage_);

	std::string temp;
	// Add the prelude on automatically for fragment shaders.
	if (glstage_ == GL_FRAGMENT_SHADER) {
		temp = std::string(glsl_fragment_prelude) + source;
		source = temp.c_str();
	}

	glShaderSource(shader_, 1, &source, nullptr);
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

class OpenGLInputLayout : public InputLayout, GfxResourceHolder {
public:
	~OpenGLInputLayout();

	void Apply(const void *base = nullptr);
	void Unapply();
	void Compile();
	void GLRestore() override;
	void GLLost() override;
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

// TODO: Add Uniform Buffer support.
class OpenGLPipeline : public Pipeline, GfxResourceHolder {
public:
	OpenGLPipeline() {
		program_ = 0;
		register_gl_resource_holder(this);
	}
	~OpenGLPipeline() {
		unregister_gl_resource_holder(this);
		for (auto iter : shaders) {
			iter->Release();
		}
		glDeleteProgram(program_);
		if (depthStencil) depthStencil->Release();
		if (blend) blend->Release();
		if (raster) raster->Release();
		if (inputLayout) inputLayout->Release();
	}
	bool RequiresBuffer() override {
		return inputLayout->RequiresBuffer();
	}

	bool LinkShaders();

	void Apply();
	void Unapply();

	int GetUniformLoc(const char *name);

	void SetVector(const char *name, float *value, int n) override;
	void SetMatrix4x4(const char *name, const float value[16]) override;

	void GLLost() override {
		program_ = 0;
		for (auto iter : shaders) {
			iter->Unset();
		}
	}

	void GLRestore() override {
		for (auto iter : shaders) {
			iter->Compile(iter->GetSource().c_str());
		}
		LinkShaders();
	}

	GLuint prim;
	std::vector<OpenGLShaderModule *> shaders;
	OpenGLInputLayout *inputLayout = nullptr;
	OpenGLDepthStencilState *depthStencil = nullptr;
	OpenGLBlendState *blend = nullptr;
	OpenGLRasterState *raster = nullptr;

private:
	GLuint program_;
	std::map<std::string, UniformInfo> uniforms_;
};

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
		return (uint32_t)ShaderLanguage::GLSL_410;
#endif
	}

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	Texture *CreateTexture() override;

	void BindSamplerStates(int start, int count, SamplerState **states) override {
		if (samplerStates_.size() < (size_t)(start + count)) {
			samplerStates_.resize(start + count);
		}
		for (int i = 0; i < count; ++i) {
			int index = i + start;
			OpenGLSamplerState *s = static_cast<OpenGLSamplerState *>(states[index]);

			if (samplerStates_[index]) {
				samplerStates_[index]->Release();
			}
			samplerStates_[index] = s;
			samplerStates_[index]->AddRef();

			// TODO: Ideally, get these from the texture and apply on the right stage?
			if (index == 0) {
				s->Apply(false, true);
			}
		}
	}

	void SetScissorRect(int left, int top, int width, int height) override {
		glScissor(left, targetHeight_ - (top + height), width, height);
	}

	void SetViewports(int count, Viewport *viewports) override {
		// TODO: Add support for multiple viewports.
		glViewport(viewports[0].TopLeftX, viewports[0].TopLeftY, viewports[0].Width, viewports[0].Height);
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

	// TODO: Add more sophisticated draws.
	void Draw(Buffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) override;
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
				switch (gl_extensions.gpuVendor) {
				case GPU_VENDOR_AMD: return "VENDOR_AMD";
				case GPU_VENDOR_POWERVR: return "VENDOR_POWERVR";
				case GPU_VENDOR_NVIDIA: return "VENDOR_NVIDIA";
				case GPU_VENDOR_INTEL: return "VENDOR_INTEL";
				case GPU_VENDOR_ADRENO: return "VENDOR_ADRENO";
				case GPU_VENDOR_ARM: return "VENDOR_ARM";
				case GPU_VENDOR_BROADCOM: return "VENDOR_BROADCOM";
				case GPU_VENDOR_UNKNOWN:
				default:
					return "VENDOR_UNKNOWN";
				}
				break;
			case RENDERER: return (const char *)glGetString(GL_RENDERER);
			case SHADELANGVERSION: return (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
			case APIVERSION: return (const char *)glGetString(GL_VERSION);
			default: return "?";
		}
	}

	std::vector<OpenGLSamplerState *> samplerStates_;
	OpenGLPipeline *curPipeline_;
	DeviceCaps caps_;
};

OpenGLContext::OpenGLContext() {
	CreatePresets();
	// TODO: Detect caps
}

OpenGLContext::~OpenGLContext() {
	for (OpenGLSamplerState *s : samplerStates_) {
		if (s) {
			s->Release();
		}
	}
	samplerStates_.clear();
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
	case LINEAR1D: return GL_TEXTURE_1D;
#endif
	case LINEAR2D: return GL_TEXTURE_2D;
	case LINEAR3D: return GL_TEXTURE_3D;
	case CUBE: return GL_TEXTURE_CUBE_MAP;
#ifndef USING_GLES2
	case ARRAY1D: return GL_TEXTURE_1D_ARRAY;
#endif
	case ARRAY2D: return GL_TEXTURE_2D_ARRAY;
	default: return GL_NONE;
	}
}

class Thin3DGLTexture : public Texture, GfxResourceHolder {
public:
	Thin3DGLTexture() : tex_(0), target_(0) {
		generatedMips_ = false;
		canWrap_ = true;
		width_ = 0;
		height_ = 0;
		depth_ = 0;
		glGenTextures(1, &tex_);
		register_gl_resource_holder(this);
	}
	Thin3DGLTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) : tex_(0), target_(TypeToTarget(type)), format_(format), mipLevels_(mipLevels) {
		generatedMips_ = false;
		canWrap_ = true;
		width_ = width;
		height_ = height;
		depth_ = depth;
		glGenTextures(1, &tex_);
		register_gl_resource_holder(this);
	}
	~Thin3DGLTexture() {
		unregister_gl_resource_holder(this);
		Destroy();
	}

	bool Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override {
		generatedMips_ = false;
		canWrap_ = true;
		format_ = format;
		target_ = TypeToTarget(type);
		mipLevels_ = mipLevels;
		width_ = width;
		height_ = height;
		depth_ = depth;
		return true;
	}

	void Destroy() {
		if (tex_) {
			glDeleteTextures(1, &tex_);
			tex_ = 0;
			generatedMips_ = false;
		}
	}
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;
	void AutoGenMipmaps() override;

	bool HasMips() {
		return mipLevels_ > 1 || generatedMips_;
	}
	bool CanWrap() {
		return canWrap_;
	}

	void Bind() {
		glBindTexture(target_, tex_);
	}

	void GLLost() override {
		// We can assume that the texture is gone.
		tex_ = 0;
		generatedMips_ = false;
	}

	void GLRestore() override {
		if (!filename_.empty()) {
			if (LoadFromFile(filename_.c_str())) {
				ILOG("Reloaded lost texture %s", filename_.c_str());
			} else {
				ELOG("Failed to reload lost texture %s", filename_.c_str());
				tex_ = 0;
			}
		} else {
			WLOG("Texture %p cannot be restored - has no filename", this);
			tex_ = 0;
		}
	}

	void Finalize(int zim_flags) override;

private:
	GLuint tex_;
	GLuint target_;

	DataFormat format_;
	int mipLevels_;
	bool generatedMips_;
	bool canWrap_;
};

Texture *OpenGLContext::CreateTexture() {
	return new Thin3DGLTexture();
}

Texture *OpenGLContext::CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
	return new Thin3DGLTexture(type, format, width, height, depth, mipLevels);
}

void Thin3DGLTexture::AutoGenMipmaps() {
	if (!generatedMips_) {
		Bind();
		glGenerateMipmap(target_);
		// TODO: Really, this should follow the sampler state.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		generatedMips_ = true;
	}
}

void Thin3DGLTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	int internalFormat;
	int format;
	int type;
	switch (format_) {
	case DataFormat::R8G8B8A8_UNORM:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;
	case DataFormat::R4G4B4A4_UNORM:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4;
		break;
	default:
		return;
	}
	if (level == 0) {
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	Bind();
	switch (target_) {
	case GL_TEXTURE_2D:
		glTexImage2D(GL_TEXTURE_2D, level, internalFormat, width_, height_, 0, format, type, data);
		break;
	default:
		ELOG("Thin3D GL: Targets other than GL_TEXTURE_2D not yet supported");
		break;
	}
}

bool isPowerOf2(int n) {
	return n == 1 || (n & (n - 1)) == 0;
}

void Thin3DGLTexture::Finalize(int zim_flags) {
	canWrap_ = (zim_flags & ZIM_CLAMP) || !isPowerOf2(width_) || !isPowerOf2(height_);
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

void OpenGLInputLayout::GLLost() {
	id_ = 0;
}

void OpenGLInputLayout::GLRestore() {
	Compile();
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
	samps->wrapS = texWrapToGL[(int)desc.wrapU];
	samps->wrapT = texWrapToGL[(int)desc.wrapV];
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
	}
	return rs;
}

Buffer *OpenGLContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new OpenGLBuffer(size, usageFlags);
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
		return pipeline;
	} else {
		delete pipeline;
		return NULL;
	}
}

void OpenGLContext::BindTextures(int start, int count, Texture **textures) {
	for (int i = start; i < start + count; i++) {
		Thin3DGLTexture *glTex = static_cast<Thin3DGLTexture *>(textures[i]);
		glActiveTexture(GL_TEXTURE0 + i);
		glTex->Bind();

		if ((int)samplerStates_.size() > i && samplerStates_[i]) {
			samplerStates_[i]->Apply(glTex->HasMips(), glTex->CanWrap());
		}
	}
	glActiveTexture(GL_TEXTURE0);
}


ShaderModule *OpenGLContext::CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	OpenGLShaderModule *shader = new OpenGLShaderModule(stage);
	if (shader->Compile(glsl_source)) {
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
	auto iter = uniforms_.find(name);
	int loc = -1;
	if (iter != uniforms_.end()) {
		loc = iter->second.loc_;
	} else {
		loc = glGetUniformLocation(program_, name);
		UniformInfo info;
		info.loc_ = loc;
		uniforms_[name] = info;
	}
	return loc;
}

void OpenGLPipeline::SetVector(const char *name, float *value, int n) {
	glUseProgram(program_);
	int loc = GetUniformLoc(name);
	if (loc != -1) {
		switch (n) {
		case 1: glUniform1fv(loc, 1, value); break;
		case 2: glUniform1fv(loc, 2, value); break;
		case 3: glUniform1fv(loc, 3, value); break;
		case 4: glUniform1fv(loc, 4, value); break;
		}
	}
}

void OpenGLPipeline::SetMatrix4x4(const char *name, const float value[16]) {
	glUseProgram(program_);
	int loc = GetUniformLoc(name);
	if (loc != -1) {
		glUniformMatrix4fv(loc, 1, false, value);
	}
}

void OpenGLPipeline::Apply() {
	glUseProgram(program_);
}

void OpenGLPipeline::Unapply() {
	glUseProgram(0);
}

void OpenGLContext::BindPipeline(Pipeline *pipeline) {
	curPipeline_ = (OpenGLPipeline *)pipeline;
	curPipeline_->blend->Apply();
	curPipeline_->depthStencil->Apply();
	curPipeline_->raster->Apply();
}

void OpenGLContext::Draw(Buffer *vdata, int vertexCount, int offset) {
	OpenGLBuffer *vbuf = static_cast<OpenGLBuffer *>(vdata);

	vbuf->Bind();
	curPipeline_->inputLayout->Apply();
	curPipeline_->Apply();

	glDrawArrays(curPipeline_->prim, offset, vertexCount);

	curPipeline_->Unapply();
	curPipeline_->inputLayout->Unapply();
}

void OpenGLContext::DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) {
	OpenGLBuffer *vbuf = static_cast<OpenGLBuffer *>(vdata);
	OpenGLBuffer *ibuf = static_cast<OpenGLBuffer *>(idata);

	vbuf->Bind();
	curPipeline_->inputLayout->Apply();
	curPipeline_->Apply();
	// Note: ibuf binding is stored in the VAO, so call this after binding the fmt.
	ibuf->Bind();

	glDrawElements(curPipeline_->prim, vertexCount, GL_UNSIGNED_INT, (const void *)(size_t)offset);
	
	curPipeline_->Unapply();
	curPipeline_->inputLayout->Unapply();
}

void OpenGLContext::DrawUP(const void *vdata, int vertexCount) {
	curPipeline_->inputLayout->Apply(vdata);
	curPipeline_->Apply();

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDrawArrays(curPipeline_->prim, 0, vertexCount);

	curPipeline_->Unapply();
	curPipeline_->inputLayout->Unapply();
}

void OpenGLContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	float col[4];
	Uint8x4ToFloat4(col, colorval);
	GLuint glMask = 0;
	if (mask & ClearFlag::COLOR) {
		glClearColor(col[0], col[1], col[2], col[3]);
		glMask |= GL_COLOR_BUFFER_BIT;
	}
	if (mask & ClearFlag::DEPTH) {
#if defined(USING_GLES2)
		glClearDepthf(depthVal);
#else
		glClearDepth(depthVal);
#endif
		glMask |= GL_DEPTH_BUFFER_BIT;
	}
	if (mask & ClearFlag::STENCIL) {
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

}  // namespace Draw