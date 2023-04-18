#include "ppsspp_config.h"
#include "GLRenderManager.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/MemoryUtil.h"
#include "Common/Math/math_util.h"

#if 0 // def _DEBUG
#define VLOG(...) INFO_LOG(G3D, __VA_ARGS__)
#else
#define VLOG(...)
#endif

static std::thread::id renderThreadId;
#if MAX_LOGLEVEL >= DEBUG_LEVEL
static bool OnRenderThread() {
	return std::this_thread::get_id() == renderThreadId;
}
#endif

GLRTexture::GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int numMips) {
	if (caps.textureNPOTFullySupported) {
		canWrap = true;
	} else {
		canWrap = isPowerOf2(width) && isPowerOf2(height);
	}
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

GLRenderManager::~GLRenderManager() {
	_dbg_assert_(!run_);

	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		_assert_(frameData_[i].deleter.IsEmpty());
		_assert_(frameData_[i].deleter_prev.IsEmpty());
	}
	// Was anything deleted during shutdown?
	deleter_.Perform(this, skipGLCalls_);
	_assert_(deleter_.IsEmpty());
}

void GLRenderManager::ThreadStart(Draw::DrawContext *draw) {
	queueRunner_.CreateDeviceObjects();
	renderThreadId = std::this_thread::get_id();

	if (newInflightFrames_ != -1) {
		INFO_LOG(G3D, "Updating inflight frames to %d", newInflightFrames_);
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
}

void GLRenderManager::ThreadEnd() {
	INFO_LOG(G3D, "ThreadEnd");

	queueRunner_.DestroyDeviceObjects();
	VLOG("  PULL: Quitting");

	// Good time to run all the deleters to get rid of leftover objects.
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		// Since we're in shutdown, we should skip the GL calls on Android.
		frameData_[i].deleter.Perform(this, skipGLCalls_);
		frameData_[i].deleter_prev.Perform(this, skipGLCalls_);
	}
	deleter_.Perform(this, skipGLCalls_);
	for (int i = 0; i < (int)steps_.size(); i++) {
		delete steps_[i];
	}
	steps_.clear();
	initSteps_.clear();
}

// Unlike in Vulkan, this isn't a full independent function, instead it gets called every frame.
//
// This means that we have to block and run the render queue until we've presented one frame,
// at which point we can leave.
//
// NOTE: If run_ is true, we WILL run a task!
bool GLRenderManager::ThreadFrame() {
	if (!run_) {
		return false;
	}

	GLRRenderThreadTask task;

	// In case of syncs or other partial completion, we keep going until we complete a frame.
	while (true) {
		// Pop a task of the queue and execute it.
		// NOTE: We need to actually wait for a task, we can't just bail!

		{
			std::unique_lock<std::mutex> lock(pushMutex_);
			while (renderThreadQueue_.empty()) {
				pushCondVar_.wait(lock);
			}
			task = renderThreadQueue_.front();
			renderThreadQueue_.pop();
		}

		// We got a task! We can now have pushMutex_ unlocked, allowing the host to
		// push more work when it feels like it, and just start working.
		if (task.runType == GLRRunType::EXIT) {
			// Oh, host wanted out. Let's leave, and also let's notify the host.
			// This is unlike Vulkan too which can just block on the thread existing.
			std::unique_lock<std::mutex> lock(syncMutex_);
			syncCondVar_.notify_one();
			syncDone_ = true;
			break;
		}

		// Render the scene.
		VLOG("  PULL: Frame %d RUN (%0.3f)", task.frame, time_now_d());
		if (Run(task)) {
			// Swap requested, so we just bail the loop.
			break;
		}
	};

	return true;
}

