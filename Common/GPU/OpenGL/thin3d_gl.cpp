#include <cstdio>
#include <vector>
#include <string>
#include <map>

#include "ppsspp_config.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/System/System.h"
#include "Common/Log.h"
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

	void Apply(GLRenderManager *render, uint8_t stencilRef, uint8_t stencilWriteMask, uint8_t stencilCompareMask) {
		render->SetDepth(depthTestEnabled, depthWriteEnabled, depthComp);
		render->SetStencil(
			stencilEnabled, stencilCompareOp, stencilRef, stencilCompareMask,
			stencilWriteMask, stencilFail, stencilZFail, stencilPass);
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
	source_ = std::string((const char *)data, dataSize);
	// Add the prelude on automatically.
	if (glstage_ == GL_FRAGMENT_SHADER || glstage_ == GL_VERTEX_SHADER) {
		if (source_.find("#version") == std::string::npos) {
			source_ = ApplyGLSLPrelude(source_, glstage_);
		}
	} else {
		// Unsupported shader type
		return false;
	}

	shader_ = render->CreateShader(glstage_, source_, tag_);
	_assert_(shader_ != nullptr);  // normally can't fail since we defer creation, unless there's a memory error or similar.
	return true;
}

struct PipelineLocData : GLRProgramLocData {
	GLint samplerLocs_[MAX_TEXTURE_SLOTS]{};
	std::vector<GLint> dynamicUniformLocs_;
};

class OpenGLInputLayout : public InputLayout {
public:
	OpenGLInputLayout(GLRenderManager *render) : render_(render) {}
	~OpenGLInputLayout();

	void Compile(const InputLayoutDesc &desc);

	GLRInputLayout *inputLayout_ = nullptr;
	int stride = 0;
private:
	GLRenderManager *render_;
};

class OpenGLPipeline : public Pipeline {
public:
	OpenGLPipeline(GLRenderManager *render) : render_(render) {}
	~OpenGLPipeline() {
		for (auto &iter : shaders) {
			iter->Release();
		}
		if (program_) {
			render_->DeleteProgram(program_);
		}
		// DO NOT delete locs_ here, it's deleted by the render manager.
	}

	bool LinkShaders(const PipelineDesc &desc);

	GLuint prim = 0;
	std::vector<OpenGLShaderModule *> shaders;
	AutoRef<OpenGLInputLayout> inputLayout;
	AutoRef<OpenGLDepthStencilState> depthStencil;
	AutoRef<OpenGLBlendState> blend;
	AutoRef<OpenGLRasterState> raster;

	// Not owned!
	PipelineLocData *locs_ = nullptr;

	// TODO: Optimize by getting the locations first and putting in a custom struct
	UniformBufferDesc dynamicUniforms;
	GLRProgram *program_ = nullptr;


	// Allow using other sampler names than sampler0, sampler1 etc in shaders.
	// If not set, will default to those, though.
	Slice<SamplerDef> samplers_;

private:
	GLRenderManager *render_;
};

class OpenGLFramebuffer;
class OpenGLTexture;

class OpenGLContext : public DrawContext {
public:
	OpenGLContext(bool canChangeSwapInterval);
	~OpenGLContext();

	BackendState GetCurrentBackendState() const override {
		return BackendState{
			(u32)renderManager_.GetNumSteps(),
			true,  // Means that the other value is meaningful.
		};
	}

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
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) override;

	Texture *CreateTexture(const TextureDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void BeginFrame(DebugFlags debugFlags) override;
	void EndFrame() override;
	void Present(PresentMode mode, int vblanks) override;

	int GetFrameCount() override {
		return frameCount_;
	}

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;
	void UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemory(Framebuffer *src, Aspect channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect channelBit, int layer) override;

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

	void SetViewport(const Viewport &viewport) override {
		// Same structure, different name.
		renderManager_.SetViewport((GLRViewport &)viewport);
	}

	void SetBlendFactor(float color[4]) override {
		renderManager_.SetBlendFactor(color);
	}

	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override {
		stencilRef_ = refValue;
		stencilWriteMask_ = writeMask;
		stencilCompareMask_ = compareMask;
		// Do we need to update on the fly here?
		renderManager_.SetStencil(
			curPipeline_->depthStencil->stencilEnabled,
			curPipeline_->depthStencil->stencilCompareOp,
			refValue,
			compareMask,
			writeMask,
			curPipeline_->depthStencil->stencilFail,
			curPipeline_->depthStencil->stencilZFail,
			curPipeline_->depthStencil->stencilPass);
	}

	void BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) override;
	void BindNativeTexture(int sampler, void *nativeTexture) override;

	void BindPipeline(Pipeline *pipeline) override;
	void BindVertexBuffer(Buffer *buffer, int offset) override {
		curVBuffer_ = (OpenGLBuffer *)buffer;
		curVBufferOffset_ = offset;
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
	void DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) override;
	void DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) override;

	void Clear(Aspect mask, uint32_t colorval, float depthVal, int stencilVal) override;

	std::string GetInfoString(InfoField info) const override {
		// TODO: Make these actually query the right information
		switch (info) {
		case InfoField::APINAME:
			if (gl_extensions.IsGLES) {
				return "OpenGL ES";
			} else {
				return "OpenGL";
			}
		case InfoField::VENDORSTRING:
			return renderManager_.GetGLString(GL_VENDOR);
		case InfoField::VENDOR:
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
			case GPUVendor::VENDOR_MESA: return "VENDOR_MESA";
			case GPUVendor::VENDOR_UNKNOWN:
			default:
				return "VENDOR_UNKNOWN";
			}
			break;
		case InfoField::DRIVER: return renderManager_.GetGLString(GL_RENDERER);
		case InfoField::SHADELANGVERSION: return renderManager_.GetGLString(GL_SHADING_LANGUAGE_VERSION);
		case InfoField::APIVERSION: return renderManager_.GetGLString(GL_VERSION);
		default: return "?";
		}
	}

	uint64_t GetNativeObject(NativeObject obj, void *srcObject) override;

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override {}

	void Invalidate(InvalidationFlags flags) override;

	void SetInvalidationCallback(InvalidationCallback callback) override {
		renderManager_.SetInvalidationCallback(callback);
	}

	std::string GetGpuProfileString() const override {
		return renderManager_.GetGpuProfileString();
	}

