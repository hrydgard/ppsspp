#pragma once

#include <thread>
#include <unordered_map>
#include <vector>
#include <functional>
#include <set>
#include <string>
#include <mutex>
#include <queue>
#include <condition_variable>

#include "Common/GPU/MiscTypes.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Log.h"
#include "Common/GPU/OpenGL/GLQueueRunner.h"
#include "Common/GPU/OpenGL/GLFrameData.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLMemory.h"

class GLRInputLayout;
class GLPushBuffer;

namespace Draw {
class DrawContext;
}

constexpr int MAX_GL_TEXTURE_SLOTS = 8;

class GLRTexture {
public:
	GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int numMips);
	~GLRTexture();

	GLuint texture = 0;
	uint16_t w;
	uint16_t h;
	uint16_t d;

	// We don't trust OpenGL defaults - setting wildly off values ensures that we'll end up overwriting these parameters.
	GLenum target = 0xFFFF;
	GLenum wrapS = 0xFFFF;
	GLenum wrapT = 0xFFFF;
	GLenum magFilter = 0xFFFF;
	GLenum minFilter = 0xFFFF;
	uint8_t numMips = 0;
	bool canWrap = true;
	float anisotropy = -100000.0f;
	float minLod = -1000.0f;
	float maxLod = 1000.0f;
	float lodBias = 0.0f;
};

class GLRFramebuffer {
public:
	GLRFramebuffer(const Draw::DeviceCaps &caps, int _width, int _height, bool z_stencil, const char *tag)
		: color_texture(caps, _width, _height, 1, 1), z_stencil_texture(caps, _width, _height, 1, 1),
		width(_width), height(_height), z_stencil_(z_stencil) {
	}
	~GLRFramebuffer();

	const char *Tag() const { return tag_.c_str(); }

	GLuint handle = 0;
	GLRTexture color_texture;
	// Either z_stencil_texture, z_stencil_buffer, or (z_buffer and stencil_buffer) are set.
	GLuint z_stencil_buffer = 0;
	GLRTexture z_stencil_texture;
	GLuint z_buffer = 0;
	GLuint stencil_buffer = 0;

	int width;
	int height;
	GLuint colorDepth = 0;
	bool z_stencil_;

private:
	std::string tag_;
};

// We need to create some custom heap-allocated types so we can forward things that need to be created on the GL thread, before
// they've actually been created.

class GLRShader {
public:
	~GLRShader() {
		if (shader) {
			glDeleteShader(shader);
		}
	}

	GLuint shader = 0;
	bool valid = false;
	// Warning: Won't know until a future frame.
	bool failed = false;
	std::string desc;
	std::string code;
	std::string error;
};

struct GLRProgramFlags {
	bool supportDualSource : 1;
	bool useClipDistance0 : 1;
	bool useClipDistance1 : 1;
	bool useClipDistance2 : 1;
};

// Unless you manage lifetimes in some smart way,
// your loc data for uniforms and samplers need to be in a struct
// derived from this, and passed into CreateProgram.
class GLRProgramLocData {
public:
	virtual ~GLRProgramLocData() {}
};

class GLRProgram {
public:
	~GLRProgram() {
		if (deleteCallback_) {
			deleteCallback_(deleteParam_);
		}
		if (program) {
			glDeleteProgram(program);
		}
		delete locData_;
	}
	struct Semantic {
		int location;
		const char *attrib;
	};

	struct UniformLocQuery {
		GLint *dest;
		const char *name;
		bool required;
	};

	struct Initializer {
		GLint *uniform;
		int type;
		int value;
	};

	GLuint program = 0;
	std::vector<Semantic> semantics_;
	std::vector<UniformLocQuery> queries_;
	std::vector<Initializer> initialize_;

	GLRProgramLocData *locData_;
	bool use_clip_distance[8]{};

	struct UniformInfo {
		int loc_;
	};

