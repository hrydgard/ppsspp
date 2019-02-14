#pragma once

#include <thread>
#include <unordered_map>
#include <vector>
#include <functional>
#include <set>
#include <string>
#include <mutex>
#include <condition_variable>
#include <cassert>

#include "base/logging.h"
#include "gfx/gl_common.h"
#include "math/dataconv.h"
#include "Common/Log.h"
#include "GLQueueRunner.h"

class GLRInputLayout;
class GLPushBuffer;

namespace Draw {
class DrawContext;
}

class GLRTexture {
public:
	~GLRTexture() {
		if (texture) {
			glDeleteTextures(1, &texture);
		}
	}
	GLuint texture = 0;
	// Could also trust OpenGL defaults I guess..
	GLenum target = 0xFFFF;
	GLenum wrapS = 0xFFFF;
	GLenum wrapT = 0xFFFF;
	GLenum magFilter = 0xFFFF;
	GLenum minFilter = 0xFFFF;
	bool canWrap = true;
	float anisotropy = -100000.0f;
	float minLod = -1000.0f;
	float maxLod = 1000.0f;
	float lodBias = 0.0f;
};

class GLRFramebuffer {
public:
	GLRFramebuffer(int _width, int _height, bool z_stencil)
		: width(_width), height(_height), z_stencil_(z_stencil) {
	}

	~GLRFramebuffer();

	int numShadows = 1;  // TODO: Support this.

	GLuint handle = 0;
	GLRTexture color_texture;
	// Either z_stencil_texture, z_stencil_buffer, or (z_buffer and stencil_buffer) are set.
	GLuint z_stencil_buffer = 0;
	GLRTexture z_stencil_texture;
	GLuint z_buffer = 0;
	GLuint stencil_buffer = 0;

	int width;
	int height;
	GLuint colorDepth;

	bool z_stencil_;
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

class GLRProgram {
public:
	~GLRProgram() {
		if (program) {
			glDeleteProgram(program);
		}
	}
	struct Semantic {
		int location;
		const char *attrib;
	};

	struct UniformLocQuery {
		GLint *dest;
		const char *name;
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
	std::unordered_map<std::string, UniformInfo> uniformCache_;
};

enum class GLBufferStrategy {
	SUBDATA = 0,

	MASK_FLUSH = 0x10,
	MASK_INVALIDATE = 0x20,

	// Map/unmap the buffer each frame.
	FRAME_UNMAP = 1,
	// Map/unmap and also invalidate the buffer on map.
	INVALIDATE_UNMAP = MASK_INVALIDATE,
	// Map/unmap and explicitly flushed changed ranges.
	FLUSH_UNMAP = MASK_FLUSH,
	// Map/unmap, invalidate on map, and explicit flush.
	FLUSH_INVALIDATE_UNMAP = MASK_FLUSH | MASK_INVALIDATE,
};

static inline int operator &(const GLBufferStrategy &lhs, const GLBufferStrategy &rhs) {
	return (int)lhs & (int)rhs;
}

class GLRBuffer {
public:
	GLRBuffer(GLuint target, size_t size) : target_(target), size_((int)size) {}
	~GLRBuffer() {
		if (buffer_) {
			glDeleteBuffers(1, &buffer_);
		}
	}

	void *Map(GLBufferStrategy strategy);
	bool Unmap();

	bool Mapped() const {
		return mapped_;
	}

	GLuint buffer_ = 0;
	GLuint target_;
	int size_;

private:
	bool mapped_ = false;
	bool hasStorage_ = false;
};

class GLRenderManager;

// Similar to VulkanPushBuffer but is currently less efficient - it collects all the data in
// RAM then does a big memcpy/buffer upload at the end of the frame. This is at least a lot
// faster than the hundreds of buffer uploads or memory array buffers we used before.
// On modern GL we could avoid the copy using glBufferStorage but not sure it's worth the
// trouble.
// We need to manage the lifetime of this together with the other resources so its destructor
// runs on the render thread.
class GLPushBuffer {
public:
	friend class GLRenderManager;

