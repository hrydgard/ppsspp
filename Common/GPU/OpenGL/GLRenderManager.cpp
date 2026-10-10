#include "ppsspp_config.h"
#include "GLRenderManager.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/Math/math_util.h"

#if 0 // def _DEBUG
#define VLOG(...) INFO_LOG(Log::G3D, __VA_ARGS__)
#else
#define VLOG(...)
#endif

GLRTexture::GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int numMips) {
	w = width;
	h = height;
	d = depth;
	this->numMips = numMips;
}

GLRTexture::~GLRTexture() {
	if (texture) {
		glDeleteTextures(1, &texture);
	}
}

GLRenderManager::GLRenderManager(HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory) : frameTimeHistory_(frameTimeHistory) {
	// size_t sz = sizeof(GLRRenderData);
	// _dbg_assert_(sz == 88);
}

GLRenderManager::~GLRenderManager() {
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		_assert_(frameData_[i].deleter.IsEmpty());
		_assert_(frameData_[i].deleter_prev.IsEmpty());
	}
	// Was anything deleted during shutdown?
	deleter_.Perform(this, skipGLCalls_);
	_assert_(deleter_.IsEmpty());
}

void GLRenderManager::ThreadStart(Draw::DrawContext *draw) {
	INFO_LOG(Log::G3D, "GLRenderManager::ThreadStart begin");

	queueRunner_.CreateDeviceObjects();

	exitNotified_ = false;
	hitExit_ = false;
	skipGLCalls_ = false;

	if (newInflightFrames_ != -1) {
		INFO_LOG(Log::G3D, "OpenGL: Updating inflight frames to %d", newInflightFrames_);
		inflightFrames_ = newInflightFrames_;
		newInflightFrames_ = -1;
	}

	// Don't save draw, we don't want any thread safety confusion.
	bool mapBuffers = draw->GetBugs().Has(Draw::Bugs::ANY_MAP_BUFFER_RANGE_SLOW);
	bool hasBufferStorage = gl_extensions.ARB_buffer_storage || gl_extensions.EXT_buffer_storage;
	if (!gl_extensions.VersionGEThan(3, 0, 0) && gl_extensions.IsGLES && !hasBufferStorage) {
		// Force disable if it wouldn't work anyway.
		mapBuffers = false;
	}

	// Notes on buffer mapping:
	// NVIDIA GTX 9xx / 2017-10 drivers - mapping improves speed, basic unmap seems best.
	// PowerVR GX6xxx / iOS 10.3 - mapping has little improvement, explicit flush is slower.
	if (mapBuffers) {
		switch (gl_extensions.gpuVendor) {
		case GPU_VENDOR_NVIDIA:
			bufferStrategy_ = GLBufferStrategy::FRAME_UNMAP;
			break;

		// Temporarily disabled because it doesn't work with task switching on Android.
		// The mapped buffer seems to just be pulled out like a rug from under us, crashing
		// as soon as any write happens, which can happen during shutdown since we write from the
		// Emu thread which may not yet have shut down. There may be solutions to this, but for now,
		// disable this strategy to avoid crashing.
		//case GPU_VENDOR_QUALCOMM:
		//	bufferStrategy_ = GLBufferStrategy::FLUSH_INVALIDATE_UNMAP;
		//	break;

		default:
			bufferStrategy_ = GLBufferStrategy::SUBDATA;
		}
	} else {
		bufferStrategy_ = GLBufferStrategy::SUBDATA;
	}

	INFO_LOG(Log::G3D, "GLRenderManager::ThreadStart end");
}

void GLRenderManager::ThreadEnd() {
	INFO_LOG(Log::G3D, "GLRenderManager::ThreadEnd begin");

	queueRunner_.DestroyDeviceObjects();

	VLOG("  PULL: Quitting");

	// Buffers that never made it over in a task, so the deleters below find them.
	AddNewPushBuffers(newPushBuffers_);

	// Good time to run all the deleters to get rid of leftover objects.
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		// Since we're in shutdown, we should skip the GL calls on Android.
		frameData_[i].deleter.Perform(this, skipGLCalls_);
		frameData_[i].deleter_prev.Perform(this, skipGLCalls_);
	}
	deleter_.Perform(this, skipGLCalls_);
	// Steps that never got submitted. A dry run frees the data they own (texture uploads etc), and the steps.
	queueRunner_.RunInitSteps(initSteps_, true);
	initSteps_.clear();
	queueRunner_.RunSteps(steps_, frameData_[0], true, false, false);
	steps_.clear();
	INFO_LOG(Log::G3D, "GLRenderManager::ThreadEnd end");
}

