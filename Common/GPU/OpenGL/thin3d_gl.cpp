#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

#include "ppsspp_config.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/OpenGL/DataFormatGL.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/GPU/OpenGL/GLRenderManager.h"

// #define DEBUG_READ_PIXELS 1

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
#elif !PPSSPP_PLATFORM(IOS)
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
};

class OpenGLBuffer;

class OpenGLBlendState : public BlendState {
public:
	bool enabled;
	GLuint eqCol, eqAlpha;
	GLuint srcCol, srcAlpha, dstCol, dstAlpha;
	int colorMask;
	// uint32_t fixedColor;

	void Apply(GLRenderManager *render) {
		render->SetBlendAndMask(colorMask, enabled, srcCol, dstCol, srcAlpha, dstAlpha, eqCol, eqAlpha);
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
	// TODO: Two-sided. Although in practice, do we care?
	bool stencilEnabled;
	GLuint stencilFail;
	GLuint stencilZFail;
	GLuint stencilPass;
	GLuint stencilCompareOp;
	uint8_t stencilCompareMask;
	uint8_t stencilWriteMask;

	void Apply(GLRenderManager *render, uint8_t stencilRef) {
		render->SetDepth(depthTestEnabled, depthWriteEnabled, depthComp);
		render->SetStencilFunc(stencilEnabled, stencilCompareOp, stencilRef, stencilCompareMask);
		render->SetStencilOp(stencilWriteMask, stencilFail, stencilZFail, stencilPass);
	}
};

class OpenGLRasterState : public RasterState {
public:
	void Apply(GLRenderManager *render) {
		render->SetRaster(cullEnable, frontFace, cullMode, GL_FALSE, GL_FALSE);
	}

	GLboolean cullEnable;
	GLenum cullMode;
	GLenum frontFace;
};

GLuint ShaderStageToOpenGL(ShaderStage stage) {
	switch (stage) {
	case ShaderStage::Vertex: return GL_VERTEX_SHADER;
#ifndef USING_GLES2
	case ShaderStage::Compute: return GL_COMPUTE_SHADER;
	case ShaderStage::Geometry: return GL_GEOMETRY_SHADER;
#endif
	case ShaderStage::Fragment:
	default:
		return GL_FRAGMENT_SHADER;
	}
}

class OpenGLShaderModule : public ShaderModule {
public:
	OpenGLShaderModule(GLRenderManager *render, ShaderStage stage, const std::string &tag) : render_(render), stage_(stage), tag_(tag) {
		DEBUG_LOG(G3D, "Shader module created (%p)", this);
		glstage_ = ShaderStageToOpenGL(stage);
	}

	~OpenGLShaderModule() {
		if (shader_)
			render_->DeleteShader(shader_);
	}

	bool Compile(GLRenderManager *render, ShaderLanguage language, const uint8_t *data, size_t dataSize);
	GLRShader *GetShader() const {
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
	GLRenderManager *render_;
	ShaderStage stage_;
	ShaderLanguage language_ = GLSL_1xx;
	GLRShader *shader_ = nullptr;
	GLuint glstage_ = 0;
	std::string source_;  // So we can recompile in case of context loss.
	std::string tag_;
};

bool OpenGLShaderModule::Compile(GLRenderManager *render, ShaderLanguage language, const uint8_t *data, size_t dataSize) {
	source_ = std::string((const char *)data);
	// Add the prelude on automatically.
	if (glstage_ == GL_FRAGMENT_SHADER || glstage_ == GL_VERTEX_SHADER) {
		if (source_.find("#version") == source_.npos) {
			source_ = ApplyGLSLPrelude(source_, glstage_);
		}
	}

	shader_ = render->CreateShader(glstage_, source_, tag_);
	return true;
}

class OpenGLInputLayout : public InputLayout {
public:
	OpenGLInputLayout(GLRenderManager *render) : render_(render) {}
	~OpenGLInputLayout();

	void Compile(const InputLayoutDesc &desc);
	bool RequiresBuffer() {
		return false;
	}

	GLRInputLayout *inputLayout_ = nullptr;
	int stride = 0;
private:
	GLRenderManager *render_;
};

class OpenGLPipeline : public Pipeline {
public:
	OpenGLPipeline(GLRenderManager *render) : render_(render) {
	}
	~OpenGLPipeline() {
		for (auto &iter : shaders) {
			iter->Release();
		}
		if (program_) render_->DeleteProgram(program_);
	}

	bool LinkShaders();

	bool RequiresBuffer() override {
		return inputLayout && inputLayout->RequiresBuffer();
	}

	GLuint prim = 0;
	std::vector<OpenGLShaderModule *> shaders;
	AutoRef<OpenGLInputLayout> inputLayout;
	AutoRef<OpenGLDepthStencilState> depthStencil;
	AutoRef<OpenGLBlendState> blend;
	AutoRef<OpenGLRasterState> raster;

	// TODO: Optimize by getting the locations first and putting in a custom struct
	UniformBufferDesc dynamicUniforms;
	GLint samplerLocs_[MAX_TEXTURE_SLOTS]{};
	std::vector<GLint> dynamicUniformLocs_;
	GLRProgram *program_ = nullptr;

private:
	GLRenderManager *render_;
};

class OpenGLFramebuffer;
class OpenGLTexture;

class OpenGLContext : public DrawContext {
public:
	OpenGLContext();
	virtual ~OpenGLContext();

