#pragma once

#include <cstdint>
#include <vector>
#include <set>
#include <unordered_map>

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFrameData.h"
#include "Common/GPU/DataFormat.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Collections/TinySet.h"
#include "Common/Data/Collections/FastVec.h"

struct GLRViewport {
	float x, y, w, h, minZ, maxZ;
};

struct GLRect2D {
	int x, y, w, h;
};

struct GLOffset2D {
	int x, y;
};

enum class GLRAllocType : uint8_t {
	NONE,
	NEW,
	ALIGNED,
};

class GLRShader;
class GLRTexture;
class GLRBuffer;
class GLRFramebuffer;
class GLRProgram;
class GLRInputLayout;

enum class GLRRenderCommand : uint8_t {
	DEPTH,
	STENCIL,
	BLEND,
	BLENDCOLOR,
	LOGICOP,
	UNIFORM4I,
	UNIFORM4UI,
	UNIFORM4F,
	UNIFORMMATRIX,
	UNIFORMSTEREOMATRIX,
	TEXTURESAMPLER,
	TEXTURELOD,
	VIEWPORT,
	SCISSOR,
	RASTER,
	CLEAR,
	INVALIDATE,
	BINDPROGRAM,
	BINDTEXTURE,
	BIND_FB_TEXTURE,
	BIND_VERTEX_BUFFER,
	GENMIPS,
	DRAW,
	TEXTURE_SUBIMAGE,
};

// TODO: Bloated since the biggest struct decides the size. Will need something more efficient (separate structs with shared
// type field, smashed right after each other?)
// Also, all GLenums are really only 16 bits.
struct GLRRenderData {
	GLRRenderData(GLRRenderCommand _cmd) : cmd(_cmd) {}
	GLRRenderCommand cmd;
	union {
		struct {
			GLboolean enabled;
			GLenum srcColor;
			GLenum dstColor;
			GLenum srcAlpha;
			GLenum dstAlpha;
			GLenum funcColor;
			GLenum funcAlpha;
			int mask;
		} blend;
		struct {
			float color[4];
		} blendColor;
		struct {
			GLboolean enabled;
			GLenum logicOp;
		} logic;
		struct {
			GLboolean enabled;
			GLboolean write;
			GLenum func;
		} depth;
		struct {
			GLenum func;
			GLenum sFail;
			GLenum zFail;
			GLenum pass;
			GLboolean enabled;
			uint8_t ref;
			uint8_t compareMask;
			uint8_t writeMask;
		} stencil;
		struct {
			GLRInputLayout *inputLayout;
			GLRBuffer *vertexBuffer;
			GLRBuffer *indexBuffer;
			uint32_t vertexOffset;
			uint32_t indexOffset;
			GLenum mode;  // primitive
			GLint first;
			GLint count;
			GLint indexType;
			GLint instances;
		} draw;
		struct {
			const char *name;  // if null, use loc
			const GLint *loc; // NOTE: This is a pointer so we can immediately use things that are "queried" during program creation.
			GLint count;
			float v[4];
		} uniform4;
		struct {
			const char *name;  // if null, use loc
			const GLint *loc;
			float m[16];
		} uniformMatrix4;
		struct {
			const char *name;  // if null, use loc
			const GLint *loc;
			float *mData;  // new'd, 32 entries
		} uniformStereoMatrix4;
		struct {
			uint32_t clearColor;
			float clearZ;
			uint8_t clearStencil;
			uint8_t colorMask; // Like blend, but for the clear.
			GLuint clearMask;   // GL_COLOR_BUFFER_BIT etc
			int16_t scissorX;
			int16_t scissorY;
			int16_t scissorW;
			int16_t scissorH;
		} clear;  // also used for invalidate
		struct {
			int slot;
			GLRTexture *texture;
		} texture;
		struct {
			GLRTexture *texture;
			Draw::DataFormat format;
			uint8_t slot;
			uint8_t level;
			uint16_t width;
			uint16_t height;
			uint16_t x;
			uint16_t y;
			GLRAllocType allocType;
			uint8_t *data;  // owned, delete[]-d
		} texture_subimage;
		struct {
			GLRFramebuffer* framebuffer;
			int slot;
			int aspect;
		} bind_fb_texture;
		struct {
			GLRBuffer *buffer;
			GLuint target;
		} bind_buffer;
		struct {
			GLRProgram *program;
		} program;
		struct {
			int slot;
			GLenum wrapS;
			GLenum wrapT;
			GLenum magFilter;
			GLenum minFilter;  // also includes mip. GL...
			float anisotropy;
		} textureSampler;
		struct {
			int slot;
			float minLod;
			float maxLod;
			float lodBias;
		} textureLod;
		struct {
			GLRViewport vp;
		} viewport;
		struct {
			GLRect2D rc;
		} scissor;
		struct {
			GLenum frontFace;
			GLenum cullFace;
			GLboolean cullEnable;
			GLboolean ditherEnable;
			GLboolean depthClampEnable;
		} raster;
	};
};

// Unlike in Vulkan, we can't create stuff on the main thread, but need to
// defer this too. A big benefit will be that we'll be able to do all creation
// at the start of the frame.
enum class GLRInitStepType : uint8_t {
	CREATE_TEXTURE,
	CREATE_SHADER,
	CREATE_PROGRAM,
	CREATE_BUFFER,
	CREATE_INPUT_LAYOUT,
	CREATE_FRAMEBUFFER,

	TEXTURE_IMAGE,
	TEXTURE_FINALIZE,
	BUFFER_SUBDATA,
};