void GLRenderManager::NotifyEmuThreadExit() {
	_dbg_assert_(!exitNotified_);
	_dbg_assert_(!hitExit_);
	exitNotified_ = true;
	// We need to make sure that the render thread isn't waiting for more work, since the emu thread is gone.
	// This is a bit of a hack, but it works.
	PushTask(new GLRRenderThreadTask(GLRRunType::EXIT));
}

void GLRenderManager::NotifyEmuThreadPaused() {
	PushTask(new GLRRenderThreadTask(GLRRunType::PAUSE));
}

// Unlike in Vulkan, this isn't a full independent function, instead it gets called every frame.
//
// This means that we have to block and run the render queue until we've presented one frame,
// at which point we can leave.
bool GLRenderManager::ThreadFrame() {
	return RunQueue(RunUntil::PRESENT);
}

bool GLRenderManager::ThreadRunUntilPaused() {
	return RunQueue(RunUntil::PAUSE);
}

bool GLRenderManager::RunQueue(RunUntil until) {
	if (hitExit_) {
		return false;
	}

	GLRRenderThreadTask *task = nullptr;

	// In case of syncs or other partial completion, we keep going until we complete a frame.
	while (true) {
		// Pop a task off the queue and execute it. Exiting this loop is done with a special EXIT task,
		// to keep things uniform.
		while (renderThreadTasks_.empty()) {
			const int seen = renderThreadWork_.Seen();
			renderThreadQueue_.Drain([this](GLRRenderThreadTask *t) { renderThreadTasks_.push_back(t); });
			if (renderThreadTasks_.empty()) {
				renderThreadWork_.Wait(seen);
			}
		}
		task = renderThreadTasks_.front();
		renderThreadTasks_.pop_front();

		if (task->runType == GLRRunType::EXIT) {
			VLOG("  PULL: Frame %d EXIT (%0.3f)", task->frame, time_now_d());
			delete task;
			hitExit_ = true;
			return false;
		}

		if (task->runType == GLRRunType::PAUSE) {
			delete task;
			_dbg_assert_(until == RunUntil::PAUSE);
			if (until == RunUntil::PAUSE)
				break;
			continue;
		}

		// Render the scene.
		VLOG("  PULL: Frame %d RUN (%0.3f)", task->frame, time_now_d());
		if (Run(*task) && until == RunUntil::PRESENT) {
			// Swap requested, so we just bail the loop.
			delete task;
			break;
		}
		delete task;
	};
	return true;
}

std::string GLRenderManager::GetGpuProfileString() const {
	int curFrame = curFrame_;
	const GLQueueProfileContext &profile = frameData_[curFrame].profile;

	float cputime_ms = 1000.0f * (profile.cpuEndTime - profile.cpuStartTime);
	return StringFromFormat("CPU time to run the list: %0.2f ms\n\n%s", cputime_ms, profilePassesString_.c_str());
}