private:
	void ApplySamplers();

	GLRenderManager renderManager_;
	int frameCount_ = 0;

	DeviceCaps caps_{};

	// Bound state
	AutoRef<OpenGLSamplerState> boundSamplers_[MAX_TEXTURE_SLOTS];
	// Point to GLRTexture directly because they can point to the textures
	// in framebuffers too (which also can be bound).
	const GLRTexture *boundTextures_[MAX_TEXTURE_SLOTS]{};

	AutoRef<OpenGLPipeline> curPipeline_;
	AutoRef<OpenGLBuffer> curVBuffer_;
	AutoRef<OpenGLBuffer> curIBuffer_;
	int curVBufferOffset_ = 0;
	int curIBufferOffset_ = 0;
	AutoRef<Framebuffer> curRenderTarget_;

	uint8_t stencilRef_ = 0;
	uint8_t stencilWriteMask_ = 0;
	uint8_t stencilCompareMask_ = 0;

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

static bool HasIntelDualSrcBug(const int versions[4]) {
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

OpenGLContext::OpenGLContext(bool canChangeSwapInterval) : renderManager_(frameTimeHistory_) {
	if (gl_extensions.IsGLES) {
		if (gl_extensions.OES_packed_depth_stencil || gl_extensions.OES_depth24) {
			caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
		} else {
			caps_.preferredDepthBufferFormat = DataFormat::D16;
		}
		if (gl_extensions.GLES3) {
			// Mali reports 30 but works fine...
			if (gl_extensions.range[1][5][1] >= 30) {
				caps_.fragmentShaderInt32Supported = true;
			}
		}
		caps_.texture3DSupported = gl_extensions.OES_texture_3D;
		caps_.textureDepthSupported = gl_extensions.GLES3 || gl_extensions.OES_depth_texture;
	} else {
		if (gl_extensions.VersionGEThan(3, 3, 0)) {
			caps_.fragmentShaderInt32Supported = true;
		}
		caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
		caps_.texture3DSupported = true;
		caps_.textureDepthSupported = true;
	}

	caps_.coordConvention = CoordConvention::OpenGL;
	caps_.setMaxFrameLatencySupported = true;
	caps_.dualSourceBlend = gl_extensions.ARB_blend_func_extended || gl_extensions.EXT_blend_func_extended;
	caps_.anisoSupported = gl_extensions.EXT_texture_filter_anisotropic;
	caps_.framebufferCopySupported = gl_extensions.OES_copy_image || gl_extensions.NV_copy_image || gl_extensions.EXT_copy_image || gl_extensions.ARB_copy_image;
	caps_.framebufferBlitSupported = gl_extensions.NV_framebuffer_blit || gl_extensions.ARB_framebuffer_object || gl_extensions.GLES3;
	caps_.framebufferDepthBlitSupported = caps_.framebufferBlitSupported;
	caps_.framebufferStencilBlitSupported = caps_.framebufferBlitSupported;
	caps_.depthClampSupported = gl_extensions.ARB_depth_clamp || gl_extensions.EXT_depth_clamp;
	caps_.blendMinMaxSupported = gl_extensions.EXT_blend_minmax;
	caps_.multiSampleLevelsMask = 1;  // More could be supported with some work.

	if (gl_extensions.IsGLES) {
		caps_.clipDistanceSupported = gl_extensions.EXT_clip_cull_distance || gl_extensions.APPLE_clip_distance;
		caps_.cullDistanceSupported = gl_extensions.EXT_clip_cull_distance;
	} else {
		caps_.clipDistanceSupported = gl_extensions.VersionGEThan(3, 0);
		caps_.cullDistanceSupported = gl_extensions.ARB_cull_distance;
	}
	caps_.textureNPOTFullySupported =
		(!gl_extensions.IsGLES && gl_extensions.VersionGEThan(2, 0, 0)) ||
		gl_extensions.IsCoreContext || gl_extensions.GLES3 ||
		gl_extensions.ARB_texture_non_power_of_two || gl_extensions.OES_texture_npot;

	if (gl_extensions.IsGLES) {
		caps_.fragmentShaderDepthWriteSupported = gl_extensions.GLES3;
		// There's also GL_EXT_frag_depth but it's rare along with 2.0. Most chips that support it are simply 3.0 chips.
	} else {
		caps_.fragmentShaderDepthWriteSupported = true;
	}
	caps_.fragmentShaderStencilWriteSupported = gl_extensions.ARB_shader_stencil_export;

	// GLES has no support for logic framebuffer operations. There doesn't even seem to exist any such extensions.
	caps_.logicOpSupported = !gl_extensions.IsGLES;

	// Always the case in GL (which is what we want for PSP flat shade).
	caps_.provokingVertexLast = true;

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
	case GPU_VENDOR_MESA: caps_.vendor = GPUVendor::VENDOR_MESA; break;
	case GPU_VENDOR_UNKNOWN:
	default:
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
		break;
	}

	// Very rough heuristic!
	caps_.isTilingGPU = gl_extensions.IsGLES && caps_.vendor != GPUVendor::VENDOR_NVIDIA && caps_.vendor != GPUVendor::VENDOR_INTEL;

	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].push = renderManager_.CreatePushBuffer(i, GL_ARRAY_BUFFER, 64 * 1024, "thin3d_vbuf");
	}

	if (!gl_extensions.VersionGEThan(3, 0, 0)) {
		// Don't use this extension on sub 3.0 OpenGL versions as it does not seem reliable.
		bugs_.Infest(Bugs::DUAL_SOURCE_BLENDING_BROKEN);
	} else if (caps_.vendor == GPUVendor::VENDOR_INTEL) {
		// Note: this is for Intel drivers with GL3+.
		// Also on Intel, see https://github.com/hrydgard/ppsspp/issues/10117
		// TODO: Remove entirely sometime reasonably far in driver years after 2015.
		const std::string ver = OpenGLContext::GetInfoString(Draw::InfoField::APIVERSION);
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

	if (caps_.vendor == GPUVendor::VENDOR_QUALCOMM) {
#if PPSSPP_PLATFORM(ANDROID)
		// The bug seems to affect Adreno 3xx and 5xx, and appeared in Android 8.0 Oreo, so API 26.
		// See https://github.com/hrydgard/ppsspp/issues/16015#issuecomment-1328316080.
		if (gl_extensions.modelNumber < 600 && System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 26)
			bugs_.Infest(Bugs::ADRENO_RESOURCE_DEADLOCK);
#endif
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
			shaderLanguageDesc_.texture3D = "texture";
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
			shaderLanguageDesc_.texture3D = "texture";
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
			shaderLanguageDesc_.texture3D = "texture";
			shaderLanguageDesc_.bitwiseOps = true;
			shaderLanguageDesc_.texelFetch = "texelFetch";
			shaderLanguageDesc_.varying_vs = "out";
			shaderLanguageDesc_.varying_fs = "in";
			shaderLanguageDesc_.attribute = "in";
		} else {
			// This too...
			shaderLanguageDesc_.shaderLanguage = ShaderLanguage::GLSL_1xx;
			if (gl_extensions.EXT_gpu_shader4) {
				// Older macOS devices seem to have problems defining uint uniforms.
				// Let's just assume OpenGL 3.0+ is required.
				shaderLanguageDesc_.bitwiseOps = gl_extensions.VersionGEThan(3, 0, 0);
				shaderLanguageDesc_.texelFetch = "texelFetch2D";
			}
		}
	}

	// NOTE: We only support framebuffer fetch on ES3 due to past issues..
	if (gl_extensions.IsGLES && gl_extensions.GLES3) {
		caps_.framebufferFetchSupported = (gl_extensions.EXT_shader_framebuffer_fetch || gl_extensions.ARM_shader_framebuffer_fetch);

		if (gl_extensions.EXT_shader_framebuffer_fetch) {
			shaderLanguageDesc_.framebufferFetchExtension = "#extension GL_EXT_shader_framebuffer_fetch : require";
			shaderLanguageDesc_.lastFragData = "fragColor0";
		} else if (gl_extensions.ARM_shader_framebuffer_fetch) {
			shaderLanguageDesc_.framebufferFetchExtension = "#extension GL_ARM_shader_framebuffer_fetch : require";
			shaderLanguageDesc_.lastFragData = "gl_LastFragColorARM";
		}
	}

	if (canChangeSwapInterval) {
		caps_.presentInstantModeChange = true;
		caps_.presentMaxInterval = 4;
		caps_.presentModesSupported = PresentMode::FIFO | PresentMode::IMMEDIATE;
	} else {
		caps_.presentInstantModeChange = false;
		caps_.presentModesSupported = PresentMode::FIFO;
		caps_.presentMaxInterval = 1;
	}

	renderManager_.SetDeviceCaps(caps_);
}