	// Must ONLY be called from GLQueueRunner!
	// Also it's pretty slow...
	int GetUniformLoc(const char *name) {
		auto iter = uniformCache_.find(std::string(name));
		int loc = -1;
		if (iter != uniformCache_.end()) {
			loc = iter->second.loc_;
		} else {
			loc = glGetUniformLocation(program, name);
			UniformInfo info;
			info.loc_ = loc;
			uniformCache_[name] = info;
		}
		return loc;
	}

	void SetDeleteCallback(void(*cb)(void *), void *p) {
		deleteCallback_ = cb;
		deleteParam_ = p;
	}

private:
	void(*deleteCallback_)(void *) = nullptr;
	void *deleteParam_ = nullptr;

	std::unordered_map<std::string, UniformInfo> uniformCache_;
};

class GLRInputLayout {
public:
	struct Entry {
		int location;
		int count;
		GLenum type;
		GLboolean normalized;
		intptr_t offset;
	};
	std::vector<Entry> entries;
	int stride;
	int semanticsMask_ = 0;
};

enum class GLRRunType {
	SUBMIT,
	PRESENT,
	SYNC,
	EXIT,
};

class GLRenderManager;
class GLPushBuffer;

// These are enqueued from the main thread, and the render thread pops them off
struct GLRRenderThreadTask {
	GLRRenderThreadTask(GLRRunType _runType) : runType(_runType) {}

	std::vector<GLRStep *> steps;
	FastVec<GLRInitStep> initSteps;

	int frame = -1;
	GLRRunType runType;

	// Avoid copying these by accident.
	GLRRenderThreadTask(GLRRenderThreadTask &) = delete;
	GLRRenderThreadTask& operator =(GLRRenderThreadTask &) = delete;
};

// Note: The GLRenderManager is created and destroyed on the render thread, and the latter
// happens after the emu thread has been destroyed. Therefore, it's safe to run wild deleting stuff
// directly in the destructor.
class GLRenderManager {
public:
	GLRenderManager(HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory);
	~GLRenderManager();

	GLRenderManager(GLRenderManager &) = delete;
	GLRenderManager &operator=(GLRenderManager &) = delete;

	void SetInvalidationCallback(InvalidationCallback callback) {
		invalidationCallback_ = callback;
	}

	void ThreadStart(Draw::DrawContext *draw);
	void ThreadEnd();
	bool ThreadFrame();  // Returns true if it did anything. False means the queue was empty.

	void SetErrorCallback(ErrorCallbackFn callback, void *userdata) {
		queueRunner_.SetErrorCallback(callback, userdata);
	}
	void SetDeviceCaps(const Draw::DeviceCaps &caps) {
		queueRunner_.SetDeviceCaps(caps);
		caps_ = caps;
	}

	std::string GetGpuProfileString() const;

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame(bool enableProfiling);
	// Can run on a different thread!
	void Finish();
	void Present();

	// Creation commands. These were not needed in Vulkan since there we can do that on the main thread.
	// We pass in width/height here even though it's not strictly needed until we support glTextureStorage
	// and then we'll also need formats and stuff.
	GLRTexture *CreateTexture(GLenum target, int width, int height, int depth, int numMips) {
		_dbg_assert_(target != 0);
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_TEXTURE;
		step.create_texture.texture = new GLRTexture(caps_, width, height, depth, numMips);
		step.create_texture.texture->target = target;
		return step.create_texture.texture;
	}