void GLRenderManager::StopThread() {
	// There's not really a lot to do here anymore.
	INFO_LOG(G3D, "GLRenderManager::StopThread()");
	if (run_) {
		run_ = false;

		std::unique_lock<std::mutex> lock(pushMutex_);
		GLRRenderThreadTask exitTask{};
		exitTask.runType = GLRRunType::EXIT;
		renderThreadQueue_.push(exitTask);
		pushCondVar_.notify_one();
	} else {
		WARN_LOG(G3D, "GL submission thread was already paused.");
	}
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
	step->render.numDraws = 0;
	step->tag = tag;
	steps_.push_back(step);

	GLuint clearMask = 0;
	GLRRenderData data;
	data.cmd = GLRRenderCommand::CLEAR;
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

void GLRenderManager::BeginFrame() {
#ifdef _DEBUG
	curProgram_ = nullptr;
#endif

	int curFrame = GetCurFrame();

	GLFrameData &frameData = frameData_[curFrame];
	{
		VLOG("PUSH: BeginFrame (curFrame = %d, readyForFence = %d, time=%0.3f)", curFrame, (int)frameData.readyForFence, time_now_d());
		std::unique_lock<std::mutex> lock(frameData.fenceMutex);
		while (!frameData.readyForFence) {
			frameData.fenceCondVar.wait(lock);
		}
		frameData.readyForFence = false;
	}

	if (!run_) {
		WARN_LOG(G3D, "BeginFrame while !run_!");
	}

	insideFrame_ = true;
}

void GLRenderManager::Finish() {
	curRenderStep_ = nullptr;  // EndCurRenderStep is this simple here.

	int curFrame = GetCurFrame();
	GLFrameData &frameData = frameData_[curFrame];

	frameData_[curFrame].deleter.Take(deleter_);

	VLOG("PUSH: Finish, pushing task. curFrame = %d", curFrame);
	GLRRenderThreadTask task;
	task.frame = curFrame;
	task.runType = GLRRunType::PRESENT;

	{
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		renderThreadQueue_.back().initSteps = std::move(initSteps_);
		renderThreadQueue_.back().steps = std::move(steps_);
		initSteps_.clear();
		steps_.clear();
		pushCondVar_.notify_one();
	}

	curFrame_++;
	if (curFrame_ >= inflightFrames_)
		curFrame_ = 0;

	insideFrame_ = false;
}

// Render thread. Returns true if the caller should handle a swap.
bool GLRenderManager::Run(GLRRenderThreadTask &task) {
	GLFrameData &frameData = frameData_[task.frame];

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

	if (IsVREnabled()) {
		int passes = GetVRPassesCount();
		for (int i = 0; i < passes; i++) {
			PreVRFrameRender(i);
			queueRunner_.RunSteps(task.steps, skipGLCalls_, i < passes - 1, true);
			PostVRFrameRender();
		}
	} else {
		queueRunner_.RunSteps(task.steps, skipGLCalls_, false, false);
	}

	if (!skipGLCalls_) {
		for (auto iter : frameData.activePushBuffers) {
			iter->MapDevice(bufferStrategy_);
		}
	}

	bool swapRequest = false;

	switch (task.runType) {
	case GLRRunType::PRESENT:
		if (!frameData.skipSwap) {
			if (swapIntervalChanged_) {
				swapIntervalChanged_ = false;
				if (swapIntervalFunction_) {
					swapIntervalFunction_(swapInterval_);
				}
			}
			// This is the swapchain framebuffer flip.
			if (swapFunction_) {
				VLOG("  PULL: SwapFunction()");
				swapFunction_();
				if (!retainControl_) {
					// get out of here.
					swapRequest = true;
				}
			} else {
				VLOG("  PULL: SwapRequested");
				swapRequest = true;
			}
		} else {
			frameData.skipSwap = false;
		}
		frameData.hasBegun = false;

		VLOG("  PULL: Frame %d.readyForFence = true", task.frame);

		{
			std::lock_guard<std::mutex> lock(frameData.fenceMutex);
			frameData.readyForFence = true;
			frameData.fenceCondVar.notify_one();
			// At this point, we're done with this framedata (for now).
		}

		break;

	case GLRRunType::SYNC:
		frameData.hasBegun = false;

		// glFinish is not actually necessary here, and won't be unless we start using
		// glBufferStorage. Then we need to use fences.
		{
			std::unique_lock<std::mutex> lock(syncMutex_);
			syncDone_ = true;
			syncCondVar_.notify_one();
		}
		break;

	default:
		_assert_(false);
	}
	VLOG("  PULL: ::Run(): Done running tasks");
	return swapRequest;
}

void GLRenderManager::FlushSync() {
	{
		VLOG("PUSH: Frame[%d].readyForRun = true (sync)", curFrame_);

		GLRRenderThreadTask task;
		task.frame = curFrame_;
		task.runType = GLRRunType::SYNC;

		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		renderThreadQueue_.back().initSteps = std::move(initSteps_);
		renderThreadQueue_.back().steps = std::move(steps_);
		pushCondVar_.notify_one();
		steps_.clear();
	}

	{
		std::unique_lock<std::mutex> lock(syncMutex_);
		// Wait for the flush to be hit, since we're syncing.
		while (!syncDone_) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (sync)", curFrame_);
			syncCondVar_.wait(lock);
		}
		syncDone_ = false;
	}
}