OpenGLContext::~OpenGLContext() {
	DestroyPresets();

	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		renderManager_.DeletePushBuffer(frameData_[i].push);
	}
}

void OpenGLContext::BeginFrame(DebugFlags debugFlags) {
	renderManager_.BeginFrame(debugFlags & DebugFlags::PROFILE_TIMESTAMPS);
	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];
	frameData.push->Begin();
}

void OpenGLContext::EndFrame() {
	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];
	frameData.push->End();  // upload the data!
	renderManager_.Finish();
	Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
}

void OpenGLContext::Present(PresentMode presentMode, int vblanks) {
	renderManager_.Present();
	frameCount_++;
}

void OpenGLContext::Invalidate(InvalidationFlags flags) {
	if (flags & InvalidationFlags::CACHED_RENDER_STATE) {
		// Unbind stuff.
		for (auto &texture : boundTextures_) {
			texture = nullptr;
		}
		for (auto &sampler : boundSamplers_) {
			sampler = nullptr;
		}
		curPipeline_ = nullptr;
	}
}

InputLayout *OpenGLContext::CreateInputLayout(const InputLayoutDesc &desc) {
	OpenGLInputLayout *fmt = new OpenGLInputLayout(&renderManager_);
	fmt->Compile(desc);
	return fmt;
}