	void SetTargetSize(int w, int h) override {
		DrawContext::SetTargetSize(w, h);
		renderManager_.Resize(w, h);
	}

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		if (gl_extensions.GLES3) {
			return (uint32_t)(ShaderLanguage::GLSL_3xx | ShaderLanguage::GLSL_1xx);
		} else {
			return (uint32_t)ShaderLanguage::GLSL_1xx;
		}
	}

	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	void SetErrorCallback(ErrorCallbackFn callback, void *userdata) override {
		renderManager_.SetErrorCallback(callback, userdata);
	}

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const std::string &tag) override;

	Texture *CreateTexture(const TextureDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void BeginFrame() override;
	void EndFrame() override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, const char *tag) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	Framebuffer *GetCurrentRenderTarget() override {
		return curRenderTarget_;
	}
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindSamplerStates(int start, int count, SamplerState **states) override {
		_assert_(start + count <= MAX_TEXTURE_SLOTS);
		for (int i = 0; i < count; i++) {
			int index = i + start;
			boundSamplers_[index] = static_cast<OpenGLSamplerState *>(states[i]);
		}
	}

	void SetScissorRect(int left, int top, int width, int height) override {
		renderManager_.SetScissor({ left, top, width, height });
	}

	void SetViewports(int count, Viewport *viewports) override {
		// Same structure, different name.
		renderManager_.SetViewport((GLRViewport &)*viewports);
	}

	void SetBlendFactor(float color[4]) override {
		renderManager_.SetBlendFactor(color);
	}

	void SetStencilRef(uint8_t ref) override {
		stencilRef_ = ref;
		renderManager_.SetStencilFunc(
			curPipeline_->depthStencil->stencilEnabled,
			curPipeline_->depthStencil->stencilCompareOp,
			ref,
			curPipeline_->depthStencil->stencilCompareMask);
	}

	void BindTextures(int start, int count, Texture **textures) override;
	void BindPipeline(Pipeline *pipeline) override;
	void BindVertexBuffers(int start, int count, Buffer **buffers, const int *offsets) override {
		_assert_(start + count <= ARRAY_SIZE(curVBuffers_));
		for (int i = 0; i < count; i++) {
			curVBuffers_[i + start] = (OpenGLBuffer *)buffers[i];
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
			case VENDORSTRING: return renderManager_.GetGLString(GL_VENDOR);
			case VENDOR:
				switch (caps_.vendor) {
				case GPUVendor::VENDOR_AMD: return "VENDOR_AMD";
				case GPUVendor::VENDOR_IMGTEC: return "VENDOR_POWERVR";
				case GPUVendor::VENDOR_NVIDIA: return "VENDOR_NVIDIA";
				case GPUVendor::VENDOR_INTEL: return "VENDOR_INTEL";
				case GPUVendor::VENDOR_QUALCOMM: return "VENDOR_ADRENO";
				case GPUVendor::VENDOR_ARM: return "VENDOR_ARM";
				case GPUVendor::VENDOR_BROADCOM: return "VENDOR_BROADCOM";
				case GPUVendor::VENDOR_VIVANTE: return "VENDOR_VIVANTE";
				case GPUVendor::VENDOR_APPLE: return "VENDOR_APPLE";
				case GPUVendor::VENDOR_UNKNOWN:
				default:
					return "VENDOR_UNKNOWN";
				}
				break;
			case DRIVER: return renderManager_.GetGLString(GL_RENDERER);
			case SHADELANGVERSION: return renderManager_.GetGLString(GL_SHADING_LANGUAGE_VERSION);
			case APIVERSION: return renderManager_.GetGLString(GL_VERSION);
			default: return "?";
		}
	}

	uint64_t GetNativeObject(NativeObject obj) override {
		switch (obj) {
		case NativeObject::RENDER_MANAGER:
			return (uint64_t)(uintptr_t)&renderManager_;
		default:
			return 0;
		}
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override {}

	int GetCurrentStepId() const override {
		return renderManager_.GetCurrentStepId();
	}

	void InvalidateCachedState() override;

private:
	void ApplySamplers();

	GLRenderManager renderManager_;

	DeviceCaps caps_{};

	// Bound state
	AutoRef<OpenGLSamplerState> boundSamplers_[MAX_TEXTURE_SLOTS];
	// Point to GLRTexture directly because they can point to the textures
	// in framebuffers too (which also can be bound).
	const GLRTexture *boundTextures_[MAX_TEXTURE_SLOTS]{};

	AutoRef<OpenGLPipeline> curPipeline_;
	AutoRef<OpenGLBuffer> curVBuffers_[4]{};
	int curVBufferOffsets_[4]{};
	AutoRef<OpenGLBuffer> curIBuffer_;
	int curIBufferOffset_ = 0;
	AutoRef<Framebuffer> curRenderTarget_;

	uint8_t stencilRef_ = 0;

	// Frames in flight is not such a strict concept as with Vulkan until we start using glBufferStorage and fences.
	// But might as well have the structure ready, and can't hurt to rotate buffers.
	struct FrameData {
		GLPushBuffer *push;
	};
	FrameData frameData_[GLRenderManager::MAX_INFLIGHT_FRAMES]{};
};

static constexpr int MakeIntelSimpleVer(int v1, int v2, int v3) {
	return (v1 << 16) | (v2 << 8) | v3;
}

static bool HasIntelDualSrcBug(int versions[4]) {
	// Intel uses a confusing set of at least 3 version numbering schemes.  This is the one given to OpenGL.
	switch (MakeIntelSimpleVer(versions[0], versions[1], versions[2])) {
	case MakeIntelSimpleVer(9, 17, 10):
	case MakeIntelSimpleVer(9, 18, 10):
		return false;
	case MakeIntelSimpleVer(10, 18, 10):
		return versions[3] < 4061;
	case MakeIntelSimpleVer(10, 18, 14):
		return versions[3] < 4080;
	default:
		// Older than above didn't support dual src anyway, newer should have the fix.
		return false;
	}
}

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
	caps_.depthClampSupported = gl_extensions.ARB_depth_clamp;
	if (gl_extensions.IsGLES) {
		caps_.clipDistanceSupported = gl_extensions.EXT_clip_cull_distance || gl_extensions.APPLE_clip_distance;
		caps_.cullDistanceSupported = gl_extensions.EXT_clip_cull_distance;
	} else {
		caps_.clipDistanceSupported = gl_extensions.VersionGEThan(3, 0);
		caps_.cullDistanceSupported = gl_extensions.ARB_cull_distance;
	}