struct GLRInitStep {
	GLRInitStep(GLRInitStepType _type) : stepType(_type) {}
	GLRInitStepType stepType;
	union {
		struct {
			GLRTexture *texture;
		} create_texture;
		struct {
			GLRShader *shader;
			// This char arrays needs to be allocated with new[].
			char *code;
			GLuint stage;
		} create_shader;
		struct {
			GLRProgram *program;
			GLRShader *shaders[3];
			int num_shaders;
			bool support_dual_source;
		} create_program;
		struct {
			GLRBuffer *buffer;
			int size;
			GLuint usage;
		} create_buffer;
		struct {
			GLRInputLayout *inputLayout;
		} create_input_layout;
		struct {
			GLRFramebuffer *framebuffer;
		} create_framebuffer;
		struct {
			GLRBuffer *buffer;
			int offset;
			int size;
			uint8_t *data;  // owned, delete[]-d
			bool deleteData;
		} buffer_subdata;
		struct {
			GLRTexture *texture;
			Draw::DataFormat format;
			int level;
			uint16_t width;
			uint16_t height;
			uint16_t depth;
			GLRAllocType allocType;
			bool linearFilter;
			uint8_t *data;  // owned, delete[]-d
		} texture_image;
		struct {
			GLRTexture *texture;
			int loadedLevels;
			bool genMips;
		} texture_finalize;
	};
};

enum class GLRStepType : uint8_t {
	RENDER,
	COPY,
	BLIT,
	READBACK,
	READBACK_IMAGE,
	RENDER_SKIP,
};

enum class GLRRenderPassAction {
	DONT_CARE,
	CLEAR,
	KEEP,
};

class GLRFramebuffer;

enum GLRAspect {
	GLR_ASPECT_COLOR = 1,
	GLR_ASPECT_DEPTH = 2,
	GLR_ASPECT_STENCIL = 3,
};
const char *GLRAspectToString(GLRAspect aspect);

struct GLRStep {
	GLRStep(GLRStepType _type) : stepType(_type), tag() {}
	GLRStepType stepType;
	FastVec<GLRRenderData> commands;
	TinySet<const GLRFramebuffer *, 8> dependencies;
	const char *tag;
	union {
		struct {
			GLRFramebuffer *framebuffer;
			GLRRenderPassAction color;
			GLRRenderPassAction depth;
			GLRRenderPassAction stencil;
		} render;
		struct {
			GLRFramebuffer *src;
			GLRFramebuffer *dst;
			GLRect2D srcRect;
			GLOffset2D dstPos;
			int aspectMask;
		} copy;
		struct {
			GLRFramebuffer *src;
			GLRFramebuffer *dst;
			GLRect2D srcRect;
			GLRect2D dstRect;
			int aspectMask;
			GLboolean filter;
		} blit;
		struct {
			GLRFramebuffer* src;
			GLRect2D srcRect;
			int aspectMask;
			Draw::DataFormat dstFormat;
		} readback;
		struct {
			GLRTexture *texture;
			GLRect2D srcRect;
			int mipLevel;
		} readback_image;
	};
};

class GLQueueRunner {
public:
	GLQueueRunner() {}

	void SetErrorCallback(ErrorCallbackFn callback, void *userdata) {
		errorCallback_ = callback;
		errorCallbackUserData_ = userdata;
	}

	void SetDeviceCaps(const Draw::DeviceCaps &caps) {
		caps_ = caps;
	}

	void RunInitSteps(const FastVec<GLRInitStep> &steps, bool skipGLCalls);

	void RunSteps(const std::vector<GLRStep *> &steps, GLFrameData &frameData, bool skipGLCalls, bool keepSteps, bool useVR);

	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	void CopyFromReadbackBuffer(GLRFramebuffer *framebuffer, int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

	void Resize(int width, int height) {
		targetWidth_ = width;
		targetHeight_ = height;
	}

	bool SawOutOfMemory() {
		return sawOutOfMemory_;
	}

	std::string GetGLString(int name) const {
		auto it = glStrings_.find(name);
		return it != glStrings_.end() ? it->second : "";
	}

private:
	void InitCreateFramebuffer(const GLRInitStep &step);

	void PerformBindFramebufferAsRenderTarget(const GLRStep &pass);
	void PerformRenderPass(const GLRStep &pass, bool first, bool last, GLQueueProfileContext &profile);
	void PerformCopy(const GLRStep &pass);
	void PerformBlit(const GLRStep &pass);
	void PerformReadback(const GLRStep &pass);
	void PerformReadbackImage(const GLRStep &pass);

	void fbo_ext_create(const GLRInitStep &step);
	void fbo_bind_fb_target(bool read, GLuint name);
	GLenum fbo_get_fb_target(bool read, GLuint **cached);
	void fbo_unbind();

	std::string StepToString(const GLRStep &step) const;

	GLRFramebuffer *curFB_ = nullptr;

	GLuint globalVAO_ = 0;

	Draw::DeviceCaps caps_{};  // For sanity checks.

	int curFBWidth_ = 0;
	int curFBHeight_ = 0;
	int targetWidth_ = 0;
	int targetHeight_ = 0;

	// Readback buffer. Currently we only support synchronous readback, so we only really need one.
	// We size it generously.
	uint8_t *readbackBuffer_ = nullptr;
	int readbackBufferSize_ = 0;
	uint32_t readbackAspectMask_ = 0;

	float maxAnisotropyLevel_ = 0.0f;

	// Framebuffer state?
	GLuint currentDrawHandle_ = 0;
	GLuint currentReadHandle_ = 0;

	std::unordered_map<int, std::string> glStrings_;

	bool sawOutOfMemory_ = false;
	bool useDebugGroups_ = false;

	ErrorCallbackFn errorCallback_ = nullptr;
	void *errorCallbackUserData_ = nullptr;
};

const char *RenderCommandToString(GLRRenderCommand cmd);