static GLuint TypeToTarget(TextureType type) {
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
		ERROR_LOG(Log::G3D,  "Bad texture type %d", (int)type);
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
	GLRTexture *GetTex() const {
		return tex_;
	}

	void UpdateTextureLevels(GLRenderManager *render, const uint8_t *const *data, int numLevels, TextureCallback initDataCallback);

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback initDataCallback);

	GLRenderManager *render_;
	GLRTexture *tex_;

	TextureType type_;
	int mipLevels_;
	bool generateMips_;  // Generate mips requested
	bool generatedMips_;  // Has generated mips
};

OpenGLTexture::OpenGLTexture(GLRenderManager *render, const TextureDesc &desc) : render_(render) {
	_dbg_assert_(desc.format != Draw::DataFormat::UNDEFINED);
	_dbg_assert_msg_(desc.width > 0 && desc.height > 0 && desc.depth > 0, "w: %d h: %d d: %d fmt: %s", desc.width, desc.height, desc.depth, DataFormatToString(desc.format));
	_dbg_assert_(desc.width > 0 && desc.height > 0 && desc.depth > 0);
	_dbg_assert_(desc.type != Draw::TextureType::UNKNOWN);

	generatedMips_ = false;
	generateMips_ = desc.generateMips;
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	format_ = desc.format;
	type_ = desc.type;

	GLenum target = TypeToTarget(desc.type);
	tex_ = render->CreateTexture(target, desc.width, desc.height, 1, desc.mipLevels);

	mipLevels_ = desc.mipLevels;
	if (desc.initData.empty())
		return;

	UpdateTextureLevels(render, desc.initData.data(), (int)desc.initData.size(), desc.initDataCallback);
}

void OpenGLTexture::UpdateTextureLevels(GLRenderManager *render, const uint8_t * const *data, int numLevels, TextureCallback initDataCallback) {
	int level = 0;
	int width = width_;
	int height = height_;
	int depth = depth_;
	for (int i = 0; i < numLevels; i++) {
		SetImageData(0, 0, 0, width, height, depth, level, 0, data[i], initDataCallback);
		width = (width + 1) / 2;
		height = (height + 1) / 2;
		depth = (depth + 1) / 2;
		level++;
	}
	mipLevels_ = generateMips_ ? mipLevels_ : level;

	bool genMips = false;
	if (numLevels < mipLevels_ && generateMips_) {
		// Assumes the texture is bound for editing
		genMips = true;
		generatedMips_ = true;
	}
	render->FinalizeTexture(tex_, mipLevels_, genMips);
}

OpenGLTexture::~OpenGLTexture() {
	if (tex_) {
		render_->DeleteTexture(tex_);
		tex_ = nullptr;
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

void OpenGLTexture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback initDataCallback) {
	if ((width != width_ || height != height_ || depth != depth_) && level == 0) {
		// When switching to texStorage we need to handle this correctly.
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	if (!stride)
		stride = width;

	size_t alignment = DataFormatSizeInBytes(format_);
	// Make a copy of data with stride eliminated.
	uint8_t *texData = new uint8_t[(size_t)(width * height * depth * alignment)];

	bool texDataPopulated = false;
	if (initDataCallback) {
		texDataPopulated = initDataCallback(texData, data, width, height, depth, width * (int)alignment, height * width * (int)alignment);
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

	render_->TextureImage(tex_, level, width, height, depth, format_, texData);
}

#ifdef DEBUG_READ_PIXELS
// TODO: Make more generic.
static void LogReadPixelsError(GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		break;
	case GL_INVALID_ENUM:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_INVALID_ENUM");
		break;
	case GL_INVALID_VALUE:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_INVALID_VALUE");
		break;
	case GL_INVALID_OPERATION:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_INVALID_OPERATION");
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_INVALID_FRAMEBUFFER_OPERATION");
		break;
	case GL_OUT_OF_MEMORY:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_OUT_OF_MEMORY");
		break;
#ifndef USING_GLES2
	case GL_STACK_UNDERFLOW:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_STACK_UNDERFLOW");
		break;
	case GL_STACK_OVERFLOW:
		ERROR_LOG(Log::G3D, "glReadPixels: GL_STACK_OVERFLOW");
		break;
#endif
	default:
		ERROR_LOG(Log::G3D, "glReadPixels: %08x", error);
		break;
	}
}
#endif

bool OpenGLContext::CopyFramebufferToMemory(Framebuffer *src, Aspect channelBits, int x, int y, int w, int h, Draw::DataFormat dataFormat, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) {
	if (gl_extensions.IsGLES && (channelBits & Aspect::COLOR_BIT) == 0) {
		// Can't readback depth or stencil on GLES.
		return false;
	}
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)src;
	GLuint aspect = 0;
	if (channelBits & Aspect::COLOR_BIT)
		aspect |= GL_COLOR_BUFFER_BIT;
	if (channelBits & Aspect::DEPTH_BIT)
		aspect |= GL_DEPTH_BUFFER_BIT;
	if (channelBits & Aspect::STENCIL_BIT)
		aspect |= GL_STENCIL_BUFFER_BIT;
	return renderManager_.CopyFramebufferToMemory(fb ? fb->framebuffer_ : nullptr, aspect, x, y, w, h, dataFormat, (uint8_t *)pixels, pixelStride, mode, tag);
}


Texture *OpenGLContext::CreateTexture(const TextureDesc &desc) {
	return new OpenGLTexture(&renderManager_, desc);
}

void OpenGLContext::UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) {
	OpenGLTexture *tex = (OpenGLTexture *)texture;
	tex->UpdateTextureLevels(&renderManager_, data, numLevels, initDataCallback);
}

