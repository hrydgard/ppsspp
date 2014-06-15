// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <vector>
#include <string>

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Core/MemMap.h"

struct GPUDebugOp {
	u32 pc;
	u8 cmd;
	u32 op;
	std::string desc;
};

enum GPUDebugBufferFormat {
	// These match GEBufferFormat.
	GPU_DBG_FORMAT_565 = 0,
	GPU_DBG_FORMAT_5551 = 1,
	GPU_DBG_FORMAT_4444 = 2,
	GPU_DBG_FORMAT_8888 = 3,
	GPU_DBG_FORMAT_INVALID = 0xFF,

	// These are reversed versions.
	GPU_DBG_FORMAT_REVERSE_FLAG = 4,
	GPU_DBG_FORMAT_565_REV = 4,
	GPU_DBG_FORMAT_5551_REV = 5,
	GPU_DBG_FORMAT_4444_REV = 6,

	// These don't, they're for depth/stencil buffers.
	GPU_DBG_FORMAT_FLOAT = 0x10,
	GPU_DBG_FORMAT_16BIT = 0x11,
	GPU_DBG_FORMAT_8BIT = 0x12,
};

inline GPUDebugBufferFormat &operator |=(GPUDebugBufferFormat &lhs, const GPUDebugBufferFormat &rhs) {
	lhs = GPUDebugBufferFormat((int)lhs | (int)rhs);
	return lhs;
}

struct GPUDebugBuffer {
	GPUDebugBuffer() : alloc_(false), data_(NULL) {
	}

	GPUDebugBuffer(void *data, u32 stride, u32 height, GEBufferFormat fmt, bool reversed = false)
		: alloc_(false), data_((u8 *)data), stride_(stride), height_(height), fmt_(GPUDebugBufferFormat(fmt)), flipped_(false) {
		if (reversed && fmt_ < GPU_DBG_FORMAT_8888) {
			fmt_ |= GPU_DBG_FORMAT_REVERSE_FLAG;
		}
	}

	GPUDebugBuffer(void *data, u32 stride, u32 height, GETextureFormat fmt, bool reversed = false)
		: alloc_(false), data_((u8 *)data), stride_(stride), height_(height), fmt_(GPUDebugBufferFormat(fmt)), flipped_(false) {
		if (reversed && fmt_ < GPU_DBG_FORMAT_8888) {
			fmt_ |= GPU_DBG_FORMAT_REVERSE_FLAG;
		}
	}

	GPUDebugBuffer(void *data, u32 stride, u32 height, GPUDebugBufferFormat fmt)
		: alloc_(false), data_((u8 *)data), stride_(stride), height_(height), fmt_(fmt), flipped_(false) {
	}

	GPUDebugBuffer(GPUDebugBuffer &&other) {
		alloc_ = other.alloc_;
		data_ = other.data_;
		height_ = other.height_;
		stride_ = other.stride_;
		flipped_ = other.flipped_;
		fmt_ = other.fmt_;
		other.alloc_ = false;
		other.data_ = NULL;
	}

	~GPUDebugBuffer() {
		Free();
	}

	GPUDebugBuffer &operator = (GPUDebugBuffer &&other) {
		if (this != &other) {
			Free();
			alloc_ = other.alloc_;
			data_ = other.data_;
			height_ = other.height_;
			stride_ = other.stride_;
			flipped_ = other.flipped_;
			fmt_ = other.fmt_;
			other.alloc_ = false;
			other.data_ = NULL;
		}

		return *this;
	}

	void Allocate(u32 stride, u32 height, GEBufferFormat fmt, bool flipped = false, bool reversed = false) {
		GPUDebugBufferFormat actualFmt = GPUDebugBufferFormat(fmt);
		if (reversed && actualFmt < GPU_DBG_FORMAT_8888) {
			actualFmt |= GPU_DBG_FORMAT_REVERSE_FLAG;
		}
		Allocate(stride, height, actualFmt, flipped);
	}

	void Allocate(u32 stride, u32 height, GPUDebugBufferFormat fmt, bool flipped = false) {
		if (alloc_ && stride_ == stride && height_ == height && fmt_ == fmt) {
			// Already allocated the right size.
			flipped_ = flipped;
			return;
		}

		Free();
		alloc_ = true;
		height_ = height;
		stride_ = stride;
		fmt_ = fmt;
		flipped_ = flipped;

		u32 pixelSize = 2;
		if (fmt == GPU_DBG_FORMAT_8888 || fmt == GPU_DBG_FORMAT_FLOAT) {
			pixelSize = 4;
		} else if (fmt == GPU_DBG_FORMAT_8BIT) {
			pixelSize = 1;
		}

		data_ = new u8[pixelSize * stride * height];
	}

	void Free() {
		if (alloc_ && data_ != NULL) {
			delete [] data_;
		}
		data_ = NULL;
	}

	u8 *GetData() const {
		return data_;
	}

	u32 GetHeight() const {
		return height_;
	}

	u32 GetStride() const {
		return stride_;
	}

	bool GetFlipped() const {
		return flipped_;
	}

	GPUDebugBufferFormat GetFormat() const {
		return fmt_;
	}

private:
	bool alloc_;
	u8 *data_;
	u32 stride_;
	u32 height_;
	GPUDebugBufferFormat fmt_;
	bool flipped_;
};

struct GPUDebugVertex {
	float u;
	float v;
	float x;
	float y;
	float z;
	u8 c[4];
};

class GPUDebugInterface {
public:
	virtual bool GetCurrentDisplayList(DisplayList &list) = 0;
	virtual std::vector<DisplayList> ActiveDisplayLists() = 0;
	virtual void ResetListPC(int listID, u32 pc) = 0;
	virtual void ResetListStall(int listID, u32 stall) = 0;
	virtual void ResetListState(int listID, DisplayListState state) = 0;

	GPUDebugOp DissassembleOp(u32 pc) {
		return DissassembleOp(pc, Memory::Read_U32(pc));
	}
	virtual GPUDebugOp DissassembleOp(u32 pc, u32 op) = 0;
	virtual std::vector<GPUDebugOp> DissassembleOpRange(u32 startpc, u32 endpc) = 0;

	// Enter/exit stepping mode.  Mainly for better debug stats on time taken.
	virtual void NotifySteppingEnter() = 0;
	virtual void NotifySteppingExit() = 0;

	virtual u32 GetRelativeAddress(u32 data) = 0;
	virtual u32 GetVertexAddress() = 0;
	virtual u32 GetIndexAddress() = 0;
	virtual GPUgstate GetGState() = 0;
	// Needs to be called from the GPU thread.
	// Calling from a separate thread (e.g. UI) may fail.
	virtual void SetCmdValue(u32 op) = 0;

	virtual bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
		return false;
	}

	// Needs to be called from the GPU thread, so on the same thread as a notification is fine.
	// Calling from a separate thread (e.g. UI) may fail.
	virtual bool GetCurrentFramebuffer(GPUDebugBuffer &buffer) {
		// False means unsupported.
		return false;
	}

	// Similar to GetCurrentFramebuffer().
	virtual bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
		return false;
	}

	// Similar to GetCurrentFramebuffer().
	virtual bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
		return false;
	}

	// Similar to GetCurrentFramebuffer(), with texture level specification.
	virtual bool GetCurrentTexture(GPUDebugBuffer &buffer, int level) {
		return false;
	}

	// TODO:
	// cached framebuffers / textures / vertices?
	// get content of specific framebuffer / texture?
	// vertex / texture decoding?
};