	struct BufInfo {
		GLRBuffer *buffer = nullptr;
		uint8_t *localMemory = nullptr;
		uint8_t *deviceMemory = nullptr;
		size_t flushOffset = 0;
	};

	GLPushBuffer(GLRenderManager *render, GLuint target, size_t size);
	~GLPushBuffer();

	void Reset() { offset_ = 0; }

private:
	// Needs context in case of defragment.
	void Begin() {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment();
		Map();
		assert(writePtr_);
	}

	void BeginNoReset() {
		Map();
	}

	void End() {
		Unmap();
	}

public:
	void Map();
	void Unmap();

	// When using the returned memory, make sure to bind the returned vkbuf.
	// This will later allow for handling overflow correctly.
	size_t Allocate(size_t numBytes, GLRBuffer **vkbuf) {
		size_t out = offset_;
		if (offset_ + ((numBytes + 3) & ~3) >= size_) {
			NextBuffer(numBytes);
			out = offset_;
			offset_ += (numBytes + 3) & ~3;
		} else {
			offset_ += (numBytes + 3) & ~3;  // Round up to 4 bytes.
		}
		*vkbuf = buffers_[buf_].buffer;
		return out;
	}

	// Returns the offset that should be used when binding this buffer to get this data.
	size_t Push(const void *data, size_t size, GLRBuffer **vkbuf) {
		assert(writePtr_);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align, GLRBuffer **vkbuf) {
		assert(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return (uint32_t)off;
	}

	size_t GetOffset() const {
		return offset_;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	// Recommended.
	void *Push(size_t size, uint32_t *bindOffset, GLRBuffer **vkbuf) {
		assert(writePtr_);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}
	void *PushAligned(size_t size, uint32_t *bindOffset, GLRBuffer **vkbuf, int align) {
		assert(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}

	size_t GetTotalSize() const;

	void Destroy(bool onRenderThread);
	void Flush();

protected:
	void MapDevice(GLBufferStrategy strategy);
	void UnmapDevice();

private:
	bool AddBuffer();
	void NextBuffer(size_t minSize);
	void Defragment();

	GLRenderManager *render_;
	std::vector<BufInfo> buffers_;
	size_t buf_ = 0;
	size_t offset_ = 0;
	size_t size_ = 0;
	uint8_t *writePtr_ = nullptr;
	GLuint target_;
	GLBufferStrategy strategy_ = GLBufferStrategy::SUBDATA;
};

enum class GLRRunType {
	END,
	SYNC,
};

class GLDeleter {
public:
	void Perform(GLRenderManager *renderManager, bool skipGLCalls);

	bool IsEmpty() const {
		return shaders.empty() && programs.empty() && buffers.empty() && textures.empty() && inputLayouts.empty() && framebuffers.empty() && pushBuffers.empty();
	}

	void Take(GLDeleter &other) {
		_assert_msg_(G3D, IsEmpty(), "Deleter already has stuff");
		shaders = std::move(other.shaders);
		programs = std::move(other.programs);
		buffers = std::move(other.buffers);
		textures = std::move(other.textures);
		inputLayouts = std::move(other.inputLayouts);
		framebuffers = std::move(other.framebuffers);
		pushBuffers = std::move(other.pushBuffers);
		other.shaders.clear();
		other.programs.clear();
		other.buffers.clear();
		other.textures.clear();
		other.inputLayouts.clear();
		other.framebuffers.clear();
		other.pushBuffers.clear();
	}

	std::vector<GLRShader *> shaders;
	std::vector<GLRProgram *> programs;
	std::vector<GLRBuffer *> buffers;
	std::vector<GLRTexture *> textures;
	std::vector<GLRInputLayout *> inputLayouts;
	std::vector<GLRFramebuffer *> framebuffers;
	std::vector<GLPushBuffer *> pushBuffers;
};

class GLRInputLayout {
public:
	struct Entry {
		int location;
		int count;
		GLenum type;
		GLboolean normalized;
		int stride;
		intptr_t offset;
	};
	std::vector<Entry> entries;
	int semanticsMask_ = 0;
};

// Note: The GLRenderManager is created and destroyed on the render thread, and the latter
// happens after the emu thread has been destroyed. Therefore, it's safe to run wild deleting stuff
// directly in the destructor.
class GLRenderManager {
public:
	GLRenderManager();
	~GLRenderManager();

	void ThreadStart(Draw::DrawContext *draw);
	void ThreadEnd();
	bool ThreadFrame();  // Returns false to request exiting the loop.

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame();
	// Can run on a different thread!
	void Finish();
	void Run(int frame);

	// Zaps queued up commands. Use if you know there's a risk you've queued up stuff that has already been deleted. Can happen during in-game shutdown.
	void Wipe();

	// Wait until no frames are pending.  Use during shutdown before freeing pointers.
	void WaitUntilQueueIdle();

	// Creation commands. These were not needed in Vulkan since there we can do that on the main thread.
	GLRTexture *CreateTexture(GLenum target) {
		GLRInitStep step{ GLRInitStepType::CREATE_TEXTURE };
		step.create_texture.texture = new GLRTexture();
		step.create_texture.texture->target = target;
		initSteps_.push_back(step);
		return step.create_texture.texture;
	}

	GLRBuffer *CreateBuffer(GLuint target, size_t size, GLuint usage) {
		GLRInitStep step{ GLRInitStepType::CREATE_BUFFER };
		step.create_buffer.buffer = new GLRBuffer(target, size);
		step.create_buffer.size = (int)size;
		step.create_buffer.usage = usage;
		initSteps_.push_back(step);
		return step.create_buffer.buffer;
	}

	GLRShader *CreateShader(GLuint stage, const std::string &code, const std::string &desc) {
		GLRInitStep step{ GLRInitStepType::CREATE_SHADER };
		step.create_shader.shader = new GLRShader();
		step.create_shader.shader->desc = desc;
		step.create_shader.stage = stage;
		step.create_shader.code = new char[code.size() + 1];
		memcpy(step.create_shader.code, code.data(), code.size() + 1);
		initSteps_.push_back(step);
		return step.create_shader.shader;
	}

	GLRFramebuffer *CreateFramebuffer(int width, int height, bool z_stencil) {
		GLRInitStep step{ GLRInitStepType::CREATE_FRAMEBUFFER };
		step.create_framebuffer.framebuffer = new GLRFramebuffer(width, height, z_stencil);
		initSteps_.push_back(step);
		return step.create_framebuffer.framebuffer;
	}

	// Can't replace uniform initializers with direct calls to SetUniform() etc because there might
	// not be an active render pass.
	GLRProgram *CreateProgram(
		std::vector<GLRShader *> shaders, std::vector<GLRProgram::Semantic> semantics, std::vector<GLRProgram::UniformLocQuery> queries,
		std::vector<GLRProgram::Initializer> initalizers, bool supportDualSource) {
		GLRInitStep step{ GLRInitStepType::CREATE_PROGRAM };
		assert(shaders.size() <= ARRAY_SIZE(step.create_program.shaders));
		step.create_program.program = new GLRProgram();
		step.create_program.program->semantics_ = semantics;
		step.create_program.program->queries_ = queries;
		step.create_program.program->initialize_ = initalizers;
		step.create_program.support_dual_source = supportDualSource;
		_assert_msg_(G3D, shaders.size() > 0, "Can't create a program with zero shaders");
		for (size_t i = 0; i < shaders.size(); i++) {
			step.create_program.shaders[i] = shaders[i];
		}
#ifdef _DEBUG
		for (auto &iter : queries) {
			assert(iter.name);
		}
		for (auto &sem : semantics) {
			assert(sem.attrib);
		}
#endif
		step.create_program.num_shaders = (int)shaders.size();
		initSteps_.push_back(step);
		return step.create_program.program;
	}

	GLRInputLayout *CreateInputLayout(std::vector<GLRInputLayout::Entry> &entries) {
		GLRInitStep step{ GLRInitStepType::CREATE_INPUT_LAYOUT };
		step.create_input_layout.inputLayout = new GLRInputLayout();
		step.create_input_layout.inputLayout->entries = entries;
		for (auto &iter : step.create_input_layout.inputLayout->entries) {
			step.create_input_layout.inputLayout->semanticsMask_ |= 1 << iter.location;
		}
		initSteps_.push_back(step);
		return step.create_input_layout.inputLayout;
	}

	GLPushBuffer *CreatePushBuffer(int frame, GLuint target, size_t size) {
		GLPushBuffer *push = new GLPushBuffer(this, target, size);
		RegisterPushBuffer(frame, push);
		return push;
	}

	void DeleteShader(GLRShader *shader) {
		deleter_.shaders.push_back(shader);
	}
	void DeleteProgram(GLRProgram *program) {
		deleter_.programs.push_back(program);
	}
	void DeleteBuffer(GLRBuffer *buffer) {
		deleter_.buffers.push_back(buffer);
	}
	void DeleteTexture(GLRTexture *texture) {
		deleter_.textures.push_back(texture);
	}
	void DeleteInputLayout(GLRInputLayout *inputLayout) {
		deleter_.inputLayouts.push_back(inputLayout);
	}
	void DeleteFramebuffer(GLRFramebuffer *framebuffer) {
		deleter_.framebuffers.push_back(framebuffer);
	}
	void DeletePushBuffer(GLPushBuffer *pushbuffer) {
		deleter_.pushBuffers.push_back(pushbuffer);
	}

	void BeginPushBuffer(GLPushBuffer *pushbuffer) {
		pushbuffer->Begin();
	}

	void EndPushBuffer(GLPushBuffer *pushbuffer) {
		pushbuffer->End();
	}

	void BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, GLRRenderPassAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil);
	void BindFramebufferAsTexture(GLRFramebuffer *fb, int binding, int aspectBit, int attachment);
	bool CopyFramebufferToMemorySync(GLRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);
	void CopyImageToMemorySync(GLRTexture *texture, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);

	void CopyFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLOffset2D dstPos, int aspectMask);
	void BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter);