DepthStencilState *OpenGLContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	OpenGLDepthStencilState *ds = new OpenGLDepthStencilState();
	ds->depthTestEnabled = desc.depthTestEnabled;
	ds->depthWriteEnabled = desc.depthWriteEnabled;
	ds->depthComp = compToGL[(int)desc.depthCompare];
	ds->stencilEnabled = desc.stencilEnabled;
	ds->stencilCompareOp = compToGL[(int)desc.stencil.compareOp];
	ds->stencilPass = stencilOpToGL[(int)desc.stencil.passOp];
	ds->stencilFail = stencilOpToGL[(int)desc.stencil.failOp];
	ds->stencilZFail = stencilOpToGL[(int)desc.stencil.depthFailOp];
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
	~OpenGLBuffer() {
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

Pipeline *OpenGLContext::CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) {
	if (!desc.shaders.size()) {
		ERROR_LOG(Log::G3D,  "Pipeline requires at least one shader");
		return nullptr;
	}
	if ((uint32_t)desc.prim >= (uint32_t)Primitive::PRIMITIVE_TYPE_COUNT) {
		ERROR_LOG(Log::G3D, "Invalid primitive type");
		return nullptr;
	}
	if (!desc.depthStencil || !desc.blend || !desc.raster) {
		ERROR_LOG(Log::G3D,  "Incomplete prim desciption");
		return nullptr;
	}

	OpenGLPipeline *pipeline = new OpenGLPipeline(&renderManager_);
	for (auto iter : desc.shaders) {
		if (iter) {
			iter->AddRef();
			pipeline->shaders.push_back(static_cast<OpenGLShaderModule *>(iter));
		} else {
			ERROR_LOG(Log::G3D,  "ERROR: Tried to create graphics pipeline %s with a null shader module", tag ? tag : "no tag");
			delete pipeline;
			return nullptr;
		}
	}

	if (desc.uniformDesc) {
		pipeline->dynamicUniforms = *desc.uniformDesc;
	}

	pipeline->samplers_ = desc.samplers;
	if (pipeline->LinkShaders(desc)) {
		_assert_((u32)desc.prim < ARRAY_SIZE(primToGL));
		// Build the rest of the virtual pipeline object.
		pipeline->prim = primToGL[(int)desc.prim];
		pipeline->depthStencil = (OpenGLDepthStencilState *)desc.depthStencil;
		pipeline->blend = (OpenGLBlendState *)desc.blend;
		pipeline->raster = (OpenGLRasterState *)desc.raster;
		pipeline->inputLayout = (OpenGLInputLayout *)desc.inputLayout;
		return pipeline;
	} else {
		ERROR_LOG(Log::G3D,  "Failed to create pipeline %s - shaders failed to link", tag ? tag : "no tag");
		delete pipeline;
		return nullptr;
	}
}