	// Interesting potential hack for emulating GL_DEPTH_CLAMP (use a separate varying, force depth in fragment shader):
	// This will induce a performance penalty on many architectures though so a blanket enable of this
	// is probably not a good idea.
	// https://stackoverflow.com/questions/5960757/how-to-emulate-gl-depth-clamp-nv

	switch (gl_extensions.gpuVendor) {
	case GPU_VENDOR_AMD: caps_.vendor = GPUVendor::VENDOR_AMD; break;
	case GPU_VENDOR_NVIDIA: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
	case GPU_VENDOR_ARM: caps_.vendor = GPUVendor::VENDOR_ARM; break;
	case GPU_VENDOR_QUALCOMM: caps_.vendor = GPUVendor::VENDOR_QUALCOMM; break;
	case GPU_VENDOR_BROADCOM: caps_.vendor = GPUVendor::VENDOR_BROADCOM; break;
	case GPU_VENDOR_INTEL: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
	case GPU_VENDOR_IMGTEC: caps_.vendor = GPUVendor::VENDOR_IMGTEC; break;
	case GPU_VENDOR_VIVANTE: caps_.vendor = GPUVendor::VENDOR_VIVANTE; break;
	case GPU_VENDOR_APPLE: caps_.vendor = GPUVendor::VENDOR_APPLE; break;
	case GPU_VENDOR_UNKNOWN:
	default:
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
		break;
	}
	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].push = renderManager_.CreatePushBuffer(i, GL_ARRAY_BUFFER, 64 * 1024);
	}

	if (!gl_extensions.VersionGEThan(3, 0, 0)) {
		// Don't use this extension on sub 3.0 OpenGL versions as it does not seem reliable.
		bugs_.Infest(Bugs::DUAL_SOURCE_BLENDING_BROKEN);
	} else if (caps_.vendor == GPUVendor::VENDOR_INTEL) {
		// Note: this is for Intel drivers with GL3+.
		// Also on Intel, see https://github.com/hrydgard/ppsspp/issues/10117
		// TODO: Remove entirely sometime reasonably far in driver years after 2015.
		const std::string ver = GetInfoString(Draw::InfoField::APIVERSION);
		int versions[4]{};
		if (sscanf(ver.c_str(), "Build %d.%d.%d.%d", &versions[0], &versions[1], &versions[2], &versions[3]) == 4) {
			if (HasIntelDualSrcBug(versions)) {
				bugs_.Infest(Bugs::DUAL_SOURCE_BLENDING_BROKEN);
			}
		}
	}

#if PPSSPP_ARCH(ARMV7)
	if (caps_.vendor == GPUVendor::VENDOR_BROADCOM) {
		bugs_.Infest(Bugs::RASPBERRY_SHADER_COMP_HANG);
	}
#endif

	// Try to detect old Tegra chips by checking for sub 3.0 GL versions. Like Vivante and Broadcom,
	// those can't handle NaN values in conditionals.
	if (caps_.vendor == GPUVendor::VENDOR_VIVANTE ||
		caps_.vendor == GPUVendor::VENDOR_BROADCOM ||
		(caps_.vendor == GPUVendor::VENDOR_NVIDIA && !gl_extensions.VersionGEThan(3, 0, 0))) {
		bugs_.Infest(Bugs::BROKEN_NAN_IN_CONDITIONAL);
	}

	// TODO: Make this check more lenient. Disabled for all right now
	// because it murders performance on Mali.
	if (caps_.vendor != GPUVendor::VENDOR_NVIDIA) {
		bugs_.Infest(Bugs::ANY_MAP_BUFFER_RANGE_SLOW);
	}

	if (caps_.vendor == GPUVendor::VENDOR_IMGTEC) {
		// See https://github.com/hrydgard/ppsspp/commit/8974cd675e538f4445955e3eac572a9347d84232
		// TODO: Should this workaround be removed for newer devices/drivers?
		bugs_.Infest(Bugs::PVR_GENMIPMAP_HEIGHT_GREATER);
	}

#if PPSSPP_PLATFORM(IOS)
	// For some reason, this bug does not appear on M1.
	if (caps_.vendor == GPUVendor::VENDOR_APPLE) {
		bugs_.Infest(Bugs::BROKEN_FLAT_IN_SHADER);
	}