	// Takes ownership of data if deleteData = true.
	void BufferSubdata(GLRBuffer *buffer, size_t offset, size_t size, uint8_t *data, bool deleteData = true) {
		// TODO: Maybe should be a render command instead of an init command? When possible it's better as
		// an init command, that's for sure.
		GLRInitStep step{ GLRInitStepType::BUFFER_SUBDATA };
		_dbg_assert_(G3D, offset >= 0);
		_dbg_assert_(G3D, offset <= buffer->size_ - size);
		step.buffer_subdata.buffer = buffer;
		step.buffer_subdata.offset = (int)offset;
		step.buffer_subdata.size = (int)size;
		step.buffer_subdata.data = data;
		step.buffer_subdata.deleteData = deleteData;
		initSteps_.push_back(step);
	}

	// Takes ownership over the data pointer and delete[]-s it.
	void TextureImage(GLRTexture *texture, int level, int width, int height, GLenum internalFormat, GLenum format, GLenum type, uint8_t *data, GLRAllocType allocType = GLRAllocType::NEW, bool linearFilter = false) {
		GLRInitStep step{ GLRInitStepType::TEXTURE_IMAGE };
		step.texture_image.texture = texture;
		step.texture_image.data = data;
		step.texture_image.internalFormat = internalFormat;
		step.texture_image.format = format;
		step.texture_image.type = type;
		step.texture_image.level = level;
		step.texture_image.width = width;
		step.texture_image.height = height;
		step.texture_image.allocType = allocType;
		step.texture_image.linearFilter = linearFilter;
		initSteps_.push_back(step);
	}

