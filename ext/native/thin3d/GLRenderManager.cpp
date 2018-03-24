#include <cassert>

#include "GLRenderManager.h"
#include "gfx_es2/gpu_features.h"
#include "thread/threadutil.h"
#include "base/logging.h"
#include "GPU/GPUState.h"
#include "Common/MemoryUtil.h"

#if 0 // def _DEBUG
#define VLOG ILOG
#else
#define VLOG(...)
#endif

void GLDeleter::Perform() {
	for (auto shader : shaders) {
		delete shader;
	}
	shaders.clear();
	for (auto program : programs) {
		delete program;
	}
	programs.clear();
	for (auto buffer : buffers) {
		delete buffer;
	}
	buffers.clear();
	for (auto texture : textures) {
		delete texture;
	}
	textures.clear();
	for (auto inputLayout : inputLayouts) {
		delete inputLayout;
	}
	inputLayouts.clear();
	for (auto framebuffer : framebuffers) {
		delete framebuffer;
	}
	framebuffers.clear();
}

GLRenderManager::GLRenderManager() {
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {

	}
}

GLRenderManager::~GLRenderManager() {
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		_assert_(frameData_[i].deleter.IsEmpty());
		_assert_(frameData_[i].deleter_prev.IsEmpty());
	}
	// Was anything deleted during shutdown?
	deleter_.Perform();
	_assert_(deleter_.IsEmpty());
}

void GLRenderManager::ThreadStart() {
	queueRunner_.CreateDeviceObjects();
	threadFrame_ = threadInitFrame_;

	bool mapBuffers = (gl_extensions.bugs & BUG_ANY_MAP_BUFFER_RANGE_SLOW) == 0;
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

		case GPU_VENDOR_QUALCOMM:
			bufferStrategy_ = GLBufferStrategy::FLUSH_INVALIDATE_UNMAP;
			break;

		default:
			bufferStrategy_ = GLBufferStrategy::SUBDATA;
		}
	} else {
		bufferStrategy_ = GLBufferStrategy::SUBDATA;
	}
}

void GLRenderManager::ThreadEnd() {
	ILOG("ThreadEnd");

	// Wait for any shutdown to complete in StopThread().
	std::unique_lock<std::mutex> lock(mutex_);
	queueRunner_.DestroyDeviceObjects();
	VLOG("PULL: Quitting");

	// Good point to run all the deleters to get rid of leftover objects.
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].deleter.Perform();
		frameData_[i].deleter_prev.Perform();
		for (int j = 0; j < (int)frameData_[i].steps.size(); j++) {
			delete frameData_[i].steps[j];
		}
		frameData_[i].steps.clear();
		frameData_[i].initSteps.clear();
	}
	deleter_.Perform();

	for (int i = 0; i < (int)steps_.size(); i++) {
		delete steps_[i];
	}
	steps_.clear();
	initSteps_.clear();
}

bool GLRenderManager::ThreadFrame() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (!run_)
		return false;

	// In case of syncs or other partial completion, we keep going until we complete a frame.
	do {
		if (nextFrame) {
			threadFrame_++;
			if (threadFrame_ >= MAX_INFLIGHT_FRAMES)
				threadFrame_ = 0;
		}
		FrameData &frameData = frameData_[threadFrame_];
		{
			std::unique_lock<std::mutex> lock(frameData.pull_mutex);
			while (!frameData.readyForRun && run_) {
				VLOG("PULL: Waiting for frame[%d].readyForRun", threadFrame_);
				frameData.pull_condVar.wait(lock);
			}
			if (!frameData.readyForRun && !run_) {
				// This means we're out of frames to render and run_ is false, so bail.
				return false;
			}
			VLOG("PULL: Setting frame[%d].readyForRun = false", threadFrame_);
			frameData.readyForRun = false;
			frameData.deleter_prev.Perform();
			frameData.deleter_prev.Take(frameData.deleter);
			// Previously we had a quick exit here that avoided calling Run() if run_ was suddenly false,
			// but that created a race condition where frames could end up not finished properly on resize etc.

			// Only increment next time if we're done.
			nextFrame = frameData.type == GLRRunType::END;
			assert(frameData.type == GLRRunType::END || frameData.type == GLRRunType::SYNC);
		}
		VLOG("PULL: Running frame %d", threadFrame_);
		if (firstFrame) {
			ILOG("Running first frame (%d)", threadFrame_);
			firstFrame = false;
		}
		Run(threadFrame_);
		VLOG("PULL: Finished frame %d", threadFrame_);
	} while (!nextFrame);
	return true;
}