#endif

	shaderLanguageDesc_.Init(GLSL_1xx);

	shaderLanguageDesc_.glslVersionNumber = gl_extensions.GLSLVersion();

	snprintf(shaderLanguageDesc_.driverInfo, sizeof(shaderLanguageDesc_.driverInfo),
		"%s - GLSL %d", gl_extensions.model, gl_extensions.GLSLVersion());
	// Detect shader language features.
	if (gl_extensions.IsGLES) {
		shaderLanguageDesc_.gles = true;
		if (gl_extensions.GLES3) {
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_3xx;
			shaderLanguageDesc_.fragColor0 = "fragColor0";
			shaderLanguageDesc_.texture = "texture";
			shaderLanguageDesc_.glslES30 = true;
			shaderLanguageDesc_.bitwiseOps = true;
			shaderLanguageDesc_.texelFetch = "texelFetch";
			shaderLanguageDesc_.varying_vs = "out";
			shaderLanguageDesc_.varying_fs = "in";
			shaderLanguageDesc_.attribute = "in";
		} else {
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_1xx;
			if (gl_extensions.EXT_gpu_shader4) {
				shaderLanguageDesc_.bitwiseOps = true;
				shaderLanguageDesc_.texelFetch = "texelFetch2D";
			}
			if (gl_extensions.EXT_blend_func_extended) {
				// Oldy moldy GLES, so use the fixed output name.
				shaderLanguageDesc_.fragColor1 = "gl_SecondaryFragColorEXT";
			}
		}
	} else {
		// I don't know why we were checking for IsCoreContext here before.
		if (gl_extensions.VersionGEThan(3, 3, 0)) {
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_3xx;
			shaderLanguageDesc_.fragColor0 = "fragColor0";
			shaderLanguageDesc_.texture = "texture";
			shaderLanguageDesc_.glslES30 = true;
			shaderLanguageDesc_.bitwiseOps = true;
			shaderLanguageDesc_.texelFetch = "texelFetch";
			shaderLanguageDesc_.varying_vs = "out";
			shaderLanguageDesc_.varying_fs = "in";
			shaderLanguageDesc_.attribute = "in";
		} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_1xx;
			shaderLanguageDesc_.fragColor0 = "fragColor0";
			shaderLanguageDesc_.texture = "texture";
			shaderLanguageDesc_.bitwiseOps = true;
			shaderLanguageDesc_.texelFetch = "texelFetch";
			shaderLanguageDesc_.varying_vs = "out";
			shaderLanguageDesc_.varying_fs = "in";
			shaderLanguageDesc_.attribute = "in";
		} else {
			// This too...
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_1xx;
			if (gl_extensions.EXT_gpu_shader4) {
				shaderLanguageDesc_.bitwiseOps = true;
				shaderLanguageDesc_.texelFetch = "texelFetch2D";
			}
		}
	}

	if (gl_extensions.IsGLES) {
		caps_.framebufferFetchSupported = (gl_extensions.EXT_shader_framebuffer_fetch || gl_extensions.ARM_shader_framebuffer_fetch);
		if (gl_extensions.EXT_shader_framebuffer_fetch) {
			shaderLanguageDesc_.framebufferFetchExtension = "#extension GL_EXT_shader_framebuffer_fetch : require";
			shaderLanguageDesc_.lastFragData = gl_extensions.GLES3 ? "fragColor0" : "gl_LastFragData[0]";
		} else if (gl_extensions.ARM_shader_framebuffer_fetch) {
			shaderLanguageDesc_.framebufferFetchExtension = "#extension GL_ARM_shader_framebuffer_fetch : require";
			shaderLanguageDesc_.lastFragData = "gl_LastFragColorARM";
		}
	}
}

OpenGLContext::~OpenGLContext() {
	DestroyPresets();
	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		renderManager_.DeletePushBuffer(frameData_[i].push);
	}
}

void OpenGLContext::BeginFrame() {
	renderManager_.BeginFrame();
	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];
	renderManager_.BeginPushBuffer(frameData.push);
}

void OpenGLContext::EndFrame() {
	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];
	renderManager_.EndPushBuffer(frameData.push);  // upload the data!
	renderManager_.Finish();

	InvalidateCachedState();
}

void OpenGLContext::InvalidateCachedState() {
	// Unbind stuff.
	for (auto &texture : boundTextures_) {
		texture = nullptr;
	}
	for (auto &sampler : boundSamplers_) {
		sampler = nullptr;
	}
	curPipeline_ = nullptr;
}

InputLayout *OpenGLContext::CreateInputLayout(const InputLayoutDesc &desc) {
	OpenGLInputLayout *fmt = new OpenGLInputLayout(&renderManager_);
	fmt->Compile(desc);
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
		ERROR_LOG(G3D,  "Bad texture type %d", (int)type);
		return GL_NONE;
	}
}

class OpenGLTexture : public Texture {
public:
	OpenGLTexture(GLRenderManager *render, const TextureDesc &desc);
	~OpenGLTexture();

	bool HasMips() const {
		return mipLevels_ > 1 || generatedMips_;
	}

	TextureType GetType() const { return type_; }
	void Bind(int stage) {
		render_->BindTexture(stage, tex_);
	}
	int NumMipmaps() const {
		return mipLevels_;
	}
	const GLRTexture *GetTex() const {
		return tex_;
	}

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback callback);

	GLRenderManager *render_;
	GLRTexture *tex_;

	DataFormat format_;
	TextureType type_;
	int mipLevels_;
	bool generatedMips_;
};

OpenGLTexture::OpenGLTexture(GLRenderManager *render, const TextureDesc &desc) : render_(render) {
	generatedMips_ = false;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	format_ = desc.format;
	type_ = desc.type;
	GLenum target = TypeToTarget(desc.type);
	tex_ = render->CreateTexture(target, desc.width, desc.height, desc.mipLevels);

	mipLevels_ = desc.mipLevels;
	if (desc.initData.empty())
		return;

	int level = 0;
	for (auto data : desc.initData) {
		SetImageData(0, 0, 0, width_, height_, depth_, level, 0, data, desc.initDataCallback);
		width_ = (width_ + 1) / 2;
		height_ = (height_ + 1) / 2;
		depth_ = (depth_ + 1) / 2;
		level++;
	}
	mipLevels_ = desc.generateMips ? desc.mipLevels : level;

	bool genMips = false;
	if ((int)desc.initData.size() < desc.mipLevels && desc.generateMips) {
		// Assumes the texture is bound for editing
		genMips = true;
		generatedMips_ = true;
	}
	render->FinalizeTexture(tex_, mipLevels_, genMips);
}