GLPushBuffer::GLPushBuffer(GLRenderManager *render, GLuint target, size_t size) : render_(render), size_(size), target_(target) {
	bool res = AddBuffer();
	_assert_(res);
}

GLPushBuffer::~GLPushBuffer() {
	Destroy(true);
}

void GLPushBuffer::Map() {
	_assert_(!writePtr_);
	auto &info = buffers_[buf_];
	writePtr_ = info.deviceMemory ? info.deviceMemory : info.localMemory;
	info.flushOffset = 0;
	// Force alignment.  This is needed for PushAligned() to work as expected.
	while ((intptr_t)writePtr_ & 15) {
		writePtr_++;
		offset_++;
		info.flushOffset++;
	}
	_assert_(writePtr_);
}

void GLPushBuffer::Unmap() {
	_assert_(writePtr_);
	if (!buffers_[buf_].deviceMemory) {
		// Here we simply upload the data to the last buffer.
		// Might be worth trying with size_ instead of offset_, so the driver can replace
		// the whole buffer. At least if it's close.
		render_->BufferSubdata(buffers_[buf_].buffer, 0, offset_, buffers_[buf_].localMemory, false);
	} else {
		buffers_[buf_].flushOffset = offset_;
	}
	writePtr_ = nullptr;
}

void GLPushBuffer::Flush() {
	// Must be called from the render thread.
	_dbg_assert_(OnRenderThread());

	buffers_[buf_].flushOffset = offset_;
	if (!buffers_[buf_].deviceMemory && writePtr_) {
		auto &info = buffers_[buf_];
		if (info.flushOffset != 0) {
			_assert_(info.buffer->buffer_);
			glBindBuffer(target_, info.buffer->buffer_);
			glBufferSubData(target_, 0, info.flushOffset, info.localMemory);
		}

		// Here we will submit all the draw calls, with the already known buffer and offsets.
		// Might as well reset the write pointer here and start over the current buffer.
		writePtr_ = info.localMemory;
		offset_ = 0;
		info.flushOffset = 0;
	}

	// For device memory, we flush all buffers here.
	if ((strategy_ & GLBufferStrategy::MASK_FLUSH) != 0) {
		for (auto &info : buffers_) {
			if (info.flushOffset == 0 || !info.deviceMemory)
				continue;

			glBindBuffer(target_, info.buffer->buffer_);
			glFlushMappedBufferRange(target_, 0, info.flushOffset);
			info.flushOffset = 0;
		}
	}
}

bool GLPushBuffer::AddBuffer() {
	BufInfo info;
	info.localMemory = (uint8_t *)AllocateAlignedMemory(size_, 16);
	if (!info.localMemory)
		return false;
	info.buffer = render_->CreateBuffer(target_, size_, GL_DYNAMIC_DRAW);
	buf_ = buffers_.size();
	buffers_.push_back(info);
	return true;
}

void GLPushBuffer::Destroy(bool onRenderThread) {
	if (buf_ == -1)
		return;  // Already destroyed
	for (BufInfo &info : buffers_) {
		// This will automatically unmap device memory, if needed.
		// NOTE: We immediately delete the buffer, don't go through the deleter, if we're on the render thread.
		if (onRenderThread) {
			delete info.buffer;
		} else {
			render_->DeleteBuffer(info.buffer);
		}

		FreeAlignedMemory(info.localMemory);
	}
	buffers_.clear();
	buf_ = -1;
}

void GLPushBuffer::NextBuffer(size_t minSize) {
	// First, unmap the current memory.
	Unmap();

	buf_++;
	if (buf_ >= buffers_.size() || minSize > size_) {
		// Before creating the buffer, adjust to the new size_ if necessary.
		while (size_ < minSize) {
			size_ <<= 1;
		}

		bool res = AddBuffer();
		_assert_(res);
		if (!res) {
			// Let's try not to crash at least?
			buf_ = 0;
		}
	}

	// Now, move to the next buffer and map it.
	offset_ = 0;
	Map();
}