void GLRenderManager::StopThread() {
	// Since we don't control the thread directly, this will only pause the thread.

	if (run_) {
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

		// Wait until we've definitely stopped the threadframe.
		std::unique_lock<std::mutex> lock(mutex_);

		ILOG("GL submission thread paused. Frame=%d", curFrame_);

		// Eat whatever has been queued up for this frame if anything.
		Wipe();

		// Wait for any fences to finish and be resignaled, so we don't have sync issues.
		// Also clean out any queued data, which might refer to things that might not be valid
		// when we restart...
		for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
			auto &frameData = frameData_[i];
			std::unique_lock<std::mutex> lock(frameData.push_mutex);
			if (frameData.readyForRun || frameData.steps.size() != 0) {
				Crash();
			}
			frameData.readyForRun = false;
			frameData.readyForSubmit = false;
			for (size_t i = 0; i < frameData.steps.size(); i++) {
				delete frameData.steps[i];
			}
			frameData.steps.clear();
			frameData.initSteps.clear();

			while (!frameData.readyForFence) {
				VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (stop)", i);
				frameData.push_condVar.wait(lock);
			}
		}
	} else {
		ILOG("GL submission thread was already paused.");
	}
}

void GLRenderManager::BindFramebufferAsRenderTarget(GLRFramebuffer *fb, GLRRenderPassAction color, GLRRenderPassAction depth, GLRRenderPassAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil) {
	assert(insideFrame_);
	// Eliminate dupes.
	if (steps_.size() && steps_.back()->render.framebuffer == fb && steps_.back()->stepType == GLRStepType::RENDER) {
		if (color != GLRRenderPassAction::CLEAR && depth != GLRRenderPassAction::CLEAR && stencil != GLRRenderPassAction::CLEAR) {
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

	// Every step clears this state.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
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
	GLRStep *step = new GLRStep{ GLRStepType::COPY };
	step->copy.srcRect = srcRect;
	step->copy.dstPos = dstPos;
	step->copy.src = src;
	step->copy.dst = dst;
	step->copy.aspectMask = aspectMask;
	steps_.push_back(step);

	// Every step clears this state.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
}

void GLRenderManager::BlitFramebuffer(GLRFramebuffer *src, GLRect2D srcRect, GLRFramebuffer *dst, GLRect2D dstRect, int aspectMask, bool filter) {
	GLRStep *step = new GLRStep{ GLRStepType::BLIT };
	step->blit.srcRect = srcRect;
	step->blit.dstRect = dstRect;
	step->blit.src = src;
	step->blit.dst = dst;
	step->blit.aspectMask = aspectMask;
	step->blit.filter = filter;
	steps_.push_back(step);

	// Every step clears this state.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
}

bool GLRenderManager::CopyFramebufferToMemorySync(GLRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride) {
	GLRStep *step = new GLRStep{ GLRStepType::READBACK };
	step->readback.src = src;
	step->readback.srcRect = { x, y, w, h };
	step->readback.aspectMask = aspectBits;
	step->readback.dstFormat = destFormat;
	steps_.push_back(step);

	// Every step clears this state.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);

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
		_assert_(false);
	}
	queueRunner_.CopyReadbackBuffer(w, h, srcFormat, destFormat, pixelStride, pixels);
	return true;
}

void GLRenderManager::CopyImageToMemorySync(GLRTexture *texture, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride) {
	GLRStep *step = new GLRStep{ GLRStepType::READBACK_IMAGE };
	step->readback_image.texture = texture;
	step->readback_image.mipLevel = mipLevel;
	step->readback_image.srcRect = { x, y, w, h };
	steps_.push_back(step);

	// Every step clears this state.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);

	curRenderStep_ = nullptr;
	FlushSync();

	queueRunner_.CopyReadbackBuffer(w, h, Draw::DataFormat::R8G8B8A8_UNORM, destFormat, pixelStride, pixels);
}

void GLRenderManager::BeginFrame() {
	VLOG("BeginFrame");

	int curFrame = GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	// Make sure the very last command buffer from the frame before the previous has been fully executed.
	{
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		while (!frameData.readyForFence) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
		frameData.readyForSubmit = true;
	}

	VLOG("PUSH: Fencing %d", curFrame);

	// glFenceSync(&frameData.fence...)

	// Must be after the fence - this performs deletes.
	VLOG("PUSH: BeginFrame %d", curFrame);
	if (!run_) {
		WLOG("BeginFrame while !run_!");
	}

	// vulkan_->BeginFrame();
	// In GL, we have to do deletes on the submission thread.

	insideFrame_ = true;
}

void GLRenderManager::Finish() {
	curRenderStep_ = nullptr;
	int curFrame = GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	{
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		VLOG("PUSH: Frame[%d].readyForRun = true, notifying pull", curFrame);
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.initSteps = std::move(initSteps_);
		initSteps_.clear();
		frameData.readyForRun = true;
		frameData.type = GLRRunType::END;
		frameData_[curFrame_].deleter.Take(deleter_);
	}

	// Notify calls do not in fact need to be done with the mutex locked.
	frameData.pull_condVar.notify_all();

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

// Render thread
void GLRenderManager::Submit(int frame, bool triggerFence) {
	FrameData &frameData = frameData_[frame];

	// In GL, submission happens automatically in Run().

	// When !triggerFence, we notify after syncing with Vulkan.

	if (triggerFence) {
		VLOG("PULL: Frame %d.readyForFence = true", frame);

		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		assert(frameData.readyForSubmit);
		frameData.readyForFence = true;
		frameData.readyForSubmit = false;
		frameData.push_condVar.notify_all();
	}
}

// Render thread
void GLRenderManager::EndSubmitFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	frameData.hasBegun = false;

	Submit(frame, true);

	if (!frameData.skipSwap) {
		if (swapIntervalChanged_) {
			swapIntervalChanged_ = false;
			if (swapIntervalFunction_) {
				swapIntervalFunction_(swapInterval_);
			}
		}
		if (swapFunction_) {
			swapFunction_();
		}
	} else {
		frameData.skipSwap = false;
	}
}

// Render thread
void GLRenderManager::Run(int frame) {
	BeginSubmitFrame(frame);

	FrameData &frameData = frameData_[frame];

	auto &stepsOnThread = frameData_[frame].steps;
	auto &initStepsOnThread = frameData_[frame].initSteps;
	// queueRunner_.LogSteps(stepsOnThread);
	queueRunner_.RunInitSteps(initStepsOnThread);

	// Run this after RunInitSteps so any fresh GLRBuffers for the pushbuffers can get created.
	for (auto iter : frameData.activePushBuffers) {
		iter->Flush();
		iter->UnmapDevice();
	}

	queueRunner_.RunSteps(stepsOnThread);
	stepsOnThread.clear();
	initStepsOnThread.clear();

	for (auto iter : frameData.activePushBuffers) {
		iter->MapDevice(bufferStrategy_);
	}

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

void GLRenderManager::FlushSync() {
	// TODO: Reset curRenderStep_?
	int curFrame = curFrame_;
	FrameData &frameData = frameData_[curFrame];
	{
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		VLOG("PUSH: Frame[%d].readyForRun = true (sync)", curFrame);
		frameData.initSteps = std::move(initSteps_);
		initSteps_.clear();
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.readyForRun = true;
		assert(frameData.readyForFence == false);
		frameData.type = GLRRunType::SYNC;
		frameData.pull_condVar.notify_all();
	}
	{
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		// Wait for the flush to be hit, since we're syncing.
		while (!frameData.readyForFence) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (sync)", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
		frameData.readyForSubmit = true;
	}
}

// Render thread
void GLRenderManager::EndSyncFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	Submit(frame, false);

	// glFinish is not actually necessary here, and won't be until we start using
	// glBufferStorage. Then we need to use fences.
	// glFinish();

	// At this point we can resume filling the command buffers for the current frame since
	// we know the device is idle - and thus all previously enqueued command buffers have been processed.
	// No need to switch to the next frame number.

	{
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.readyForSubmit = true;
		frameData.push_condVar.notify_all();
	}
}

void GLRenderManager::Wipe() {
	initSteps_.clear();
	for (auto step : steps_) {
		delete step;
	}
	steps_.clear();
}

void GLRenderManager::WaitUntilQueueIdle() {
	// Just wait for all frames to be ready.
	for (int i = 0; i < MAX_INFLIGHT_FRAMES; i++) {
		FrameData &frameData = frameData_[i];

		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		// Ignore unsubmitted frames.
		while (!frameData.readyForFence && frameData.readyForRun) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (wait idle)", i);
			frameData.push_condVar.wait(lock);
		}
	}
}