OpenGLTexture::~OpenGLTexture() {
	if (tex_) {
		render_->DeleteTexture(tex_);
		tex_ = 0;
		generatedMips_ = false;
	}
}

class OpenGLFramebuffer : public Framebuffer {
public:
	OpenGLFramebuffer(GLRenderManager *render, GLRFramebuffer *framebuffer) : render_(render), framebuffer_(framebuffer) {
		width_ = framebuffer->width;
		height_ = framebuffer->height;
	}
	~OpenGLFramebuffer() {
		render_->DeleteFramebuffer(framebuffer_);
	}

	GLRenderManager *render_;
	GLRFramebuffer *framebuffer_ = nullptr;
};

void OpenGLTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback callback) {
	if ((width != width_ || height != height_ || depth != depth_) && level == 0) {
		// When switching to texStorage we need to handle this correctly.
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	if (stride == 0)
		stride = width;

	size_t alignment = DataFormatSizeInBytes(format_);
	// Make a copy of data with stride eliminated.
	uint8_t *texData = new uint8_t[(size_t)(width * height * depth * alignment)];

	bool texDataPopulated = false;
	if (callback) {
		texDataPopulated = callback(texData, data, width, height, depth, width * (int)alignment, height * width * (int)alignment);
	}
	if (texDataPopulated) {
		if (format_ == DataFormat::A1R5G5B5_UNORM_PACK16) {
			format_ = DataFormat::R5G5B5A1_UNORM_PACK16;
			ConvertBGRA5551ToABGR1555((u16 *)texData, (const u16 *)texData, width * height * depth);
		}
	} else {
		// Emulate support for DataFormat::A1R5G5B5_UNORM_PACK16.
		if (format_ == DataFormat::A1R5G5B5_UNORM_PACK16) {
			format_ = DataFormat::R5G5B5A1_UNORM_PACK16;
			for (int y = 0; y < height; y++) {
				ConvertBGRA5551ToABGR1555((u16 *)(texData + y * width * alignment), (const u16 *)(data + y * stride * alignment), width);
			}
		} else {
			for (int y = 0; y < height; y++) {
				memcpy(texData + y * width * alignment, data + y * stride * alignment, width * alignment);
			}
		}
	}

	render_->TextureImage(tex_, level, width, height, format_, texData);
}

#ifdef DEBUG_READ_PIXELS
// TODO: Make more generic.
static void LogReadPixelsError(GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		break;
	case GL_INVALID_ENUM:
		ERROR_LOG(G3D, "glReadPixels: GL_INVALID_ENUM");
		break;
	case GL_INVALID_VALUE:
		ERROR_LOG(G3D, "glReadPixels: GL_INVALID_VALUE");
		break;
	case GL_INVALID_OPERATION:
		ERROR_LOG(G3D, "glReadPixels: GL_INVALID_OPERATION");
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		ERROR_LOG(G3D, "glReadPixels: GL_INVALID_FRAMEBUFFER_OPERATION");
		break;
	case GL_OUT_OF_MEMORY:
		ERROR_LOG(G3D, "glReadPixels: GL_OUT_OF_MEMORY");
		break;
#ifndef USING_GLES2
	case GL_STACK_UNDERFLOW:
		ERROR_LOG(G3D, "glReadPixels: GL_STACK_UNDERFLOW");
		break;
	case GL_STACK_OVERFLOW:
		ERROR_LOG(G3D, "glReadPixels: GL_STACK_OVERFLOW");
		break;
#endif
	default:
		ERROR_LOG(G3D, "glReadPixels: %08x", error);
		break;
	}
}
#endif

bool OpenGLContext::CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat dataFormat, void *pixels, int pixelStride, const char *tag) {
	if (gl_extensions.IsGLES && (channelBits & FB_COLOR_BIT) == 0) {
		// Can't readback depth or stencil on GLES.
		return false;
	}
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)src;
	GLuint aspect = 0;
	if (channelBits & FB_COLOR_BIT)
		aspect |= GL_COLOR_BUFFER_BIT;
	if (channelBits & FB_DEPTH_BIT)
		aspect |= GL_DEPTH_BUFFER_BIT;
	if (channelBits & FB_STENCIL_BIT)
		aspect |= GL_STENCIL_BUFFER_BIT;
	renderManager_.CopyFramebufferToMemorySync(fb ? fb->framebuffer_ : nullptr, aspect, x, y, w, h, dataFormat, (uint8_t *)pixels, pixelStride, tag);
	return true;
}


Texture *OpenGLContext::CreateTexture(const TextureDesc &desc) {
	return new OpenGLTexture(&renderManager_, desc);
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
	OpenGLBuffer(GLRenderManager *render, size_t size, uint32_t flags) : render_(render) {
		target_ = (flags & BufferUsageFlag::INDEXDATA) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
		usage_ = 0;
		if (flags & BufferUsageFlag::DYNAMIC)
			usage_ = GL_STREAM_DRAW;
		else
			usage_ = GL_STATIC_DRAW;
		buffer_ = render->CreateBuffer(target_, size, usage_);
		totalSize_ = size;
	}
	~OpenGLBuffer() override {
		render_->DeleteBuffer(buffer_);
	}

	GLRenderManager *render_;
	GLRBuffer *buffer_;
	GLuint target_;
	GLuint usage_;

	size_t totalSize_;
};