void GLPushBuffer::Defragment() {
	_dbg_assert_msg_(!OnRenderThread(), "Defragment must not run on the render thread");

	if (buffers_.size() <= 1) {
		// Let's take this chance to jetison localMemory we don't need.
		for (auto &info : buffers_) {
			if (info.deviceMemory) {
				FreeAlignedMemory(info.localMemory);
				info.localMemory = nullptr;
			}
		}

		return;
	}

	// Okay, we have more than one.  Destroy them all and start over with a larger one.
	size_t newSize = size_ * buffers_.size();
	Destroy(false);

	size_ = newSize;
	bool res = AddBuffer();
	_assert_msg_(res, "AddBuffer failed");
}

size_t GLPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	return sum;
}

void GLPushBuffer::MapDevice(GLBufferStrategy strategy) {
	_dbg_assert_msg_(OnRenderThread(), "MapDevice must run on render thread");

	strategy_ = strategy;
	if (strategy_ == GLBufferStrategy::SUBDATA) {
		return;
	}

	bool mapChanged = false;
	for (auto &info : buffers_) {
		if (!info.buffer->buffer_ || info.deviceMemory) {
			// Can't map - no device buffer associated yet or already mapped.
			continue;
		}

		info.deviceMemory = (uint8_t *)info.buffer->Map(strategy_);
		mapChanged = mapChanged || info.deviceMemory != nullptr;

		if (!info.deviceMemory && !info.localMemory) {
			// Somehow it failed, let's dodge crashing.
			info.localMemory = (uint8_t *)AllocateAlignedMemory(info.buffer->size_, 16);
			mapChanged = true;
		}

		_dbg_assert_msg_(info.localMemory || info.deviceMemory, "Local or device memory must succeed");
	}

	if (writePtr_ && mapChanged) {
		// This can happen during a sync.  Remap.
		writePtr_ = nullptr;
		Map();
	}
}

void GLPushBuffer::UnmapDevice() {
	_dbg_assert_msg_(OnRenderThread(), "UnmapDevice must run on render thread");

	for (auto &info : buffers_) {
		if (info.deviceMemory) {
			// TODO: Technically this can return false?
			info.buffer->Unmap();
			info.deviceMemory = nullptr;
		}
	}
}

void *GLRBuffer::Map(GLBufferStrategy strategy) {
	_assert_(buffer_ != 0);

	GLbitfield access = GL_MAP_WRITE_BIT;
	if ((strategy & GLBufferStrategy::MASK_FLUSH) != 0) {
		access |= GL_MAP_FLUSH_EXPLICIT_BIT;
	}
	if ((strategy & GLBufferStrategy::MASK_INVALIDATE) != 0) {
		access |= GL_MAP_INVALIDATE_BUFFER_BIT;
	}

	void *p = nullptr;
	bool allowNativeBuffer = strategy != GLBufferStrategy::SUBDATA;
	if (allowNativeBuffer) {
		glBindBuffer(target_, buffer_);

		if (gl_extensions.ARB_buffer_storage || gl_extensions.EXT_buffer_storage) {
#if !PPSSPP_PLATFORM(IOS)
			if (!hasStorage_) {
				GLbitfield storageFlags = access & ~(GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
#ifdef USING_GLES2
#ifdef GL_EXT_buffer_storage
				glBufferStorageEXT(target_, size_, nullptr, storageFlags);
#endif
#else
				glBufferStorage(target_, size_, nullptr, storageFlags);
#endif
				hasStorage_ = true;
			}
#endif
			p = glMapBufferRange(target_, 0, size_, access);
		} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
			// GLES3 or desktop 3.
			p = glMapBufferRange(target_, 0, size_, access);
		} else if (!gl_extensions.IsGLES) {
#ifndef USING_GLES2
			p = glMapBuffer(target_, GL_READ_WRITE);
#endif
		}
	}

	mapped_ = p != nullptr;
	return p;
}

bool GLRBuffer::Unmap() {
	glBindBuffer(target_, buffer_);
	mapped_ = false;
	return glUnmapBuffer(target_) == GL_TRUE;
}