	GLRBuffer *CreateBuffer(GLuint target, size_t size, GLuint usage) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_BUFFER;
		step.create_buffer.buffer = new GLRBuffer(target, size);
		step.create_buffer.size = (int)size;
		step.create_buffer.usage = usage;
		return step.create_buffer.buffer;
	}

	GLRShader *CreateShader(GLuint stage, const std::string &code, const std::string &desc) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_SHADER;
		step.create_shader.shader = new GLRShader();
		step.create_shader.shader->desc = desc;
		step.create_shader.stage = stage;
		step.create_shader.code = new char[code.size() + 1];
		memcpy(step.create_shader.code, code.data(), code.size() + 1);
		return step.create_shader.shader;
	}

	GLRFramebuffer *CreateFramebuffer(int width, int height, bool z_stencil, const char *tag) {
		_dbg_assert_(width > 0 && height > 0 && tag != nullptr);

		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_FRAMEBUFFER;
		step.create_framebuffer.framebuffer = new GLRFramebuffer(caps_, width, height, z_stencil, tag);
		return step.create_framebuffer.framebuffer;
	}

	// Can't replace uniform initializers with direct calls to SetUniform() etc because there might
	// not be an active render pass.
	GLRProgram *CreateProgram(
		std::vector<GLRShader *> shaders, std::vector<GLRProgram::Semantic> semantics, std::vector<GLRProgram::UniformLocQuery> queries,
		std::vector<GLRProgram::Initializer> initializers, GLRProgramLocData *locData, const GLRProgramFlags &flags) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_PROGRAM;
		_assert_(shaders.size() <= ARRAY_SIZE(step.create_program.shaders));
		step.create_program.program = new GLRProgram();
		step.create_program.program->semantics_ = semantics;
		step.create_program.program->queries_ = queries;
		step.create_program.program->initialize_ = initializers;
		step.create_program.program->locData_ = locData;
		step.create_program.program->use_clip_distance[0] = flags.useClipDistance0;
		step.create_program.program->use_clip_distance[1] = flags.useClipDistance1;
		step.create_program.program->use_clip_distance[2] = flags.useClipDistance2;
		step.create_program.support_dual_source = flags.supportDualSource;
		_assert_msg_(shaders.size() > 0, "Can't create a program with zero shaders");
		for (size_t i = 0; i < shaders.size(); i++) {
			step.create_program.shaders[i] = shaders[i];
		}
#ifdef _DEBUG
		for (auto &iter : queries) {
			_dbg_assert_(iter.name);
		}
		for (auto &sem : semantics) {
			_dbg_assert_(sem.attrib);
		}
