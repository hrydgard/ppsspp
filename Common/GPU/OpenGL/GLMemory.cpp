#include "Common/MemoryUtil.h"
#include "Common/GPU/OpenGL/GLMemory.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Data/Text/Parsers.h"

extern std::thread::id renderThreadId;
#if MAX_LOGLEVEL >= DEBUG_LEVEL
static bool OnRenderThread() {
	return std::this_thread::get_id() == renderThreadId;
}
#endif

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

GLPushBuffer::GLPushBuffer(GLRenderManager *render, GLuint target, size_t size, const char *tag) : render_(render), size_(size), target_(target), tag_(tag) {
	AddBuffer();
	RegisterGPUMemoryManager(this);
}

GLPushBuffer::~GLPushBuffer() {
	UnregisterGPUMemoryManager(this);
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
	if (buf_ >= buffers_.size()) {
		_dbg_assert_msg_(false, "buf_ somehow got out of sync: %d vs %d", (int)buf_, (int)buffers_.size());
		return;
	}

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

void GLPushBuffer::AddBuffer() {
	// INFO_LOG(G3D, "GLPushBuffer(%s): Allocating %d bytes", tag_, size_);
	BufInfo info;
	info.localMemory = (uint8_t *)AllocateAlignedMemory(size_, 16);
	_assert_msg_(info.localMemory != 0, "GLPushBuffer alloc fail: %d (%s)", (int)size_, tag_);
	info.buffer = render_->CreateBuffer(target_, size_, GL_DYNAMIC_DRAW);
	info.size = size_;
	buf_ = buffers_.size();
	buffers_.push_back(info);
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
		AddBuffer();
	}

	// Now, move to the next buffer and map it.
	offset_ = 0;
	Map();
}

void GLPushBuffer::Defragment() {
	_dbg_assert_msg_(!OnRenderThread(), "Defragment must not run on the render thread");

	if (buffers_.size() <= 1) {
		// Let's take this opportunity to jettison any localMemory we don't need.
		for (auto &info : buffers_) {
			if (info.deviceMemory) {
				FreeAlignedMemory(info.localMemory);
				info.localMemory = nullptr;
			}
		}
		return;
	}

	// Okay, we have more than one.  Destroy them all and start over with a larger one.

	// When calling AddBuffer, we sometimes increase size_. So if we allocated multiple buffers in a frame,
	// they won't all have the same size. Sum things up properly.
	size_t newSize = 0;
	for (int i = 0; i < (int)buffers_.size(); i++) {
		newSize += buffers_[i].size;
	}

	Destroy(false);

	// Set some sane but very free limits. If there's another spike, we'll just allocate more anyway.
	size_ = std::min(std::max(newSize, (size_t)65536), (size_t)(512 * 1024 * 1024));
	AddBuffer();
}

size_t GLPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	// When calling AddBuffer, we sometimes increase size_. So if we allocated multiple buffers in a frame,
	// they won't all have the same size. Sum things up properly.
	if (buffers_.size() > 1) {
		for (int i = 0; i < (int)buffers_.size() - 1; i++) {
			sum += buffers_[i].size;
		}
	}
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

void GLPushBuffer::GetDebugString(char *buffer, size_t bufSize) const {
	snprintf(buffer, bufSize, "%s: %s/%s (%d)", tag_, NiceSizeFormat(this->offset_).c_str(), NiceSizeFormat(this->size_).c_str(), (int)buffers_.size());
}