Buffer *OpenGLContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new OpenGLBuffer(&renderManager_, size, usageFlags);
}

void OpenGLContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	OpenGLBuffer *buf = (OpenGLBuffer *)buffer;

	if (size + offset > buf->totalSize_) {
		Crash();
	}

	uint8_t *dataCopy = new uint8_t[size];
	memcpy(dataCopy, data, size);
	// if (flags & UPDATE_DISCARD) we could try to orphan the buffer using glBufferData.
	// But we're much better off using separate buffers per FrameData...
	renderManager_.BufferSubdata(buf->buffer_, offset, size, dataCopy);
}

Pipeline *OpenGLContext::CreateGraphicsPipeline(const PipelineDesc &desc) {
	if (!desc.shaders.size()) {
		ERROR_LOG(G3D,  "Pipeline requires at least one shader");
		return nullptr;
	}
	if ((uint32_t)desc.prim >= (uint32_t)Primitive::PRIMITIVE_TYPE_COUNT) {
		ERROR_LOG(G3D, "Invalid primitive type");
		return nullptr;
	}
	if (!desc.depthStencil || !desc.blend || !desc.raster) {
		ERROR_LOG(G3D,  "Incomplete prim desciption");
		return nullptr;
	}

	OpenGLPipeline *pipeline = new OpenGLPipeline(&renderManager_);
	for (auto iter : desc.shaders) {
		if (iter) {
			iter->AddRef();
			pipeline->shaders.push_back(static_cast<OpenGLShaderModule *>(iter));
		} else {
			ERROR_LOG(G3D,  "ERROR: Tried to create graphics pipeline with a null shader module");
			delete pipeline;
			return nullptr;
		}
	}
	if (desc.uniformDesc) {
		pipeline->dynamicUniforms = *desc.uniformDesc;
		pipeline->dynamicUniformLocs_.resize(desc.uniformDesc->uniforms.size());
	}
	if (pipeline->LinkShaders()) {
		// Build the rest of the virtual pipeline object.
		pipeline->prim = primToGL[(int)desc.prim];
		pipeline->depthStencil = (OpenGLDepthStencilState *)desc.depthStencil;
		pipeline->blend = (OpenGLBlendState *)desc.blend;
		pipeline->raster = (OpenGLRasterState *)desc.raster;
		pipeline->inputLayout = (OpenGLInputLayout *)desc.inputLayout;
		return pipeline;
	} else {
		ERROR_LOG(G3D,  "Failed to create pipeline - shaders failed to link");
		delete pipeline;
		return nullptr;
	}
}

void OpenGLContext::BindTextures(int start, int count, Texture **textures) {
	_assert_(start + count <= MAX_TEXTURE_SLOTS);
	for (int i = start; i < start + count; i++) {
		OpenGLTexture *glTex = static_cast<OpenGLTexture *>(textures[i - start]);
		if (!glTex) {
			boundTextures_[i] = nullptr;
			renderManager_.BindTexture(i, nullptr);
			continue;
		}
		glTex->Bind(i);
		boundTextures_[i] = glTex->GetTex();
	}
}

void OpenGLContext::ApplySamplers() {
	for (int i = 0; i < MAX_TEXTURE_SLOTS; i++) {
		const OpenGLSamplerState *samp = boundSamplers_[i];
		const GLRTexture *tex = boundTextures_[i];
		if (tex) {
			_assert_(samp);
		} else {
			continue;
		}
		GLenum wrapS;
		GLenum wrapT;
		if (tex->canWrap) {
			wrapS = samp->wrapU;
			wrapT = samp->wrapV;
		} else {
			wrapS = GL_CLAMP_TO_EDGE;
			wrapT = GL_CLAMP_TO_EDGE;
		}
		GLenum magFilt = samp->magFilt;
		GLenum minFilt = tex->numMips > 1 ? samp->mipMinFilt : samp->minFilt;
		renderManager_.SetTextureSampler(i, wrapS, wrapT, magFilt, minFilt, 0.0f);
		renderManager_.SetTextureLod(i, 0.0, (float)(tex->numMips - 1), 0.0);
	}
}

ShaderModule *OpenGLContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const std::string &tag) {
	OpenGLShaderModule *shader = new OpenGLShaderModule(&renderManager_, stage, tag);
	if (shader->Compile(&renderManager_, language, data, dataSize)) {
		return shader;
	} else {
		shader->Release();
		return nullptr;
	}
}

bool OpenGLPipeline::LinkShaders() {
	std::vector<GLRShader *> linkShaders;
	for (auto shaderModule : shaders) {
		if (shaderModule) {
			GLRShader *shader = shaderModule->GetShader();
			if (shader) {
				linkShaders.push_back(shader);
			} else {
				ERROR_LOG(G3D,  "LinkShaders: Bad shader module");
				return false;
			}
		} else {
			ERROR_LOG(G3D,  "LinkShaders: Bad shader in module");
			return false;
		}
	}

	std::vector<GLRProgram::Semantic> semantics;
	// Bind all the common vertex data points. Mismatching ones will be ignored.
	semantics.push_back({ SEM_POSITION, "Position" });
	semantics.push_back({ SEM_COLOR0, "Color0" });
	semantics.push_back({ SEM_TEXCOORD0, "TexCoord0" });
	semantics.push_back({ SEM_NORMAL, "Normal" });
	semantics.push_back({ SEM_TANGENT, "Tangent" });
	semantics.push_back({ SEM_BINORMAL, "Binormal" });
	// For postshaders.
	semantics.push_back({ SEM_POSITION, "a_position" });
	semantics.push_back({ SEM_TEXCOORD0, "a_texcoord0" });
	std::vector<GLRProgram::UniformLocQuery> queries;
	queries.push_back({ &samplerLocs_[0], "sampler0" });
	queries.push_back({ &samplerLocs_[1], "sampler1" });
	queries.push_back({ &samplerLocs_[2], "sampler2" });
	_assert_(queries.size() >= MAX_TEXTURE_SLOTS);
	for (size_t i = 0; i < dynamicUniforms.uniforms.size(); ++i) {
		queries.push_back({ &dynamicUniformLocs_[i], dynamicUniforms.uniforms[i].name });
	}
	std::vector<GLRProgram::Initializer> initialize;
	for (int i = 0; i < MAX_TEXTURE_SLOTS; ++i)
		initialize.push_back({ &samplerLocs_[i], 0, i });
	program_ = render_->CreateProgram(linkShaders, semantics, queries, initialize, false, false);
	return true;
}