void GLRenderManager::BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, GLRRenderPassAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil, const char *tag) {
	_assert_(insideFrame_);
#ifdef _DEBUG
	curProgram_ = nullptr;
#endif

	// Eliminate dupes.
	if (steps_.size() && steps_.back()->stepType == GLRStepType::RENDER && steps_.back()->render.framebuffer == fb) {
		if (color != GLRRenderPassAction::CLEAR && depth != GLRRenderPassAction::CLEAR && stencil != GLRRenderPassAction::CLEAR) {
			// We don't move to a new step, this bind was unnecessary and we can safely skip it.
			curRenderStep_ = steps_.back();
			return;
		}
	}
	if (curRenderStep_ && curRenderStep_->commands.size() == 0) {
		VLOG("Empty render step. Usually happens after uploading pixels.");
	}

	GLRStep *step = new GLRStep{ GLRStepType::RENDER };
	// This is what queues up new passes, and can end previous ones.
	step->render.framebuffer = fb;
	step->render.color = color;
	step->render.depth = depth;
	step->render.stencil = stencil;
	step->tag = tag;
	steps_.push_back(step);

	GLuint clearMask = 0;
	GLRRenderData data(GLRRenderCommand::CLEAR);
	if (color == GLRRenderPassAction::CLEAR) {
		clearMask |= GL_COLOR_BUFFER_BIT;
		data.clear.clearColor = clearColor;
	}
	if (depth == GLRRenderPassAction::CLEAR) {
		clearMask |= GL_DEPTH_BUFFER_BIT;
		data.clear.clearZ = clearDepth;
	}
	if (stencil == GLRRenderPassAction::CLEAR) {
		clearMask |= GL_STENCIL_BUFFER_BIT;
		data.clear.clearStencil = clearStencil;
	}
	if (clearMask) {
		data.clear.scissorX = 0;
		data.clear.scissorY = 0;
		data.clear.scissorW = 0;
		data.clear.scissorH = 0;
		data.clear.clearMask = clearMask;
		data.clear.colorMask = 0xF;
		step->commands.push_back(data);
	}
	curRenderStep_ = step;

	if (fb) {
		if (color == GLRRenderPassAction::KEEP || depth == GLRRenderPassAction::KEEP || stencil == GLRRenderPassAction::KEEP) {
			step->dependencies.insert(fb);
		}
	}

	if (invalidationCallback_) {
		invalidationCallback_(InvalidationCallbackFlags::RENDER_PASS_STATE);
	}
}

// aspectBit: GL_COLOR_BUFFER_BIT etc
void GLRenderManager::BindFramebufferAsTexture(GLRFramebuffer *fb, int binding, int aspectBit) {
	_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == GLRStepType::RENDER);
	_dbg_assert_(binding < MAX_GL_TEXTURE_SLOTS);
	GLRRenderData data{ GLRRenderCommand::BIND_FB_TEXTURE };
	data.bind_fb_texture.slot = binding;
	data.bind_fb_texture.framebuffer = fb;
	data.bind_fb_texture.aspect = aspectBit;
	curRenderStep_->commands.push_back(data);
	curRenderStep_->dependencies.insert(fb);
}

void GLRenderManager::CopyFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLOffset2D dstPos, int aspectMask, const char *tag) {
	GLRStep *step = new GLRStep{ GLRStepType::COPY };
	step->copy.srcRect = srcRect;
	step->copy.dstPos = dstPos;
	step->copy.src = src;
	step->copy.dst = dst;
	step->copy.aspectMask = aspectMask;
	step->dependencies.insert(src);
	step->tag = tag;
	bool fillsDst = dst && srcRect.x == 0 && srcRect.y == 0 && srcRect.w == dst->width && srcRect.h == dst->height;
	if (dstPos.x != 0 || dstPos.y != 0 || !fillsDst)
		step->dependencies.insert(dst);
	steps_.push_back(step);
}

void GLRenderManager::BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter, const char *tag) {
	GLRStep *step = new GLRStep{ GLRStepType::BLIT };
	step->blit.srcRect = srcRect;
	step->blit.dstRect = dstRect;
	step->blit.src = src;
	step->blit.dst = dst;
	step->blit.aspectMask = aspectMask;
	step->blit.filter = filter;
	step->dependencies.insert(src);
	step->tag = tag;
	bool fillsDst = dst && dstRect.x == 0 && dstRect.y == 0 && dstRect.w == dst->width && dstRect.h == dst->height;
	if (!fillsDst)
		step->dependencies.insert(dst);
	steps_.push_back(step);
}

