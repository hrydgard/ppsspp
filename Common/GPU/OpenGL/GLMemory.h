#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

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
class GLPushBuffer {
public:
	friend class GLRenderManager;

	struct BufInfo {
		GLRBuffer *buffer = nullptr;
		uint8_t *localMemory = nullptr;
		uint8_t *deviceMemory = nullptr;
		size_t flushOffset = 0;
		size_t size;
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
		_dbg_assert_(writePtr_);
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

	bool IsReady() const {
		return writePtr_ != nullptr;
	}

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
		_dbg_assert_(writePtr_);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align, GLRBuffer **vkbuf) {
		_dbg_assert_(writePtr_);
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
		_dbg_assert_(writePtr_);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}
	void *PushAligned(size_t size, uint32_t *bindOffset, GLRBuffer **vkbuf, int align) {
		_dbg_assert_(writePtr_);
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