	void TextureSubImage(GLRTexture *texture, int level, int x, int y, int width, int height, GLenum format, GLenum type, uint8_t *data, GLRAllocType allocType = GLRAllocType::NEW) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData _data{ GLRRenderCommand::TEXTURE_SUBIMAGE };
		_data.texture_subimage.texture = texture;
		_data.texture_subimage.data = data;
		_data.texture_subimage.format = format;
		_data.texture_subimage.type = type;
		_data.texture_subimage.level = level;
		_data.texture_subimage.x = x;
		_data.texture_subimage.y = y;
		_data.texture_subimage.width = width;
		_data.texture_subimage.height = height;
		_data.texture_subimage.allocType = allocType;
		curRenderStep_->commands.push_back(_data);
	}

	void FinalizeTexture(GLRTexture *texture, int maxLevels, bool genMips) {
		GLRInitStep step{ GLRInitStepType::TEXTURE_FINALIZE };
		step.texture_finalize.texture = texture;
		step.texture_finalize.maxLevel = maxLevels;
		step.texture_finalize.genMips = genMips;
		initSteps_.push_back(step);
	}

	void BindTexture(int slot, GLRTexture *tex) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BINDTEXTURE };
		_dbg_assert_(G3D, slot < 16);
		data.texture.slot = slot;
		data.texture.texture = tex;
		curRenderStep_->commands.push_back(data);
	}

	void BindProgram(GLRProgram *program) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BINDPROGRAM };
		_dbg_assert_(G3D, program != nullptr);
		data.program.program = program;
		curRenderStep_->commands.push_back(data);
