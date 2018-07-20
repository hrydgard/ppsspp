#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "gfx/gl_common.h"
#include "thin3d/DataFormat.h"

struct GLRViewport {
	float x, y, w, h, minZ, maxZ;
};

struct GLRect2D {
	int x, y, w, h;
};

struct GLOffset2D {
	int x, y;
};

enum class GLRAllocType {
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
	STENCILFUNC,
	STENCILOP,
	BLEND,
	BLENDCOLOR,
	LOGICOP,
	UNIFORM4I,
	UNIFORM4F,
	UNIFORMMATRIX,
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
	BIND_BUFFER,
	GENMIPS,
	DRAW,
	DRAW_INDEXED,
	PUSH_CONSTANTS,
	TEXTURE_SUBIMAGE,
};

// TODO: Bloated since the biggest struct decides the size. Will need something more efficient (separate structs with shared
// type field, smashed right after each other?)
// Also, all GLenums are really only 16 bits.
struct GLRRenderData {
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
			GLboolean enabled;
			GLenum func;
			uint8_t ref;
			uint8_t compareMask;
		} stencilFunc;
		struct {
			GLenum sFail;
			GLenum zFail;
			GLenum pass;
			uint8_t writeMask;
		} stencilOp;  // also write mask
		struct {
			GLenum mode;  // primitive
			GLint buffer;
			GLint first;
			GLint count;
		} draw;
		struct {
			GLenum mode;  // primitive
			GLint count;
			GLint instances;
			GLint indexType;
			void *indices;
		} drawIndexed;
		struct {
			const char *name;  // if null, use loc
			GLint *loc; // NOTE: This is a pointer so we can immediately use things that are "queried" during program creation.
			GLint count;
			float v[4];
		} uniform4;
		struct {
			const char *name;  // if null, use loc
			GLint *loc;
			float m[16];
		} uniformMatrix4;
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
		} clear;
		struct {
			int slot;
			GLRTexture *texture;
		} texture;
		struct {
			GLRTexture *texture;
			GLenum format;
			GLenum type;
			int level;
			int x;
			int y;
			int width;
			int height;
			GLRAllocType allocType;
			uint8_t *data;  // owned, delete[]-d
		} texture_subimage;
		struct {
			int slot;
			GLRFramebuffer *framebuffer;
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
			GLRInputLayout *inputLayout;
			GLRBuffer *buffer;
			size_t offset;
		} bindVertexBuffer;
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
			GLboolean cullEnable;
			GLenum frontFace;
			GLenum cullFace;
			GLboolean ditherEnable;
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
			GLenum target;
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
			GLenum internalFormat;
			GLenum format;
			GLenum type;
			int level;
			int width;
			int height;
			GLRAllocType allocType;
			bool linearFilter;
			uint8_t *data;  // owned, delete[]-d
		} texture_image;
		struct {
			GLRTexture *texture;
			int maxLevel;
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
};

enum class GLRRenderPassAction {
	DONT_CARE,
	CLEAR,
	KEEP,
};

class GLRFramebuffer;

enum {
	GLR_ASPECT_COLOR = 1,
	GLR_ASPECT_DEPTH = 2,
	GLR_ASPECT_STENCIL = 3,
};

struct GLRStep {
	GLRStep(GLRStepType _type) : stepType(_type) {}
	GLRStepType stepType;
	std::vector<GLRRenderData> commands;
	union {
		struct {
			GLRFramebuffer *framebuffer;
			int numDraws;
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
			int aspectMask;
			GLRFramebuffer *src;
			GLRect2D srcRect;
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

	void RunInitSteps(const std::vector<GLRInitStep> &steps, bool skipGLCalls);

	void RunSteps(const std::vector<GLRStep *> &steps, bool skipGLCalls);
	void LogSteps(const std::vector<GLRStep *> &steps);

	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	inline int RPIndex(GLRRenderPassAction color, GLRRenderPassAction depth) {
		return (int)depth * 3 + (int)color;
	}

	void CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

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
	void PerformRenderPass(const GLRStep &pass);
	void PerformCopy(const GLRStep &pass);
	void PerformBlit(const GLRStep &pass);
	void PerformReadback(const GLRStep &pass);
	void PerformReadbackImage(const GLRStep &pass);

	void LogRenderPass(const GLRStep &pass);
	void LogCopy(const GLRStep &pass);
	void LogBlit(const GLRStep &pass);
	void LogReadback(const GLRStep &pass);
	void LogReadbackImage(const GLRStep &pass);

	void ResizeReadbackBuffer(size_t requiredSize);

	void fbo_ext_create(const GLRInitStep &step);
	void fbo_bind_fb_target(bool read, GLuint name);
	GLenum fbo_get_fb_target(bool read, GLuint **cached);
	void fbo_unbind();

	GLRFramebuffer *curFB_ = nullptr;

	GLuint globalVAO_ = 0;

	int curFBWidth_ = 0;
	int curFBHeight_ = 0;
	int targetWidth_ = 0;
	int targetHeight_ = 0;

	// Readback buffer. Currently we only support synchronous readback, so we only really need one.
	// We size it generously.
	uint8_t *readbackBuffer_ = nullptr;
	int readbackBufferSize_ = 0;
	// Temp buffer for color conversion
	uint8_t *tempBuffer_ = nullptr;
	int tempBufferSize_ = 0;

	float maxAnisotropyLevel_ = 0.0f;

	// Framebuffer state?
	GLuint currentDrawHandle_ = 0;
	GLuint currentReadHandle_ = 0;

	GLuint AllocTextureName();
	// Texture name cache. Ripped straight from TextureCacheGLES.
	std::vector<GLuint> nameCache_;
	std::unordered_map<int, std::string> glStrings_;

	bool sawOutOfMemory_ = false;
};