void OpenGLContext::BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) {
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

void OpenGLContext::BindNativeTexture(int index, void *nativeTexture) {
	GLRTexture *tex = (GLRTexture *)nativeTexture;
	boundTextures_[index] = tex;
	renderManager_.BindTexture(index, tex);
}

void OpenGLContext::ApplySamplers() {
	for (int i = 0; i < MAX_TEXTURE_SLOTS; i++) {
		const OpenGLSamplerState *samp = boundSamplers_[i];
		const GLRTexture *tex = boundTextures_[i];
		if (tex) {
			_assert_msg_(samp, "Sampler missing");
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

ShaderModule *OpenGLContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) {
	OpenGLShaderModule *shader = new OpenGLShaderModule(&renderManager_, stage, tag);
	if (shader->Compile(&renderManager_, language, data, dataSize)) {
		return shader;
	} else {
		shader->Release();
		return nullptr;
	}
}

bool OpenGLPipeline::LinkShaders(const PipelineDesc &desc) {
	std::vector<GLRShader *> linkShaders;
	for (auto shaderModule : shaders) {
		if (shaderModule) {
			GLRShader *shader = shaderModule->GetShader();
			if (shader) {
				linkShaders.push_back(shader);
			} else {
				ERROR_LOG(Log::G3D,  "LinkShaders: Bad shader module");
				return false;
			}
		} else {
			ERROR_LOG(Log::G3D,  "LinkShaders: Bad shader in module");
			return false;
		}
	}

	std::vector<GLRProgram::Semantic> semantics;
	semantics.reserve(8);
	// Bind all the common vertex data points. Mismatching ones will be ignored.
	semantics.push_back({ SEM_POSITION, "Position" });
	semantics.push_back({ SEM_COLOR0, "Color0" });
	semantics.push_back({ SEM_COLOR1, "Color1" });
	semantics.push_back({ SEM_TEXCOORD0, "TexCoord0" });
	semantics.push_back({ SEM_NORMAL, "Normal" });
	semantics.push_back({ SEM_TANGENT, "Tangent" });
	semantics.push_back({ SEM_BINORMAL, "Binormal" });
	// For postshaders.
	semantics.push_back({ SEM_POSITION, "a_position" });
	semantics.push_back({ SEM_TEXCOORD0, "a_texcoord0" });

	locs_ = new PipelineLocData();
	locs_->dynamicUniformLocs_.resize(desc.uniformDesc->uniforms.size());

	std::vector<GLRProgram::UniformLocQuery> queries;
	int samplersToCheck;
	if (!samplers_.is_empty()) {
		int size = std::min((const uint32_t)samplers_.size(), MAX_TEXTURE_SLOTS);
		queries.reserve(size);
		for (int i = 0; i < size; i++) {
			queries.push_back({ &locs_->samplerLocs_[i], samplers_[i].name, true });
		}
		samplersToCheck = size;
	} else {
		queries.push_back({ &locs_->samplerLocs_[0], "sampler0" });
		queries.push_back({ &locs_->samplerLocs_[1], "sampler1" });
		queries.push_back({ &locs_->samplerLocs_[2], "sampler2" });
		samplersToCheck = 3;
	}

	_assert_(queries.size() <= MAX_TEXTURE_SLOTS);
	queries.reserve(dynamicUniforms.uniforms.size());
	for (size_t i = 0; i < dynamicUniforms.uniforms.size(); ++i) {
		queries.push_back({ &locs_->dynamicUniformLocs_[i], dynamicUniforms.uniforms[i].name });
	}
	std::vector<GLRProgram::Initializer> initialize;
	for (int i = 0; i < MAX_TEXTURE_SLOTS; ++i) {
		if (i < samplersToCheck) {
			initialize.push_back({ &locs_->samplerLocs_[i], 0, i });
		} else {
			locs_->samplerLocs_[i] = -1;
		}
	}

	GLRProgramFlags flags{};
	program_ = render_->CreateProgram(linkShaders, semantics, queries, initialize, locs_, flags);
	return true;
}

void OpenGLContext::BindPipeline(Pipeline *pipeline) {
	curPipeline_ = (OpenGLPipeline *)pipeline;
	if (!curPipeline_) {
		return;
	}
	curPipeline_->blend->Apply(&renderManager_);
	curPipeline_->depthStencil->Apply(&renderManager_, stencilRef_, stencilWriteMask_, stencilCompareMask_);
	curPipeline_->raster->Apply(&renderManager_);
	renderManager_.BindProgram(curPipeline_->program_);
}

void OpenGLContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	if (curPipeline_->dynamicUniforms.uniformBufferSize != size) {
		Crash();
	}

	for (size_t i = 0; i < curPipeline_->dynamicUniforms.uniforms.size(); ++i) {
		const auto &uniform = curPipeline_->dynamicUniforms.uniforms[i];
		const GLint &loc = curPipeline_->locs_->dynamicUniformLocs_[i];
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
	_dbg_assert_msg_(curVBuffer_ != nullptr, "Can't call Draw without a vertex buffer");
	ApplySamplers();
	_assert_(curPipeline_->inputLayout);
	renderManager_.Draw(curPipeline_->inputLayout->inputLayout_, curVBuffer_->buffer_, curVBufferOffset_, curPipeline_->prim, offset, vertexCount);
}

void OpenGLContext::DrawIndexed(int vertexCount, int offset) {
	_dbg_assert_msg_(curVBuffer_ != nullptr, "Can't call DrawIndexed without a vertex buffer");
	_dbg_assert_msg_(curIBuffer_ != nullptr, "Can't call DrawIndexed without an index buffer");
	ApplySamplers();
	_assert_(curPipeline_->inputLayout);
	renderManager_.DrawIndexed(
		curPipeline_->inputLayout->inputLayout_,
		curVBuffer_->buffer_, curVBufferOffset_,
		curIBuffer_->buffer_, curIBufferOffset_ + offset * sizeof(uint32_t),
		curPipeline_->prim, vertexCount, GL_UNSIGNED_SHORT);
}

void OpenGLContext::DrawUP(const void *vdata, int vertexCount) {
	_assert_(curPipeline_->inputLayout != nullptr);
	int stride = curPipeline_->inputLayout->stride;
	uint32_t dataSize = stride * vertexCount;

	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];

	GLRBuffer *buf;
	uint32_t offset;
	uint8_t *dest = frameData.push->Allocate(dataSize, 4, &buf, &offset);
	memcpy(dest, vdata, dataSize);

	ApplySamplers();
	_assert_(curPipeline_->inputLayout);
	renderManager_.Draw(curPipeline_->inputLayout->inputLayout_, buf, offset, curPipeline_->prim, 0, vertexCount);
}

void OpenGLContext::DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) {
	_assert_(curPipeline_->inputLayout != nullptr);
	int stride = curPipeline_->inputLayout->stride;
	uint32_t vdataSize = stride * vertexCount;
	uint32_t idataSize = indexCount * sizeof(u16);

	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];

	GLRBuffer *vbuf;
	uint32_t voffset;
	uint8_t *dest = frameData.push->Allocate(vdataSize, 4, &vbuf, &voffset);
	memcpy(dest, vdata, vdataSize);

	GLRBuffer *ibuf;
	uint32_t ioffset;
	dest = frameData.push->Allocate(idataSize, 4, &ibuf, &ioffset);
	memcpy(dest, idata, idataSize);

	ApplySamplers();
	renderManager_.DrawIndexed(curPipeline_->inputLayout->inputLayout_, vbuf, voffset, ibuf, ioffset, curPipeline_->prim, indexCount, GL_UNSIGNED_SHORT, 1);
}