#ifdef _DEBUG
		curProgram_ = program;
#endif
	}

	void BindPixelPackBuffer(GLRBuffer *buffer) {  // Want to support an offset but can't in ES 2.0. We supply an offset when binding the buffers instead.
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BIND_BUFFER };
		data.bind_buffer.buffer = buffer;
		data.bind_buffer.target = GL_PIXEL_PACK_BUFFER;
		curRenderStep_->commands.push_back(data);
	}

	void BindIndexBuffer(GLRBuffer *buffer) {  // Want to support an offset but can't in ES 2.0. We supply an offset when binding the buffers instead.
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BIND_BUFFER};
		data.bind_buffer.buffer = buffer;
		data.bind_buffer.target = GL_ELEMENT_ARRAY_BUFFER;
		curRenderStep_->commands.push_back(data);
	}

	void BindVertexBuffer(GLRInputLayout *inputLayout, GLRBuffer *buffer, size_t offset) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		assert(inputLayout);
		GLRRenderData data{ GLRRenderCommand::BIND_VERTEX_BUFFER };
		data.bindVertexBuffer.inputLayout = inputLayout;
		data.bindVertexBuffer.offset = offset;
		data.bindVertexBuffer.buffer = buffer;
		curRenderStep_->commands.push_back(data);
	}

	void SetDepth(bool enabled, bool write, GLenum func) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::DEPTH };
		data.depth.enabled = enabled;
		data.depth.write = write;
		data.depth.func = func;
		curRenderStep_->commands.push_back(data);
	}

	void SetViewport(const GLRViewport &vp) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::VIEWPORT };
		data.viewport.vp = vp;
		curRenderStep_->commands.push_back(data);
	}

	void SetScissor(const GLRect2D &rc) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::SCISSOR };
		data.scissor.rc = rc;
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformI(GLint *loc, int count, const int *udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORM4I };
		data.uniform4.loc = loc;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(int) * count);
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformI1(GLint *loc, int udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORM4I };
		data.uniform4.loc = loc;
		data.uniform4.count = 1;
		memcpy(data.uniform4.v, &udata, sizeof(udata));
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformF(GLint *loc, int count, const float *udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORM4F };
		data.uniform4.loc = loc;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(float) * count);
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformF1(GLint *loc, const float udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORM4F };
		data.uniform4.loc = loc;
		data.uniform4.count = 1;
		memcpy(data.uniform4.v, &udata, sizeof(float));
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformF(const char *name, int count, const float *udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORM4F };
		data.uniform4.name = name;
		data.uniform4.count = count;
		memcpy(data.uniform4.v, udata, sizeof(float) * count);
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformM4x4(GLint *loc, const float *udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORMMATRIX };
		data.uniformMatrix4.loc = loc;
		memcpy(data.uniformMatrix4.m, udata, sizeof(float) * 16);
		curRenderStep_->commands.push_back(data);
	}

	void SetUniformM4x4(const char *name, const float *udata) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
