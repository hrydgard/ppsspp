#pragma once

#include <thread>
#include <mutex>

#include "gfx/gl_common.h"
#include "math/dataconv.h"
#include "Common/Log.h"
#include "GLQueueRunner.h"

struct GLRImage {
	GLuint texture;
	GLuint format;
};

void GLCreateImage(GLRImage &img, int width, int height, GLint format, bool color);

class GLRFramebuffer {
public:
	GLRFramebuffer(int _width, int _height) {
		width = _width;
		height = _height;

		/*
		CreateImage(vulkan_, initCmd, color, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
		CreateImage(vulkan_, initCmd, depth, width, height, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

		VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		VkImageView views[2]{};

		fbci.renderPass = renderPass;
		fbci.attachmentCount = 2;
		fbci.pAttachments = views;
		views[0] = color.imageView;
		views[1] = depth.imageView;
		fbci.width = width;
		fbci.height = height;
		fbci.layers = 1;

		vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &framebuf);*/
	}

	~GLRFramebuffer() {
		glDeleteTextures(1, &color.texture);
		glDeleteRenderbuffers(1, &depth.texture);
	}

	int numShadows = 1;  // TODO: Support this.

	GLuint framebuf = 0;
	GLRImage color{};
	GLRImage depth{};
	int width = 0;
	int height = 0;
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
};

class GLRProgram {
public:
	~GLRProgram() {
		if (program) {
			glDeleteProgram(program);
		}
	}
	GLuint program = 0;
};

class GLRTexture {
public:
	~GLRTexture() {
		if (texture) {
			glDeleteTextures(1, &texture);
		}
	}
	GLuint texture;
};

class GLRBuffer {
public:
	~GLRBuffer() {
		if (texture) {
			glDeleteTextures(1, &texture);
		}
	}
	GLuint texture;
};

enum class GLRRunType {
	END,
	SYNC,
};

class GLDeleter {
public:
	void Perform();

	void Take(GLDeleter &other) {
		shaders = std::move(other.shaders);
		programs = std::move(other.programs);
	}

	std::vector<GLRShader *> shaders;
	std::vector<GLRProgram *> programs;
};


class GLRenderManager {
public:
	GLRenderManager();
	~GLRenderManager();

	void ThreadFunc();

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame();
	// Can run on a different thread!
	void Finish();
	void Run(int frame);

	// Zaps queued up commands. Use if you know there's a risk you've queued up stuff that has already been deleted. Can happen during in-game shutdown.
	void Wipe();

	// Creation commands. These were not needed in Vulkan since there we can do that on the main thread.
	GLRTexture *CreateTexture(int w, int h) {
		GLRInitStep step{ GLRInitStepType::CREATE_TEXTURE };
		step.create_texture.texture = new GLRTexture();
		step.create_texture.width = w;
		step.create_texture.height = h;
		initSteps_.push_back(step);
		return step.create_texture.texture;
	}

	GLRShader *CreateShader(const char *code) {
		GLRInitStep step{ GLRInitStepType::CREATE_SHADER };
		step.create_shader.shader = new GLRShader();
		step.create_shader.code = code;
		initSteps_.push_back(step);
		return step.create_shader.shader;
	}

	GLRProgram *CreateProgram(GLRShader *vshader, GLRShader *fshader) {
		GLRInitStep step{ GLRInitStepType::CREATE_PROGRAM };
		step.create_program.program = new GLRProgram();
		step.create_program.vshader = vshader;
		step.create_program.fshader = fshader;
		initSteps_.push_back(step);
		return step.create_program.program;
	}

	void BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, uint32_t clearColor, float clearDepth, uint8_t clearStencil);
	GLuint BindFramebufferAsTexture(GLRFramebuffer *fb, int binding, int aspectBit, int attachment);
	bool CopyFramebufferToMemorySync(GLRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);
	void CopyImageToMemorySync(GLuint texture, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);

	void CopyFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLOffset2D dstPos, int aspectMask);
	void BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter);

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

	void SetStencil(bool enabled, uint8_t writeMask, uint8_t compareMask, uint8_t refValue) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::STENCIL };
		data.stencil.stencilWriteMask = writeMask;
		data.stencil.stencilCompareMask = compareMask;
		data.stencil.stencilRef = refValue;
		curRenderStep_->commands.push_back(data);
	}

	void SetBlendFactor(float color[4]) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::BLENDCOLOR };
		CopyFloat4(data.blendColor.color, color);
		curRenderStep_->commands.push_back(data);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::CLEAR };
		data.clear.clearMask = clearMask;
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
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

	void DrawIndexed(GLenum mode, int count, GLenum indexType, void *indices) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
		GLRRenderData data{ GLRRenderCommand::DRAW_INDEXED };
		data.drawIndexed.mode = mode;
		data.drawIndexed.count = count;
		data.drawIndexed.indexType = indexType;
		data.drawIndexed.instances = 1;
		data.drawIndexed.indices = indices;
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	enum { MAX_INFLIGHT_FRAMES = 3 };

private:
	void BeginSubmitFrame(int frame);
	void EndSubmitFrame(int frame);
	void Submit(int frame, bool triggerFence);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void FlushSync();
	void EndSyncFrame(int frame);

	void StopThread();

	int GetCurFrame() const {
		return curFrame_;
	}

	// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
	struct FrameData {
		std::mutex push_mutex;
		std::condition_variable push_condVar;

		std::mutex pull_mutex;
		std::condition_variable pull_condVar;

		bool readyForFence = true;
		bool readyForRun = false;
		bool skipSwap = false;
		GLRRunType type = GLRRunType::END;

		// GLuint fence; For future AZDO stuff?
		std::vector<GLRStep *> steps;
		std::vector<GLRInitStep> initSteps;

		// Swapchain.
		bool hasBegun = false;
		uint32_t curSwapchainImage = -1;
		
		GLDeleter deleter;
	};

	FrameData frameData_[MAX_INFLIGHT_FRAMES];

	// Submission time state
	int curWidth_;
	int curHeight_;
	bool insideFrame_ = false;
	GLRStep *curRenderStep_ = nullptr;
	std::vector<GLRStep *> steps_;
	std::vector<GLRInitStep> initSteps_;

	// Execution time state
	bool run_ = true;
	// Thread is managed elsewhere, and should call ThreadFunc.
	std::mutex mutex_;
	int threadInitFrame_ = 0;
	GLQueueRunner queueRunner_;

	GLDeleter deleter_;

	bool useThread_ = false;

	int curFrame_ = 0;
};