void OpenGLContext::BindPipeline(Pipeline *pipeline) {
	curPipeline_ = (OpenGLPipeline *)pipeline;
	if (!curPipeline_) {
		return;
	}
	curPipeline_->blend->Apply(&renderManager_);
	curPipeline_->depthStencil->Apply(&renderManager_, stencilRef_);
	curPipeline_->raster->Apply(&renderManager_);
	renderManager_.BindProgram(curPipeline_->program_);
}

void OpenGLContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	if (curPipeline_->dynamicUniforms.uniformBufferSize != size) {
		Crash();
	}

	for (size_t i = 0; i < curPipeline_->dynamicUniforms.uniforms.size(); ++i) {
		const auto &uniform = curPipeline_->dynamicUniforms.uniforms[i];
		const GLint &loc = curPipeline_->dynamicUniformLocs_[i];
		const float *data = (const float *)((uint8_t *)ub + uniform.offset);
		switch (uniform.type) {
		case UniformType::FLOAT1:
		case UniformType::FLOAT2:
		case UniformType::FLOAT3:
		case UniformType::FLOAT4:
			renderManager_.SetUniformF(&loc, 1 + (int)uniform.type - (int)UniformType::FLOAT1, data);
			break;
		case UniformType::MATRIX4X4:
			renderManager_.SetUniformM4x4(&loc, data);
			break;
		}
	}
}

void OpenGLContext::Draw(int vertexCount, int offset) {
	_dbg_assert_msg_(curVBuffers_[0] != nullptr, "Can't call Draw without a vertex buffer");
	ApplySamplers();
	if (curPipeline_->inputLayout) {
		renderManager_.BindVertexBuffer(curPipeline_->inputLayout->inputLayout_, curVBuffers_[0]->buffer_, curVBufferOffsets_[0]);
	}
	renderManager_.Draw(curPipeline_->prim, offset, vertexCount);
}

void OpenGLContext::DrawIndexed(int vertexCount, int offset) {
	_dbg_assert_msg_(curVBuffers_[0] != nullptr, "Can't call DrawIndexed without a vertex buffer");
	_dbg_assert_msg_(curIBuffer_ != nullptr, "Can't call DrawIndexed without an index buffer");
	ApplySamplers();
	if (curPipeline_->inputLayout) {
		renderManager_.BindVertexBuffer(curPipeline_->inputLayout->inputLayout_, curVBuffers_[0]->buffer_, curVBufferOffsets_[0]);
	}
	renderManager_.BindIndexBuffer(curIBuffer_->buffer_);
	renderManager_.DrawIndexed(curPipeline_->prim, vertexCount, GL_UNSIGNED_SHORT, (void *)((intptr_t)curIBufferOffset_ + offset * sizeof(uint32_t)));
}

void OpenGLContext::DrawUP(const void *vdata, int vertexCount) {
	_assert_(curPipeline_->inputLayout != nullptr);
	int stride = curPipeline_->inputLayout->stride;
	size_t dataSize = stride * vertexCount;

	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];

	GLRBuffer *buf;
	size_t offset = frameData.push->Push(vdata, dataSize, &buf);

	ApplySamplers();
	if (curPipeline_->inputLayout) {
		renderManager_.BindVertexBuffer(curPipeline_->inputLayout->inputLayout_, buf, offset);
	}
	renderManager_.Draw(curPipeline_->prim, 0, vertexCount);
}

void OpenGLContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	float col[4];
	Uint8x4ToFloat4(col, colorval);
	GLuint glMask = 0;
	if (mask & FBChannel::FB_COLOR_BIT) {
		glMask |= GL_COLOR_BUFFER_BIT;
	}
	if (mask & FBChannel::FB_DEPTH_BIT) {
		glMask |= GL_DEPTH_BUFFER_BIT;
	}
	if (mask & FBChannel::FB_STENCIL_BIT) {
		glMask |= GL_STENCIL_BUFFER_BIT;
	}
	renderManager_.Clear(colorval, depthVal, stencilVal, glMask, 0xF, 0, 0, targetWidth_, targetHeight_);
}

DrawContext *T3DCreateGLContext() {
	return new OpenGLContext();
}

OpenGLInputLayout::~OpenGLInputLayout() {
	render_->DeleteInputLayout(inputLayout_);
}

