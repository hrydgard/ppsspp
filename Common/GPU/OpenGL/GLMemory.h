#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

#include "Common/GPU/GPUBackendCommon.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/Log.h"

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
class GLPushBuffer : public GPUMemoryManager {
public:
	friend class GLRenderManager;

	struct BufInfo {
		GLRBuffer *buffer = nullptr;
		uint8_t *localMemory = nullptr;
		uint8_t *deviceMemory = nullptr;
		size_t flushOffset = 0;
		size_t size;
	};

	GLPushBuffer(GLRenderManager *render, GLuint target, size_t size, const char *tag);
	~GLPushBuffer();

	void Reset() { offset_ = 0; }

	void GetDebugString(char *buffer, size_t bufSize) const override;

	const char *Name() const override { return tag_; };  // for sorting

	// Utility for users of this class, not used internally.
	enum { INVALID_OFFSET = 0xFFFFFFFF };

	// Needs context in case of defragment.
	void Begin() {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment();
		Map();
		_dbg_assert_(writePtr_);
	}

	void BeginNoReset() {
		Map();
	}

	void End() {
		Unmap();
	}

	void Map();
	void Unmap();

	bool IsReady() const {
		return writePtr_ != nullptr;
	}

	// Recommended - lets you write directly into the buffer through the returned pointer.
	// If you didn't end up using all the memory you grabbed here, then before calling Allocate or Push
	// again, call Rewind (see below).
	uint8_t *Allocate(uint32_t numBytes, uint32_t alignment, GLRBuffer **buf, uint32_t *bindOffset) {
		uint32_t offset = ((uint32_t)offset_ + alignment - 1) & ~(alignment - 1);
		if (offset + numBytes <= size_) {
			// Common path.
			offset_ = offset + numBytes;
			*buf = buffers_[buf_].buffer;
			*bindOffset = offset;
			return writePtr_ + offset;
		}

		NextBuffer(numBytes);
		*bindOffset = 0;
		*buf = buffers_[buf_].buffer;
		// Need to mark the allocated range used in the new buffer. How did things work before this?
		offset_ = numBytes;
		return writePtr_;
	}

	// For convenience if all you'll do is to copy.
	uint32_t Push(const void *data, uint32_t numBytes, int alignment, GLRBuffer **buf) {
		uint32_t bindOffset;
		uint8_t *ptr = Allocate(numBytes, alignment, buf, &bindOffset);
		memcpy(ptr, data, numBytes);
		return bindOffset;
	}

	uint8_t *GetPtr(uint32_t offset) {
		return writePtr_ + offset;
	}

	// If you didn't use all of the previous allocation you just made (obviously can't be another one),
	// you can return memory to the buffer by specifying the offset up until which you wrote data.
	// Pass in the buffer you got last time. If that buffer has been filled already, no rewind can be safely done.
	// (well technically would be possible but not worth the trouble).
	void Rewind(GLRBuffer *buffer, uint32_t offset) {
		if (buffer == buffers_[buf_].buffer) {
			_dbg_assert_(offset != INVALID_OFFSET);
			_dbg_assert_(offset <= offset_);
			offset_ = offset;
		}
	}

	size_t GetOffset() const { return offset_; }
	size_t GetTotalSize() const;

	void Destroy(bool onRenderThread);
	void Flush();

protected:
	void MapDevice(GLBufferStrategy strategy);
	void UnmapDevice();

private:
	void AddBuffer();  // asserts on failure
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
	const char *tag_;
};