#ifdef _DEBUG
		assert(curProgram_);
#endif
		GLRRenderData data{ GLRRenderCommand::UNIFORMMATRIX };
		data.uniformMatrix4.name = name;
		memcpy(data.uniformMatrix4.m, udata, sizeof(float) * 16);
		curRenderStep_->commands.push_back(data);
	}

	void SetBlendAndMask(int colorMask, bool blendEnabled, GLenum srcColor, GLenum dstColor, GLenum srcAlpha, GLenum dstAlpha, GLenum funcColor, GLenum funcAlpha) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BLEND };
		data.blend.mask = colorMask;
		data.blend.enabled = blendEnabled;
		data.blend.srcColor = srcColor;
		data.blend.dstColor = dstColor;
		data.blend.srcAlpha = srcAlpha;
		data.blend.dstAlpha = dstAlpha;
		data.blend.funcColor = funcColor;
		data.blend.funcAlpha = funcAlpha;
		curRenderStep_->commands.push_back(data);
	}

	void SetNoBlendAndMask(int colorMask) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BLEND };
		data.blend.mask = colorMask;
		data.blend.enabled = false;
		curRenderStep_->commands.push_back(data);
	}

#ifndef USING_GLES2
	void SetLogicOp(bool enabled, GLenum logicOp) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::LOGICOP };
		data.logic.enabled = enabled;
		data.logic.logicOp = logicOp;
		curRenderStep_->commands.push_back(data);
	}