bool GLRenderManager::CopyFramebufferToMemory(GLRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, Draw::ReadbackMode mode, const char *tag) {
	_assert_(pixels);

	GLRStep *step = new GLRStep{ GLRStepType::READBACK };
	step->readback.src = src;
	step->readback.srcRect = { x, y, w, h };
	step->readback.aspectMask = aspectBits;
	step->readback.dstFormat = destFormat;
	step->dependencies.insert(src);
	step->tag = tag;
	steps_.push_back(step);

	curRenderStep_ = nullptr;
	FlushSync();

	Draw::DataFormat srcFormat;
	if (aspectBits & GL_COLOR_BUFFER_BIT) {
		srcFormat = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (aspectBits & GL_STENCIL_BUFFER_BIT) {
		// Copies from stencil are always S8.
		srcFormat = Draw::DataFormat::S8;
	} else if (aspectBits & GL_DEPTH_BUFFER_BIT) {
		// TODO: Do this properly.
		srcFormat = Draw::DataFormat::D24_S8;
	} else {
		return false;
	}
	queueRunner_.CopyFromReadbackBuffer(src, w, h, srcFormat, destFormat, pixelStride, pixels);
	return true;
}

void GLRenderManager::CopyImageToMemorySync(GLRTexture *texture, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, const char *tag) {
	_assert_(texture);
	_assert_(pixels);
	GLRStep *step = new GLRStep{ GLRStepType::READBACK_IMAGE };
	step->readback_image.texture = texture;
	step->readback_image.mipLevel = mipLevel;
	step->readback_image.srcRect = { x, y, w, h };
	step->tag = tag;
	steps_.push_back(step);

	curRenderStep_ = nullptr;
	FlushSync();

	queueRunner_.CopyFromReadbackBuffer(nullptr, w, h, Draw::DataFormat::R8G8B8A8_UNORM, destFormat, pixelStride, pixels);
}

void GLRenderManager::BeginFrame(bool enableProfiling) {
	_dbg_assert_(!exitNotified_);
	_dbg_assert_(!hitExit_);

#ifdef _DEBUG
	curProgram_ = nullptr;
#endif

	int curFrame = GetCurFrame();

	FrameTimeData &frameTimeData = frameTimeHistory_.Add(frameIdGen_);
	frameTimeData.frameBegin = time_now_d();
	frameTimeData.afterFenceWait = frameTimeData.frameBegin;

	GLFrameData &frameData = frameData_[curFrame];
	VLOG("PUSH: BeginFrame (curFrame = %d, time=%0.3f)", curFrame, time_now_d());
	// The render thread may still be presenting this frame slot's last use.
	ParkingLotWait(&frameData.readyForFence, [&] { return retro_atomic_load_acquire_int(&frameData.readyForFence) != 0; });
	retro_atomic_store_relaxed_int(&frameData.readyForFence, 0);

	frameData.frameId = frameIdGen_;
	frameData.profile.enabled = enableProfiling;
	frameIdGen_++;

	insideFrame_ = true;
}

void GLRenderManager::Finish() {
	curRenderStep_ = nullptr;  // EndCurRenderStep is this simple here.

	int curFrame = curFrame_;
	GLFrameData &frameData = frameData_[curFrame];

	frameTimeHistory_[frameData.frameId].firstSubmit = time_now_d();

	frameData_[curFrame].deleter.Take(deleter_);

	if (frameData.profile.enabled) {
		profilePassesString_ = std::move(frameData.profile.passesString);

#ifdef _DEBUG
		std::string cmdString;
		for (int i = 0; i < ARRAY_SIZE(frameData.profile.commandCounts); i++) {
			if (frameData.profile.commandCounts[i] > 0) {
				cmdString += StringFromFormat("%s: %d\n", RenderCommandToString((GLRRenderCommand)i), frameData.profile.commandCounts[i]);
			}
		}
		memset(frameData.profile.commandCounts, 0, sizeof(frameData.profile.commandCounts));
		profilePassesString_ = cmdString + profilePassesString_;
#endif

		frameData.profile.passesString.clear();
	}

	VLOG("PUSH: Finish, pushing task. curFrame = %d", curFrame);
	GLRRenderThreadTask *task = new GLRRenderThreadTask(GLRRunType::SUBMIT);
	task->frame = curFrame;
	TakeRecordedWork(task);
	PushTask(task);
}

void GLRenderManager::Present() {
	GLRRenderThreadTask *presentTask = new GLRRenderThreadTask(GLRRunType::PRESENT);
	presentTask->frame = curFrame_;
	PushTask(presentTask);

	int newCurFrame = curFrame_ + 1;
	if (newCurFrame >= inflightFrames_) {
		newCurFrame = 0;
	}
	curFrame_ = newCurFrame;

	insideFrame_ = false;
}

// Render thread. Returns true if the caller should handle a swap.
bool GLRenderManager::Run(GLRRenderThreadTask &task) {
	_dbg_assert_(task.frame >= 0);

	GLFrameData &frameData = frameData_[task.frame];

	if (task.runType == GLRRunType::PRESENT) {
		bool swapRequest = false;
		if (!frameData.skipSwap) {
			frameTimeHistory_[frameData.frameId].queuePresent = time_now_d();
			// This is the swapchain framebuffer flip.
			if (swapFunction_) {
				VLOG("  PULL: SwapFunction()");
				swapFunction_();
			}
			swapRequest = true;
		} else {
			frameData.skipSwap = false;
		}
		frameData.hasBegun = false;

		VLOG("  PULL: Frame %d.readyForFence = true", task.frame);

		// At this point, we're done with this framedata (for now).
		retro_atomic_store_release_int(&frameData.readyForFence, 1);
		ParkingLotNotify(&frameData.readyForFence);
		return swapRequest;
	}

	AddNewPushBuffers(task.newPushBuffers);

	if (!frameData.hasBegun) {
		frameData.hasBegun = true;

		frameData.deleter_prev.Perform(this, skipGLCalls_);
		frameData.deleter_prev.Take(frameData.deleter);
	}

	// queueRunner_.LogSteps(stepsOnThread);
	queueRunner_.RunInitSteps(task.initSteps, skipGLCalls_);

	// Run this after RunInitSteps so any fresh GLRBuffers for the pushbuffers can get created.
	if (!skipGLCalls_) {
		for (auto iter : frameData.activePushBuffers) {
			iter->Flush();
			iter->UnmapDevice();
		}
	}

	if (frameData.profile.enabled) {
		frameData.profile.cpuStartTime = time_now_d();
	}

	if (IsVREnabled()) {
		int passes = GetVRPassesCount();
		for (int i = 0; i < passes; i++) {
			PreVRFrameRender(i);
			queueRunner_.RunSteps(task.steps, frameData, skipGLCalls_, i < passes - 1, true);
			PostVRFrameRender();
		}
	} else {
		queueRunner_.RunSteps(task.steps, frameData, skipGLCalls_, false, false);
	}

	if (frameData.profile.enabled) {
		frameData.profile.cpuEndTime = time_now_d();
	}

	if (!skipGLCalls_) {
		for (auto iter : frameData.activePushBuffers) {
			iter->MapDevice(bufferStrategy_);
		}
	}

	switch (task.runType) {
	case GLRRunType::SUBMIT:
		break;

	case GLRRunType::SYNC:
		frameData.hasBegun = false;

		// glFinish is not actually necessary here, and won't be unless we start using
		// glBufferStorage. Then we need to use fences.
		retro_atomic_store_release_int(&syncDone_, 1);
		ParkingLotNotify(&syncDone_);
		break;

	default:
		_assert_(false);
	}
	VLOG("  PULL: ::Run(): Done running tasks");
	return false;
}

void GLRenderManager::FlushSync() {
	{
		VLOG("PUSH: Frame[%d].readyForRun = true (sync)", curFrame_);

		GLRRenderThreadTask *task = new GLRRenderThreadTask(GLRRunType::SYNC);
		task->frame = curFrame_;
		TakeRecordedWork(task);
		PushTask(task);
	}

	// Wait for the flush to be hit, since we're syncing.
	ParkingLotWait(&syncDone_, [this] { return retro_atomic_load_acquire_int(&syncDone_) != 0; });
	retro_atomic_store_relaxed_int(&syncDone_, 0);
}