void OpenGLInputLayout::Compile(const InputLayoutDesc &desc) {
	// TODO: This is only accurate if there's only one stream. But whatever, for now we
	// never use multiple streams anyway.
	stride = desc.bindings.empty() ? 0 : (GLsizei)desc.bindings[0].stride;

	std::vector<GLRInputLayout::Entry> entries;
	for (auto &attr : desc.attributes) {
		GLRInputLayout::Entry entry;
		entry.location = attr.location;
		entry.stride = (GLsizei)desc.bindings[attr.binding].stride;
		entry.offset = attr.offset;
		switch (attr.format) {
		case DataFormat::R32G32_FLOAT:
			entry.count = 2;
			entry.type = GL_FLOAT;
			entry.normalized = GL_FALSE;
			break;
		case DataFormat::R32G32B32_FLOAT:
			entry.count = 3;
			entry.type = GL_FLOAT;
			entry.normalized = GL_FALSE;
			break;
		case DataFormat::R32G32B32A32_FLOAT:
			entry.count = 4;
			entry.type = GL_FLOAT;
			entry.normalized = GL_FALSE;
			break;
		case DataFormat::R8G8B8A8_UNORM:
			entry.count = 4;
			entry.type = GL_UNSIGNED_BYTE;
			entry.normalized = GL_TRUE;
			break;
		case DataFormat::UNDEFINED:
		default:
			ERROR_LOG(G3D,  "Thin3DGLVertexFormat: Invalid or unknown component type applied.");
			break;
		}

		entries.push_back(entry);
	}
	if (!entries.empty()) {
		inputLayout_ = render_->CreateInputLayout(entries);
	} else {
		inputLayout_ = nullptr;
	}
}

Framebuffer *OpenGLContext::CreateFramebuffer(const FramebufferDesc &desc) {
	CheckGLExtensions();

	GLRFramebuffer *framebuffer = renderManager_.CreateFramebuffer(desc.width, desc.height, desc.z_stencil);
	OpenGLFramebuffer *fbo = new OpenGLFramebuffer(&renderManager_, framebuffer);
	return fbo;
}

void OpenGLContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	GLRRenderPassAction color = (GLRRenderPassAction)rp.color;
	GLRRenderPassAction depth = (GLRRenderPassAction)rp.depth;
	GLRRenderPassAction stencil = (GLRRenderPassAction)rp.stencil;

	renderManager_.BindFramebufferAsRenderTarget(fb ? fb->framebuffer_ : nullptr, color, depth, stencil, rp.clearColor, rp.clearDepth, rp.clearStencil, tag);
	curRenderTarget_ = fb;
}

void OpenGLContext::CopyFramebufferImage(Framebuffer *fbsrc, int srcLevel, int srcX, int srcY, int srcZ, Framebuffer *fbdst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits, const char *tag) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;

	int aspect = 0;
	if (channelBits & FB_COLOR_BIT) {
		aspect |= GL_COLOR_BUFFER_BIT;
	} else if (channelBits & (FB_STENCIL_BIT | FB_DEPTH_BIT)) {
		if (channelBits & FB_DEPTH_BIT)
			aspect |= GL_DEPTH_BUFFER_BIT;
		if (channelBits & FB_STENCIL_BIT)
			aspect |= GL_STENCIL_BUFFER_BIT;
	}
	renderManager_.CopyFramebuffer(src->framebuffer_, GLRect2D{ srcX, srcY, width, height }, dst->framebuffer_, GLOffset2D{ dstX, dstY }, aspect, tag);
}

bool OpenGLContext::BlitFramebuffer(Framebuffer *fbsrc, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *fbdst, int dstX1, int dstY1, int dstX2, int dstY2, int channels, FBBlitFilter linearFilter, const char *tag) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;
	GLuint aspect = 0;
	if (channels & FB_COLOR_BIT)
		aspect |= GL_COLOR_BUFFER_BIT;
	if (channels & FB_DEPTH_BIT)
		aspect |= GL_DEPTH_BUFFER_BIT;
	if (channels & FB_STENCIL_BIT)
		aspect |= GL_STENCIL_BUFFER_BIT;

	renderManager_.BlitFramebuffer(src->framebuffer_, GLRect2D{ srcX1, srcY1, srcX2 - srcX1, srcY2 - srcY1 }, dst->framebuffer_, GLRect2D{ dstX1, dstY1, dstX2 - dstX1, dstY2 - dstY1 }, aspect, linearFilter == FB_BLIT_LINEAR, tag);
	return true;
}

void OpenGLContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int color) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	_assert_(binding < MAX_TEXTURE_SLOTS);

	GLuint aspect = 0;
	if (channelBit & FB_COLOR_BIT) {
		aspect |= GL_COLOR_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->color_texture;
	}
	if (channelBit & FB_DEPTH_BIT) {
		aspect |= GL_DEPTH_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->z_stencil_texture;
	}
	if (channelBit & FB_STENCIL_BIT) {
		aspect |= GL_STENCIL_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->z_stencil_texture;
	}
	renderManager_.BindFramebufferAsTexture(fb->framebuffer_, binding, aspect, color);
}

void OpenGLContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	if (fb) {
		*w = fb->Width();
		*h = fb->Height();
	} else {
		*w = targetWidth_;
		*h = targetHeight_;
	}
}

uint32_t OpenGLContext::GetDataFormatSupport(DataFormat fmt) const {
	switch (fmt) {
	case DataFormat::R4G4B4A4_UNORM_PACK16:
	case DataFormat::R5G6B5_UNORM_PACK16:
	case DataFormat::R5G5B5A1_UNORM_PACK16:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_AUTOGEN_MIPS;  // native support

	case DataFormat::R8G8B8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_INPUTLAYOUT | FMT_AUTOGEN_MIPS;

	case DataFormat::A1R5G5B5_UNORM_PACK16:
		return FMT_TEXTURE;  // we will emulate this! Very fast to convert from R5G5B5A1_UNORM_PACK16 during upload.

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
