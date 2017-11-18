#include <cassert>

#include "GLRenderManager.h"
#include "thread/threadutil.h"
#include "base/logging.h"

#if 0 // def _DEBUG
#define VLOG ILOG
#else
#define VLOG(...)
#endif

void GLCreateImage(GLRImage &img, int width, int height, GLuint format, bool color) {

}

void GLDeleter::Perform() {
	for (auto shader : shaders) {
		delete shader;
	}
	for (auto program : programs) {
		delete program;
	}
	// ..
}

GLRenderManager::GLRenderManager() {
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {

	}
}

GLRenderManager::~GLRenderManager() {
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {

	}
}

void GLRenderManager::ThreadFunc() {
	setCurrentThreadName("RenderMan");
	int threadFrame = threadInitFrame_;
	bool nextFrame = false;
	bool firstFrame = true;
	while (true) {
		{
			if (nextFrame) {
				threadFrame++;
				if (threadFrame >= MAX_INFLIGHT_FRAMES)
					threadFrame = 0;
			}
			FrameData &frameData = frameData_[threadFrame];
			std::unique_lock<std::mutex> lock(frameData.pull_mutex);
			while (!frameData.readyForRun && run_) {
				VLOG("PULL: Waiting for frame[%d].readyForRun", threadFrame);
				frameData.pull_condVar.wait(lock);
			}
			if (!frameData.readyForRun && !run_) {
				// This means we're out of frames to render and run_ is false, so bail.
				break;
			}
			VLOG("PULL: frame[%d].readyForRun = false", threadFrame);
			frameData.readyForRun = false;
			// Previously we had a quick exit here that avoided calling Run() if run_ was suddenly false,
			// but that created a race condition where frames could end up not finished properly on resize etc.

			// Only increment next time if we're done.
			nextFrame = frameData.type == GLRRunType::END;
			assert(frameData.type == GLRRunType::END || frameData.type == GLRRunType::SYNC);
		}
		VLOG("PULL: Running frame %d", threadFrame);
		if (firstFrame) {
			ILOG("Running first frame (%d)", threadFrame);
			firstFrame = false;
		}
		Run(threadFrame);
		VLOG("PULL: Finished frame %d", threadFrame);
	}

	VLOG("PULL: Quitting");
}

void GLRenderManager::StopThread() {
	// Since we don't control the thread directly, this will only pause the thread.


	if (useThread_ && run_) {
		run_ = false;
		for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
			auto &frameData = frameData_[i];
			{
				std::unique_lock<std::mutex> lock(frameData.push_mutex);
				frameData.push_condVar.notify_all();
			}
			{
				std::unique_lock<std::mutex> lock(frameData.pull_mutex);
				frameData.pull_condVar.notify_all();
			}
		}

		// TODO: Wait for something here!

		ILOG("GL submission thread paused. Frame=%d", curFrame_);

		// Eat whatever has been queued up for this frame if anything.
		Wipe();

		// Wait for any fences to finish and be resignaled, so we don't have sync issues.
		// Also clean out any queued data, which might refer to things that might not be valid
		// when we restart...
		for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
			auto &frameData = frameData_[i];
			if (frameData.readyForRun || frameData.steps.size() != 0) {
				Crash();
			}
			frameData.readyForRun = false;
			for (size_t i = 0; i < frameData.steps.size(); i++) {
				delete frameData.steps[i];
			}
			frameData.steps.clear();

			std::unique_lock<std::mutex> lock(frameData.push_mutex);
			while (!frameData.readyForFence) {
				VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (stop)", i);
				frameData.push_condVar.wait(lock);
			}
		}
	} else {
		ILOG("GL submission thread was already paused.");
	}
}

void GLRenderManager::BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, uint32_t clearColor, float clearDepth, uint8_t clearStencil) {
	assert(insideFrame_);
	// Eliminate dupes.
	if (steps_.size() && steps_.back()->render.framebuffer == fb && steps_.back()->stepType == GLRStepType::RENDER) {
		if (color != GLRRenderPassAction::CLEAR && depth != GLRRenderPassAction::CLEAR) {
			// We don't move to a new step, this bind was unnecessary and we can safely skip it.
			return;
		}
	}
	if (curRenderStep_ && curRenderStep_->commands.size() == 0) {
		VLOG("Empty render step. Usually happens after uploading pixels..");
	}

	GLRStep *step = new GLRStep{ GLRStepType::RENDER };
	// This is what queues up new passes, and can end previous ones.
	step->render.framebuffer = fb;
	step->render.numDraws = 0;
	steps_.push_back(step);

	GLuint clearMask = 0;
	GLRRenderData data;
	data.cmd = GLRRenderCommand::CLEAR;
	if (color == GLRRenderPassAction::CLEAR) {
		clearMask |= GL_COLOR_BUFFER_BIT;
		data.clear.clearColor = clearColor;
	}
	if (depth == GLRRenderPassAction::CLEAR) {
		clearMask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
		data.clear.clearZ = clearDepth;
		data.clear.clearStencil = clearStencil;
	}
	if (clearMask) {
		data.clear.clearMask = clearMask;
		step->commands.push_back(data);
	}

	curRenderStep_ = step;
}