#endif
		step.create_program.num_shaders = (int)shaders.size();
		return step.create_program.program;
	}

	GLRInputLayout *CreateInputLayout(const std::vector<GLRInputLayout::Entry> &entries, int stride) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::CREATE_INPUT_LAYOUT;
		step.create_input_layout.inputLayout = new GLRInputLayout();
		step.create_input_layout.inputLayout->entries = entries;
		step.create_input_layout.inputLayout->stride = stride;
		for (auto &iter : step.create_input_layout.inputLayout->entries) {
			step.create_input_layout.inputLayout->semanticsMask_ |= 1 << iter.location;
		}
		return step.create_input_layout.inputLayout;
	}

	GLPushBuffer *CreatePushBuffer(int frame, GLuint target, size_t size, const char *tag) {
		GLPushBuffer *push = new GLPushBuffer(this, target, size, tag);
		RegisterPushBuffer(frame, push);
		return push;
	}

	void DeleteShader(GLRShader *shader) {
		_dbg_assert_(shader != nullptr);
		deleter_.shaders.push_back(shader);
	}
	void DeleteProgram(GLRProgram *program) {
		_dbg_assert_(program != nullptr);
		deleter_.programs.push_back(program);
	}
	void DeleteBuffer(GLRBuffer *buffer) {
		_dbg_assert_(buffer != nullptr);
		deleter_.buffers.push_back(buffer);
	}
	void DeleteTexture(GLRTexture *texture) {
		_dbg_assert_(texture != nullptr);
		deleter_.textures.push_back(texture);
	}
	void DeleteInputLayout(GLRInputLayout *inputLayout) {
		_dbg_assert_(inputLayout != nullptr);
		deleter_.inputLayouts.push_back(inputLayout);
	}
	void DeleteFramebuffer(GLRFramebuffer *framebuffer) {
		_dbg_assert_(framebuffer != nullptr);
		deleter_.framebuffers.push_back(framebuffer);
	}
	void DeletePushBuffer(GLPushBuffer *pushbuffer) {
		_dbg_assert_(pushbuffer != nullptr);
		deleter_.pushBuffers.push_back(pushbuffer);
	}

	bool IsInRenderPass() const {
		return curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER;
	}

	// This starts a new step (like a "render pass" in Vulkan).
	//
	// After a "CopyFramebuffer" or the other functions that start "steps", you need to call this before
	// making any new render state changes or draw calls.
	//
	// The following state needs to be reset by the caller after calling this (and will thus not safely carry over from
	// the previous one):
	//   * Viewport/Scissor
	//   * Depth/stencil
	//   * Blend
	//   * Raster state like primitive, culling, etc.
	//
	// It can be useful to use GetCurrentStepId() to figure out when you need to send all this state again, if you're
	// not keeping track of your calls to this function on your own.
	void BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, GLRRenderPassAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil, const char *tag);

	// Binds a framebuffer as a texture, for the following draws.
	void BindFramebufferAsTexture(GLRFramebuffer *fb, int binding, int aspectBit);

	bool CopyFramebufferToMemory(GLRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, Draw::ReadbackMode mode, const char *tag);
	void CopyImageToMemorySync(GLRTexture *texture, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, const char *tag);

	void CopyFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLOffset2D dstPos, int aspectMask, const char *tag);
	void BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter, const char *tag);

	// Takes ownership of data if deleteData = true.
	void BufferSubdata(GLRBuffer *buffer, size_t offset, size_t size, uint8_t *data, bool deleteData = true) {
		// TODO: Maybe should be a render command instead of an init command? When possible it's better as
		// an init command, that's for sure.
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::BUFFER_SUBDATA;
		_dbg_assert_(offset <= buffer->size_ - size);
		step.buffer_subdata.buffer = buffer;
		step.buffer_subdata.offset = (int)offset;
		step.buffer_subdata.size = (int)size;
		step.buffer_subdata.data = data;
		step.buffer_subdata.deleteData = deleteData;
	}

	// Takes ownership over the data pointer and delete[]-s it.
	void TextureImage(GLRTexture *texture, int level, int width, int height, int depth, Draw::DataFormat format, uint8_t *data, GLRAllocType allocType = GLRAllocType::NEW, bool linearFilter = false) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::TEXTURE_IMAGE;
		step.texture_image.texture = texture;
		step.texture_image.data = data;
		step.texture_image.format = format;
		step.texture_image.level = level;
		step.texture_image.width = width;
		step.texture_image.height = height;
		step.texture_image.depth = depth;
		step.texture_image.allocType = allocType;
		step.texture_image.linearFilter = linearFilter;
	}

	void TextureSubImage(int slot, GLRTexture *texture, int level, int x, int y, int width, int height, Draw::DataFormat format, uint8_t *data, GLRAllocType allocType = GLRAllocType::NEW) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData _data(GLRRenderCommand::TEXTURE_SUBIMAGE);
		_data.texture_subimage.texture = texture;
		_data.texture_subimage.data = data;
		_data.texture_subimage.format = format;
		_data.texture_subimage.level = level;
		_data.texture_subimage.x = x;
		_data.texture_subimage.y = y;
		_data.texture_subimage.width = width;
		_data.texture_subimage.height = height;
		_data.texture_subimage.allocType = allocType;
		_data.texture_subimage.slot = slot;
		curRenderStep_->commands.push_back(_data);
	}

	void FinalizeTexture(GLRTexture *texture, int loadedLevels, bool genMips) {
		GLRInitStep &step = initSteps_.push_uninitialized();
		step.stepType = GLRInitStepType::TEXTURE_FINALIZE;
		step.texture_finalize.texture = texture;
		step.texture_finalize.loadedLevels = loadedLevels;
		step.texture_finalize.genMips = genMips;
	}

	void BindTexture(int slot, GLRTexture *tex) {
		if (!curRenderStep_ && !tex) {
			// Likely a pre-emptive bindtexture for D3D11 to avoid hazards. Not necessary.
			// This can happen in BlitUsingRaster.
			return;
		}
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		_dbg_assert_(slot < MAX_GL_TEXTURE_SLOTS);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::BINDTEXTURE;
		data.texture.slot = slot;
		data.texture.texture = tex;
	}

	void BindProgram(GLRProgram *program) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::BINDPROGRAM;
		_dbg_assert_(program != nullptr);
		data.program.program = program;