void OpenGLContext::DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) {
	if (draws.is_empty() || !vertexCount || !indexCount) {
		return;
	}

	BindPipeline(draws[0].pipeline);
	UpdateDynamicUniformBuffer(ub, ubSize);

	_assert_(curPipeline_->inputLayout != nullptr);
	int stride = curPipeline_->inputLayout->stride;
	uint32_t vdataSize = stride * vertexCount;
	int indexSize = sizeof(u16);
	uint32_t idataSize = indexCount * indexSize;

	FrameData &frameData = frameData_[renderManager_.GetCurFrame()];

	GLRBuffer *vbuf;
	uint32_t voffset;
	uint8_t *dest = frameData.push->Allocate(vdataSize, 4, &vbuf, &voffset);
	memcpy(dest, vdata, vdataSize);

	GLRBuffer *ibuf;
	uint32_t ioffset;
	dest = frameData.push->Allocate(idataSize, 4, &ibuf, &ioffset);
	memcpy(dest, idata, idataSize);

	ApplySamplers();
	for (auto &draw : draws) {
		if (draw.pipeline != curPipeline_) {
			OpenGLPipeline *glPipeline = (OpenGLPipeline *)draw.pipeline;
			_dbg_assert_(glPipeline->inputLayout->stride == stride);
			BindPipeline(glPipeline);  // this updated curPipeline_.
			UpdateDynamicUniformBuffer(ub, ubSize);
		}
		if (draw.bindTexture) {
			renderManager_.BindTexture(0, ((OpenGLTexture *)draw.bindTexture)->GetTex());
		} else if (draw.bindFramebufferAsTex) {
			renderManager_.BindFramebufferAsTexture(((OpenGLFramebuffer*)draw.bindFramebufferAsTex)->framebuffer_, 0, GL_COLOR_BUFFER_BIT);
		}
		GLRect2D scissor;
		scissor.x = draw.clipx;
		scissor.y = draw.clipy;
		scissor.w = draw.clipw;
		scissor.h = draw.cliph;
		renderManager_.SetScissor(scissor);
		renderManager_.DrawIndexed(curPipeline_->inputLayout->inputLayout_, vbuf, voffset, ibuf, ioffset + draw.indexOffset * indexSize, curPipeline_->prim, draw.indexCount, GL_UNSIGNED_SHORT, 1);
	}
}

void OpenGLContext::Clear(Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) {
	float col[4];
	Uint8x4ToFloat4(col, colorval);
	GLuint glMask = 0;
	if (aspects & Aspect::COLOR_BIT) {
		glMask |= GL_COLOR_BUFFER_BIT;
	}
	if (aspects & Aspect::DEPTH_BIT) {
		glMask |= GL_DEPTH_BUFFER_BIT;
	}
	if (aspects & Aspect::STENCIL_BIT) {
		glMask |= GL_STENCIL_BUFFER_BIT;
	}
	renderManager_.Clear(colorval, depthVal, stencilVal, glMask, 0xF, 0, 0, targetWidth_, targetHeight_);
}

DrawContext *T3DCreateGLContext(bool canChangeSwapInterval) {
	return new OpenGLContext(canChangeSwapInterval);
}

OpenGLInputLayout::~OpenGLInputLayout() {
	render_->DeleteInputLayout(inputLayout_);
}

void OpenGLInputLayout::Compile(const InputLayoutDesc &desc) {
	// TODO: This is only accurate if there's only one stream. But whatever, for now we
	// never use multiple streams anyway.
	stride = desc.stride;

	std::vector<GLRInputLayout::Entry> entries;
	for (auto &attr : desc.attributes) {
		GLRInputLayout::Entry entry;
		entry.location = attr.location;
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
			ERROR_LOG(Log::G3D,  "Thin3DGLVertexFormat: Invalid or unknown component type applied.");
			break;
		}

		entries.push_back(entry);
	}
	if (!entries.empty()) {
		inputLayout_ = render_->CreateInputLayout(entries, stride);
	} else {
		inputLayout_ = nullptr;
	}
}