GLPushBuffer::GLPushBuffer(GLRenderManager *render, GLuint target, size_t size) : render_(render), target_(target), size_(size) {
	bool res = AddBuffer();
	assert(res);
}

GLPushBuffer::~GLPushBuffer() {
	assert(buffers_.empty());
}

void GLPushBuffer::Map() {
	assert(!writePtr_);
	auto &info = buffers_[buf_];
	writePtr_ = info.deviceMemory ? info.deviceMemory : info.localMemory;
	info.flushOffset = 0;
	// Force alignment.  This is needed for PushAligned() to work as expected.
	while ((intptr_t)writePtr_ & 15) {
		writePtr_++;
		offset_++;
		info.flushOffset++;
	}
	assert(writePtr_);
}

void GLPushBuffer::Unmap() {
	assert(writePtr_);
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
	buffers_[buf_].flushOffset = offset_;
	if (!buffers_[buf_].deviceMemory && writePtr_) {
		auto &info = buffers_[buf_];
		if (info.flushOffset != 0) {
			assert(info.buffer->buffer);
			glBindBuffer(target_, info.buffer->buffer);
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

			glBindBuffer(target_, info.buffer->buffer);
			glFlushMappedBufferRange(target_, 0, info.flushOffset);
			info.flushOffset = 0;
		}
	}
}