#ifdef _DEBUG
		curProgram_ = program;
#endif
	}

	void SetDepth(bool enabled, bool write, GLenum func) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::DEPTH;
		data.depth.enabled = enabled;
		data.depth.write = write;
		data.depth.func = func;
	}

	void SetViewport(const GLRViewport &vp) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::VIEWPORT;
		data.viewport.vp = vp;
	}

	void SetScissor(const GLRect2D &rc) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::SCISSOR;
		data.scissor.rc = rc;
	}

	void SetUniformI(const GLint *loc, int count, const int *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4I;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(int) * count);
	}

	void SetUniformI1(const GLint *loc, int udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4I;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = 1;
		memcpy(data.uniform4.v, &udata, sizeof(udata));
	}

	void SetUniformUI(const GLint *loc, int count, const uint32_t *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4UI;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(uint32_t) * count);
	}

	void SetUniformUI1(const GLint *loc, uint32_t udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4UI;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = 1;
		memcpy(data.uniform4.v, &udata, sizeof(udata));
	}

	void SetUniformF(const GLint *loc, int count, const float *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4F;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(float) * count);
	}

	void SetUniformF1(const GLint *loc, const float udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4F;
		data.uniform4.name = nullptr;
		data.uniform4.loc = loc;
		data.uniform4.count = 1;
		memcpy(data.uniform4.v, &udata, sizeof(float));
	}

	void SetUniformF(const char *name, int count, const float *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORM4F;
		data.uniform4.name = name;
		data.uniform4.loc = nullptr;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(float) * count);
	}

	void SetUniformM4x4(const GLint *loc, const float *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORMMATRIX;
		data.uniformMatrix4.name = nullptr;
		data.uniformMatrix4.loc = loc;
		memcpy(data.uniformMatrix4.m, udata, sizeof(float) * 16);
	}

	void SetUniformM4x4Stereo(const char *name, const GLint *loc, const float *left, const float *right) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORMSTEREOMATRIX;
		data.uniformStereoMatrix4.name = name;
		data.uniformStereoMatrix4.loc = loc;
		data.uniformStereoMatrix4.mData = new float[32];
		memcpy(&data.uniformStereoMatrix4.mData[0], left, sizeof(float) * 16);
		memcpy(&data.uniformStereoMatrix4.mData[16], right, sizeof(float) * 16);
	}

	void SetUniformM4x4(const char *name, const float *udata) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		_dbg_assert_(curProgram_);
#endif
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::UNIFORMMATRIX;
		data.uniformMatrix4.name = name;
		data.uniformMatrix4.loc = nullptr;
		memcpy(data.uniformMatrix4.m, udata, sizeof(float) * 16);
	}

	void SetBlendAndMask(int colorMask, bool blendEnabled, GLenum srcColor, GLenum dstColor, GLenum srcAlpha, GLenum dstAlpha, GLenum funcColor, GLenum funcAlpha) {
		// Make this one only a non-debug _assert_, since it often comes first.
		// Lets us collect info about this potential crash through assert extra data.
		_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::BLEND;
		data.blend.mask = colorMask;
		data.blend.enabled = blendEnabled;
		data.blend.srcColor = srcColor;
		data.blend.dstColor = dstColor;
		data.blend.srcAlpha = srcAlpha;
		data.blend.dstAlpha = dstAlpha;
		data.blend.funcColor = funcColor;
		data.blend.funcAlpha = funcAlpha;
	}

	void SetNoBlendAndMask(int colorMask) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::BLEND;
		data.blend.mask = colorMask;
		data.blend.enabled = false;
	}

#ifndef USING_GLES2
	void SetLogicOp(bool enabled, GLenum logicOp) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::LOGICOP;
		data.logic.enabled = enabled;
		data.logic.logicOp = logicOp;
	}