Framebuffer *OpenGLContext::CreateFramebuffer(const FramebufferDesc &desc) {
	CheckGLExtensions();

	// TODO: Support multiview later. (It's our only use case for multi layers).
	_dbg_assert_(desc.numLayers == 1);

	GLRFramebuffer *framebuffer = renderManager_.CreateFramebuffer(desc.width, desc.height, desc.z_stencil, desc.tag);
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

void OpenGLContext::CopyFramebufferImage(Framebuffer *fbsrc, int srcLevel, int srcX, int srcY, int srcZ, Framebuffer *fbdst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;

	int glAspect = 0;
	if (aspects & Aspect::COLOR_BIT) {
		glAspect |= GL_COLOR_BUFFER_BIT;
	} else if (aspects & (Aspect::STENCIL_BIT | Aspect::DEPTH_BIT)) {
		if (aspects & Aspect::DEPTH_BIT)
			glAspect |= GL_DEPTH_BUFFER_BIT;
		if (aspects & Aspect::STENCIL_BIT)
			glAspect |= GL_STENCIL_BUFFER_BIT;
	}
	renderManager_.CopyFramebuffer(src->framebuffer_, GLRect2D{ srcX, srcY, width, height }, dst->framebuffer_, GLOffset2D{ dstX, dstY }, glAspect, tag);
}

bool OpenGLContext::BlitFramebuffer(Framebuffer *fbsrc, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *fbdst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter linearFilter, const char *tag) {
	OpenGLFramebuffer *src = (OpenGLFramebuffer *)fbsrc;
	OpenGLFramebuffer *dst = (OpenGLFramebuffer *)fbdst;
	GLuint aspect = 0;
	if (aspects & Aspect::COLOR_BIT)
		aspect |= GL_COLOR_BUFFER_BIT;
	if (aspects & Aspect::DEPTH_BIT)
		aspect |= GL_DEPTH_BUFFER_BIT;
	if (aspects & Aspect::STENCIL_BIT)
		aspect |= GL_STENCIL_BUFFER_BIT;

	renderManager_.BlitFramebuffer(src->framebuffer_, GLRect2D{ srcX1, srcY1, srcX2 - srcX1, srcY2 - srcY1 }, dst->framebuffer_, GLRect2D{ dstX1, dstY1, dstX2 - dstX1, dstY2 - dstY1 }, aspect, linearFilter == FB_BLIT_LINEAR, tag);
	return true;
}

void OpenGLContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect aspects, int layer) {
	OpenGLFramebuffer *fb = (OpenGLFramebuffer *)fbo;
	_assert_(binding < MAX_TEXTURE_SLOTS);

	GLuint glAspect = 0;
	if (aspects & Aspect::COLOR_BIT) {
		glAspect |= GL_COLOR_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->color_texture;
	}
	if (aspects & Aspect::DEPTH_BIT) {
		glAspect |= GL_DEPTH_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->z_stencil_texture;
	}
	if (aspects & Aspect::STENCIL_BIT) {
		glAspect |= GL_STENCIL_BUFFER_BIT;
		boundTextures_[binding] = &fb->framebuffer_->z_stencil_texture;
	}
	renderManager_.BindFramebufferAsTexture(fb->framebuffer_, binding, glAspect);
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

uint64_t OpenGLContext::GetNativeObject(NativeObject obj, void *srcObject) {
	switch (obj) {
	case NativeObject::RENDER_MANAGER:
		return (uint64_t)(uintptr_t)&renderManager_;
	case NativeObject::TEXTURE_VIEW:  // Gets the GLRTexture *
		return (uint64_t)(((OpenGLTexture *)srcObject)->GetTex());
	default:
		return 0;
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
		return FMT_TEXTURE;
	case DataFormat::R16_UNORM:
		if (!gl_extensions.IsGLES) {
			return FMT_TEXTURE;
		} else {
			return 0;
		}
		break;

	case DataFormat::BC1_RGBA_UNORM_BLOCK:
	case DataFormat::BC2_UNORM_BLOCK:
	case DataFormat::BC3_UNORM_BLOCK:
		return gl_extensions.supportsBC123 ? FMT_TEXTURE : 0;

	case DataFormat::BC4_UNORM_BLOCK:
	case DataFormat::BC5_UNORM_BLOCK:
		return gl_extensions.supportsBC45 ? FMT_TEXTURE : 0;

	case DataFormat::BC7_UNORM_BLOCK:
		return gl_extensions.supportsBC7 ? FMT_TEXTURE : 0;

	case DataFormat::ASTC_4x4_UNORM_BLOCK:
		return gl_extensions.supportsASTC ? FMT_TEXTURE : 0;

	case DataFormat::ETC2_R8G8B8_UNORM_BLOCK:
	case DataFormat::ETC2_R8G8B8A1_UNORM_BLOCK:
	case DataFormat::ETC2_R8G8B8A8_UNORM_BLOCK:
		return gl_extensions.supportsETC2 ? FMT_TEXTURE : 0;

	default:
		return 0;
	}
}

}  // namespace Draw