void GLRenderManager::BindFramebufferAsTexture(GLRFramebuffer *fb, int binding, int aspectBit, int attachment) {
	_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
	GLRRenderData data{ GLRRenderCommand::BIND_FB_TEXTURE };
	data.bind_fb_texture.slot = binding;
	data.bind_fb_texture.framebuffer = fb;
	data.bind_fb_texture.aspect = aspectBit;
	curRenderStep_->commands.push_back(data);
}

void GLRenderManager::CopyFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLOffset2D dstPos, int aspectMask) {
	GLRStep * step = new GLRStep{ GLRStepType::COPY };
	step->copy.srcRect = srcRect;
	step->copy.dstPos = dstPos;
	step->copy.src = src;
	step->copy.dst = dst;
	step->copy.dstPos = dstPos;
	step->copy.aspectMask = aspectMask;
	steps_.push_back(step);
}

void GLRenderManager::BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter) {
	Crash();
}

void GLRenderManager::BeginFrame() {
	VLOG("BeginFrame");

	int curFrame = GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	// Make sure the very last command buffer from the frame before the previous has been fully executed.
	if (useThread_) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		while (!frameData.readyForFence) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
	}

	VLOG("PUSH: Fencing %d", curFrame);

	// vkWaitForFences(device, 1, &frameData.fence, true, UINT64_MAX);
	// vkResetFences(device, 1, &frameData.fence);

	// Must be after the fence - this performs deletes.
	VLOG("PUSH: BeginFrame %d", curFrame);
	if (!run_) {
		WLOG("BeginFrame while !run_!");
	}

	// vulkan_->BeginFrame();
	frameData.deleter.Perform();

	insideFrame_ = true;
}

void GLRenderManager::Finish() {
	curRenderStep_ = nullptr;
	int curFrame = GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (!useThread_) {
		frameData.steps = std::move(steps_);
		frameData.initSteps = std::move(initSteps_);
		frameData.type = GLRRunType::END;
		Run(curFrame);
	} else {
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		VLOG("PUSH: Frame[%d].readyForRun = true", curFrame);
		frameData.steps = std::move(steps_);
		frameData.initSteps = std::move(initSteps_);
		frameData.readyForRun = true;
		frameData.type = GLRRunType::END;
		frameData.pull_condVar.notify_all();
	}

	// vulkan_->EndFrame();
	frameData_[curFrame_].deleter.Take(deleter_);

	curFrame_++;
	if (curFrame_ >= MAX_INFLIGHT_FRAMES)
		curFrame_ = 0;

	insideFrame_ = false;
}

void GLRenderManager::BeginSubmitFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	if (!frameData.hasBegun) {
		frameData.hasBegun = true;
	}
}

void GLRenderManager::Submit(int frame, bool triggerFence) {
	FrameData &frameData = frameData_[frame];

	// In GL, submission happens automatically in Run().

	// When !triggerFence, we notify after syncing with Vulkan.
	if (useThread_ && triggerFence) {
		VLOG("PULL: Frame %d.readyForFence = true", frame);
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.push_condVar.notify_all();
	}
}

void GLRenderManager::EndSubmitFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	frameData.hasBegun = false;

	Submit(frame, true);

	if (!frameData.skipSwap) {
		// glSwapBuffers();
	} else {
		frameData.skipSwap = false;
	}
}

void GLRenderManager::Run(int frame) {
	BeginSubmitFrame(frame);

	FrameData &frameData = frameData_[frame];
	auto &stepsOnThread = frameData_[frame].steps;
	auto &initStepsOnThread = frameData_[frame].initSteps;
	// queueRunner_.LogSteps(stepsOnThread);
	queueRunner_.RunInitSteps(initStepsOnThread);
	queueRunner_.RunSteps(stepsOnThread);
	stepsOnThread.clear();
	initStepsOnThread.clear();

	switch (frameData.type) {
	case GLRRunType::END:
		EndSubmitFrame(frame);
		break;

	case GLRRunType::SYNC:
		EndSyncFrame(frame);
		break;

	default:
		assert(false);
	}

	VLOG("PULL: Finished running frame %d", frame);
}

void GLRenderManager::EndSyncFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	Submit(frame, false);

	// This is brutal! Should probably wait for a fence instead, not that it'll matter much since we'll
	// still stall everything.
	glFinish();
	// vkDeviceWaitIdle(vulkan_->GetDevice());

	// At this point we can resume filling the command buffers for the current frame since
	// we know the device is idle - and thus all previously enqueued command buffers have been processed.
	// No need to switch to the next frame number.

	if (useThread_) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.push_condVar.notify_all();
	}
}

void GLRenderManager::Wipe() {
	for (auto step : steps_) {
		delete step;
	}
	steps_.clear();
}