#endif

	void SetStencil(bool enabled, GLenum func, uint8_t refValue, uint8_t compareMask, uint8_t writeMask, GLenum sFail, GLenum zFail, GLenum pass) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::STENCIL;
		data.stencil.enabled = enabled;
		data.stencil.func = func;
		data.stencil.ref = refValue;
		data.stencil.compareMask = compareMask;
		data.stencil.writeMask = writeMask;
		data.stencil.sFail = sFail;
		data.stencil.zFail = zFail;
		data.stencil.pass = pass;
	}

	void SetStencilDisabled() {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::STENCIL;
		data.stencil.enabled = false;
	}

	void SetBlendFactor(const float color[4]) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::BLENDCOLOR;
		CopyFloat4(data.blendColor.color, color);
	}

	void SetRaster(GLboolean cullEnable, GLenum frontFace, GLenum cullFace, GLboolean ditherEnable, GLboolean depthClamp) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::RASTER;
		data.raster.cullEnable = cullEnable;
		data.raster.frontFace = frontFace;
		data.raster.cullFace = cullFace;
		data.raster.ditherEnable = ditherEnable;
		data.raster.depthClampEnable = depthClamp;
	}
	
	// Modifies the current texture as per GL specs, not global state.
	void SetTextureSampler(int slot, GLenum wrapS, GLenum wrapT, GLenum magFilter, GLenum minFilter, float anisotropy) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		_dbg_assert_(slot < MAX_GL_TEXTURE_SLOTS);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::TEXTURESAMPLER;
		data.textureSampler.slot = slot;
		data.textureSampler.wrapS = wrapS;
		data.textureSampler.wrapT = wrapT;
		data.textureSampler.magFilter = magFilter;
		data.textureSampler.minFilter = minFilter;
		data.textureSampler.anisotropy = anisotropy;
	}

	void SetTextureLod(int slot, float minLod, float maxLod, float lodBias) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		_dbg_assert_(slot < MAX_GL_TEXTURE_SLOTS);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::TEXTURELOD;
		data.textureLod.slot = slot;
		data.textureLod.minLod = minLod;
		data.textureLod.maxLod = maxLod;
		data.textureLod.lodBias = lodBias;
	}

	// If scissorW == 0, no scissor is applied (the whole render target is cleared).
	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask, int colorMask, int scissorX, int scissorY, int scissorW, int scissorH) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		if (!clearMask)
			return;
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::CLEAR;
		data.clear.clearMask = clearMask;
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
		data.clear.colorMask = colorMask;
		data.clear.scissorX = scissorX;
		data.clear.scissorY = scissorY;
		data.clear.scissorW = scissorW;
		data.clear.scissorH = scissorH;
	}

	void Draw(GLRInputLayout *inputLayout, GLRBuffer *vertexBuffer, uint32_t vertexOffset, GLenum mode, int first, int count) {
		_dbg_assert_(vertexBuffer && curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::DRAW;
		data.draw.inputLayout = inputLayout;
		data.draw.vertexOffset = vertexOffset;
		data.draw.vertexBuffer = vertexBuffer;
		data.draw.indexBuffer = nullptr;
		data.draw.mode = mode;
		data.draw.first = first;
		data.draw.count = count;
		data.draw.indexType = 0;
	}

	// Would really love to have a basevertex parameter, but impossible in unextended GLES, without glDrawElementsBaseVertex, unfortunately.
	void DrawIndexed(GLRInputLayout *inputLayout, GLRBuffer *vertexBuffer, uint32_t vertexOffset, GLRBuffer *indexBuffer, uint32_t indexOffset, GLenum mode, int count, GLenum indexType, int instances = 1) {
		_dbg_assert_(vertexBuffer && indexBuffer && curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = GLRRenderCommand::DRAW;
		data.draw.inputLayout = inputLayout;
		data.draw.vertexOffset = vertexOffset;
		data.draw.vertexBuffer = vertexBuffer;
		data.draw.indexBuffer = indexBuffer;
		data.draw.indexOffset = indexOffset;
		data.draw.mode = mode;
		data.draw.count = count;
		data.draw.indexType = indexType;
		data.draw.instances = instances;
	}

	enum { MAX_INFLIGHT_FRAMES = 3 };

	void SetInflightFrames(int f) {
		newInflightFrames_ = f < 1 || f > MAX_INFLIGHT_FRAMES ? MAX_INFLIGHT_FRAMES : f;
	}

	int GetCurFrame() const {
		return curFrame_;
	}

	void Resize(int width, int height) {
		targetWidth_ = width;
		targetHeight_ = height;
		queueRunner_.Resize(width, height);
	}

	void UnregisterPushBuffer(GLPushBuffer *buffer) {
		int foundCount = 0;
		for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
			auto iter = frameData_[i].activePushBuffers.find(buffer);
			if (iter != frameData_[i].activePushBuffers.end()) {
				frameData_[i].activePushBuffers.erase(iter);
				foundCount++;
			}
		}
		_dbg_assert_(foundCount == 1);
	}

	void SetSwapFunction(std::function<void()> swapFunction) {
		swapFunction_ = swapFunction;
	}

	void SetSwapIntervalFunction(std::function<void(int)> swapIntervalFunction) {
		swapIntervalFunction_ = swapIntervalFunction;
	}

	void SwapInterval(int interval) {
		if (interval != swapInterval_) {
			swapInterval_ = interval;
			swapIntervalChanged_ = true;
		}
	}

	void StartThread();  // Currently only used on iOS, since we fully recreate the context on Android
	void StopThread();

	bool SawOutOfMemory() {
		return queueRunner_.SawOutOfMemory();
	}

	// Only supports a common subset.
	std::string GetGLString(int name) const {
		return queueRunner_.GetGLString(name);
	}

	// Used during Android-style ugly shutdown. No need to have a way to set it back because we'll be
	// destroyed.
	void SetSkipGLCalls() {
		skipGLCalls_ = true;
	}

private:
	bool Run(GLRRenderThreadTask &task);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void FlushSync();

	// When using legacy functionality for push buffers (glBufferData), we need to flush them
	// before actually making the glDraw* calls. It's best if the render manager handles that.
	void RegisterPushBuffer(int frame, GLPushBuffer *buffer) {
		frameData_[frame].activePushBuffers.insert(buffer);
	}

	GLFrameData frameData_[MAX_INFLIGHT_FRAMES];

	// Submission time state
	bool insideFrame_ = false;

	GLRStep *curRenderStep_ = nullptr;
	std::vector<GLRStep *> steps_;
	FastVec<GLRInitStep> initSteps_;

	// Execution time state
	// TODO: Rename this, as we don't actually use a compile thread on OpenGL.
	bool runCompileThread_ = true;

	// Thread is managed elsewhere, and should call ThreadFrame.
	GLQueueRunner queueRunner_;

	// For pushing data on the queue.
	std::mutex pushMutex_;
	std::condition_variable pushCondVar_;

	std::queue<GLRRenderThreadTask *> renderThreadQueue_;

	// For readbacks and other reasons we need to sync with the render thread.
	std::mutex syncMutex_;
	std::condition_variable syncCondVar_;

	bool firstFrame_ = true;
	bool vrRenderStarted_ = false;
	bool syncDone_ = false;

	GLDeleter deleter_;
	bool skipGLCalls_ = false;

	int curFrame_ = 0;

	std::function<void()> swapFunction_;
	std::function<void(int)> swapIntervalFunction_;
	GLBufferStrategy bufferStrategy_ = GLBufferStrategy::SUBDATA;

	int inflightFrames_ = MAX_INFLIGHT_FRAMES;
	int newInflightFrames_ = -1;

	int swapInterval_ = 0;
	bool swapIntervalChanged_ = true;

	int targetWidth_ = 0;
	int targetHeight_ = 0;

#ifdef _DEBUG
	GLRProgram *curProgram_ = nullptr;
#endif
	Draw::DeviceCaps caps_{};

	std::string profilePassesString_;
	InvalidationCallback invalidationCallback_;

	uint64_t frameIdGen_ = FRAME_TIME_HISTORY_LENGTH;
	HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory_;
};