#endif

	void SetStencilFunc(bool enabled, GLenum func, uint8_t refValue, uint8_t compareMask) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::STENCILFUNC };
		data.stencilFunc.enabled = enabled;
		data.stencilFunc.func = func;
		data.stencilFunc.ref = refValue;
		data.stencilFunc.compareMask = compareMask;
		curRenderStep_->commands.push_back(data);
	}

	void SetStencilOp(uint8_t writeMask, GLenum sFail, GLenum zFail, GLenum pass) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::STENCILOP };
		data.stencilOp.writeMask = writeMask;
		data.stencilOp.sFail = sFail;
		data.stencilOp.zFail = zFail;
		data.stencilOp.pass = pass;
		curRenderStep_->commands.push_back(data);
	}

	void SetStencilDisabled() {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data;
		data.cmd = GLRRenderCommand::STENCILFUNC;
		data.stencilFunc.enabled = false;
		curRenderStep_->commands.push_back(data);
	}

	void SetBlendFactor(const float color[4]) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BLENDCOLOR };
		CopyFloat4(data.blendColor.color, color);
		curRenderStep_->commands.push_back(data);
	}

	void SetRaster(GLboolean cullEnable, GLenum frontFace, GLenum cullFace, GLboolean ditherEnable) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::RASTER };
		data.raster.cullEnable = cullEnable;
		data.raster.frontFace = frontFace;
		data.raster.cullFace = cullFace;
		data.raster.ditherEnable = ditherEnable;
		curRenderStep_->commands.push_back(data);
	}
	
	// Modifies the current texture as per GL specs, not global state.
	void SetTextureSampler(int slot, GLenum wrapS, GLenum wrapT, GLenum magFilter, GLenum minFilter, float anisotropy) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::TEXTURESAMPLER };
		data.textureSampler.slot = slot;
		data.textureSampler.wrapS = wrapS;
		data.textureSampler.wrapT = wrapT;
		data.textureSampler.magFilter = magFilter;
		data.textureSampler.minFilter = minFilter;
		data.textureSampler.anisotropy = anisotropy;
		curRenderStep_->commands.push_back(data);
	}

	void SetTextureLod(int slot, float minLod, float maxLod, float lodBias) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::TEXTURELOD};
		data.textureLod.slot = slot;
		data.textureLod.minLod = minLod;
		data.textureLod.maxLod = maxLod;
		data.textureLod.lodBias = lodBias;
		curRenderStep_->commands.push_back(data);
	}

	// If scissorW == 0, no scissor is applied.
	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask, int colorMask, int scissorX, int scissorY, int scissorW, int scissorH) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		if (!clearMask)
			return;
		GLRRenderData data{ GLRRenderCommand::CLEAR };
		data.clear.clearMask = clearMask;
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
		data.clear.colorMask = colorMask;
		data.clear.scissorX = scissorX;
		data.clear.scissorY = scissorY;
		data.clear.scissorW = scissorW;
		data.clear.scissorH = scissorH;
		curRenderStep_->commands.push_back(data);
	}

	void Invalidate(int invalidateMask) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::INVALIDATE };
		data.clear.clearMask = invalidateMask;
		curRenderStep_->commands.push_back(data);
	}

	void Draw(GLenum mode, int first, int count) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::DRAW };
		data.draw.mode = mode;
		data.draw.first = first;
		data.draw.count = count;
		data.draw.buffer = 0;
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	void DrawIndexed(GLenum mode, int count, GLenum indexType, void *indices, int instances = 1) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::DRAW_INDEXED };
		data.drawIndexed.mode = mode;
		data.drawIndexed.count = count;
		data.drawIndexed.indexType = indexType;
		data.drawIndexed.instances = instances;
		data.drawIndexed.indices = indices;
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	enum { MAX_INFLIGHT_FRAMES = 3 };

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
		assert(foundCount == 1);
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
	void BeginSubmitFrame(int frame);
	void EndSubmitFrame(int frame);
	void Submit(int frame, bool triggerFence);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void FlushSync();
	void EndSyncFrame(int frame);

	// When using legacy functionality for push buffers (glBufferData), we need to flush them
	// before actually making the glDraw* calls. It's best if the render manager handles that.
	void RegisterPushBuffer(int frame, GLPushBuffer *buffer) {
		frameData_[frame].activePushBuffers.insert(buffer);
	}

	// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
	struct FrameData {
		std::mutex push_mutex;
		std::condition_variable push_condVar;

		std::mutex pull_mutex;
		std::condition_variable pull_condVar;

		bool readyForFence = true;
		bool readyForRun = false;
		bool readyForSubmit = false;
		bool skipSwap = false;
		GLRRunType type = GLRRunType::END;

		// GLuint fence; For future AZDO stuff?
		std::vector<GLRStep *> steps;
		std::vector<GLRInitStep> initSteps;

		// Swapchain.
		bool hasBegun = false;
		uint32_t curSwapchainImage = -1;

		GLDeleter deleter;
		GLDeleter deleter_prev;
		std::set<GLPushBuffer *> activePushBuffers;
	};

	FrameData frameData_[MAX_INFLIGHT_FRAMES];

	// Submission time state
	bool insideFrame_ = false;
	GLRStep *curRenderStep_ = nullptr;
	std::vector<GLRStep *> steps_;
	std::vector<GLRInitStep> initSteps_;

	// Execution time state
	bool run_ = true;
	// Thread is managed elsewhere, and should call ThreadFrame.
	std::mutex mutex_;
	int threadInitFrame_ = 0;
	GLQueueRunner queueRunner_;

	// Thread state
	int threadFrame_ = -1;

	bool nextFrame = false;
	bool firstFrame = true;

	GLDeleter deleter_;
	bool skipGLCalls_ = false;

	int curFrame_ = 0;

	std::function<void()> swapFunction_;
	std::function<void(int)> swapIntervalFunction_;
	GLBufferStrategy bufferStrategy_ = GLBufferStrategy::SUBDATA;

	int swapInterval_ = 0;
	bool swapIntervalChanged_ = true;

	int targetWidth_ = 0;
	int targetHeight_ = 0;

#ifdef _DEBUG
	GLRProgram *curProgram_ = nullptr;
#endif
};