bool GLPushBuffer::AddBuffer() {
	BufInfo info;
	info.localMemory = (uint8_t *)AllocateAlignedMemory(size_, 16);
	info.buffer = render_->CreateBuffer(target_, size_, GL_DYNAMIC_DRAW);
	buf_ = buffers_.size();
	buffers_.push_back(info);
	return true;
}

void GLPushBuffer::Destroy() {
	for (BufInfo &info : buffers_) {
		// This will automatically unmap device memory, if needed.
		render_->DeleteBuffer(info.buffer);
		FreeAlignedMemory(info.localMemory);
	}
	buffers_.clear();
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
		assert(res);
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
	Destroy();

	size_ = newSize;
	bool res = AddBuffer();
	assert(res);
}

size_t GLPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	return sum;
}

void GLPushBuffer::MapDevice(GLBufferStrategy strategy) {
	strategy_ = strategy;
	if (strategy_ == GLBufferStrategy::SUBDATA) {
		return;
	}

	bool mapChanged = false;
	for (auto &info : buffers_) {
		if (!info.buffer->buffer || info.deviceMemory) {
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

		assert(info.localMemory || info.deviceMemory);
	}

	if (writePtr_ && mapChanged) {
		// This can happen during a sync.  Remap.
		writePtr_ = nullptr;
		Map();
	}
}

void GLPushBuffer::UnmapDevice() {
	for (auto &info : buffers_) {
		if (info.deviceMemory) {
			// TODO: Technically this can return false?
			info.buffer->Unmap();
			info.deviceMemory = nullptr;
		}
	}
}

void *GLRBuffer::Map(GLBufferStrategy strategy) {
	assert(buffer != 0);

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
		glBindBuffer(target_, buffer);

		if (gl_extensions.ARB_buffer_storage || gl_extensions.EXT_buffer_storage) {
#ifndef IOS
			if (!hasStorage_) {
				GLbitfield storageFlags = access & ~(GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
#ifdef USING_GLES2
				glBufferStorageEXT(target_, size_, nullptr, storageFlags);
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
		} else {
#ifndef USING_GLES2
			p = glMapBuffer(target_, GL_READ_WRITE);
#endif
		}
	}

	mapped_ = p != nullptr;
	return p;
}

bool GLRBuffer::Unmap() {
	glBindBuffer(target_, buffer);
	mapped_ = false;
	return glUnmapBuffer(target_) == GL_TRUE;
}
