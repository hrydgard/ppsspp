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
#include <list>
#include <string>

#include "Common/Math/expression_parser.h"
#include "Core/MemMap.h"
#include "GPU/GPU.h"
#include "GPU/GPUDefinitions.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Debugger/Debugger.h"

class FramebufferManagerCommon;
class TextureCacheCommon;

struct VirtualFramebuffer;
struct DisplayList;

namespace GPURecord {
class Recorder;
}
class GPUBreakpoints;

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

	// 565 is just reversed, the others have B and R swapped.
	GPU_DBG_FORMAT_565_BGRA = 0x04,
	GPU_DBG_FORMAT_BRSWAP_FLAG = 0x08,
	GPU_DBG_FORMAT_5551_BGRA = 0x09,
	GPU_DBG_FORMAT_4444_BGRA = 0x0A,
	GPU_DBG_FORMAT_8888_BGRA = 0x0B,

	// These don't, they're for depth/stencil buffers.
	GPU_DBG_FORMAT_FLOAT = 0x10,
	GPU_DBG_FORMAT_16BIT = 0x11,
	GPU_DBG_FORMAT_8BIT = 0x12,
	GPU_DBG_FORMAT_24BIT_8X = 0x13,
	GPU_DBG_FORMAT_24X_8BIT = 0x14,

	GPU_DBG_FORMAT_FLOAT_DIV_256 = 0x18,
	GPU_DBG_FORMAT_24BIT_8X_DIV_256 = 0x1B,

	// This is used for screenshots, mainly.
	GPU_DBG_FORMAT_888_RGB = 0x20,
};

enum GPUDebugFramebufferType {
	// The current render target.
	GPU_DBG_FRAMEBUF_RENDER,
	// The current display target (not the displayed screen, though.)
	GPU_DBG_FRAMEBUF_DISPLAY,
};

inline GPUDebugBufferFormat &operator |=(GPUDebugBufferFormat &lhs, const GPUDebugBufferFormat &rhs) {
	lhs = GPUDebugBufferFormat((int)lhs | (int)rhs);
	return lhs;
}

struct GPUDebugBuffer {
	GPUDebugBuffer() {
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

	GPUDebugBuffer(GPUDebugBuffer &&other) noexcept {
		alloc_ = other.alloc_;
		data_ = other.data_;
		height_ = other.height_;
		stride_ = other.stride_;
		flipped_ = other.flipped_;
		fmt_ = other.fmt_;
		other.alloc_ = false;
		other.data_ = nullptr;
	}

	~GPUDebugBuffer() {
		Free();
	}

	GPUDebugBuffer &operator = (GPUDebugBuffer &&other) noexcept {
		if (this != &other) {
			Free();
			alloc_ = other.alloc_;
			data_ = other.data_;
			height_ = other.height_;
			stride_ = other.stride_;
			flipped_ = other.flipped_;
			fmt_ = other.fmt_;
			other.alloc_ = false;
			other.data_ = nullptr;
		}

		return *this;
	}

	void Allocate(u32 stride, u32 height, GEBufferFormat fmt, bool flipped = false, bool reversed = false);
	void Allocate(u32 stride, u32 height, GPUDebugBufferFormat fmt, bool flipped = false);
	void Free();

	void ZeroBytes();

	u32 GetRawPixel(int x, int y) const;
	void SetRawPixel(int x, int y, u32 c);

	u8 *GetData() {
		return data_;
	}

	const u8 *GetData() const {
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

	u32 PixelSize() const;

	void SetIsBackbuffer(bool isBackBuffer) { isBackBuffer_ = isBackBuffer; }
	bool IsBackBuffer() const { return isBackBuffer_; }

private:
	bool alloc_ = false;
	u8 *data_ = nullptr;
	u32 stride_ = 0;
	u32 height_ = 0;
	GPUDebugBufferFormat fmt_ = GPU_DBG_FORMAT_INVALID;
	bool flipped_ = false;
	bool isBackBuffer_ = false;
};

struct GPUDebugVertex {
	float u;
	float v;
	float x;
	float y;
	float z;
	u8 c[4];
	float nx;
	float ny;
	float nz;
};

class GPUDebugInterface {
public:
	virtual ~GPUDebugInterface() = default;
	virtual bool GetCurrentDisplayList(DisplayList &list) = 0;
	virtual int GetCurrentPrimCount() = 0;
	virtual std::vector<DisplayList> ActiveDisplayLists() = 0;
	virtual void ResetListPC(int listID, u32 pc) = 0;
	virtual void ResetListStall(int listID, u32 stall) = 0;
	virtual void ResetListState(int listID, DisplayListState state) = 0;

	virtual GPUDebugOp DisassembleOp(u32 pc, u32 op) = 0;
	virtual std::vector<GPUDebugOp> DisassembleOpRange(u32 startpc, u32 endpc) = 0;

	virtual u32 GetRelativeAddress(u32 data) = 0;
	virtual u32 GetVertexAddress() = 0;
	virtual u32 GetIndexAddress() = 0;
	virtual const GPUgstate &GetGState() = 0;
	// Needs to be called from the GPU thread.
	// Calling from a separate thread (e.g. UI) may fail.
	virtual void SetCmdValue(u32 op) = 0;
	virtual void Flush() = 0;

	virtual void GetStats(char *buffer, size_t bufsize) = 0;

	virtual uint32_t SetAddrTranslation(uint32_t value) = 0;
	virtual uint32_t GetAddrTranslation() = 0;
	
	// TODO: Make a proper debug interface instead of accessing directly?
	virtual FramebufferManagerCommon *GetFramebufferManagerCommon() = 0;
	virtual TextureCacheCommon *GetTextureCacheCommon() = 0;

	virtual std::vector<const VirtualFramebuffer *> GetFramebufferList() const = 0;

	virtual std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) = 0;
	virtual std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) = 0;
	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name) = 0;
	virtual const std::list<int> &GetDisplayListQueue() = 0;
	virtual const DisplayList &GetDisplayList(int index) = 0;

	virtual int PrimsThisFrame() const = 0;
	virtual int PrimsLastFrame() const = 0;

	virtual void ClearBreakNext() = 0;
	virtual void SetBreakNext(GPUDebug::BreakNext next) = 0 ;
	virtual void SetBreakCount(int c, bool relative = false) = 0 ;
	virtual GPUDebug::BreakNext GetBreakNext() const = 0;
	virtual int GetBreakCount() const = 0;
	virtual bool SetRestrictPrims(std::string_view rule) = 0 ;
	virtual std::string_view GetRestrictPrims() = 0;

	virtual GPURecord::Recorder *GetRecorder() = 0;
	virtual GPUBreakpoints *GetBreakpoints() = 0;

	virtual bool GetCurrentDrawAsDebugVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
		return false;
	}

	// Needs to be called from the GPU thread, so on the same thread as a notification is fine.
	// Calling from a separate thread (e.g. UI) may fail.
	virtual bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes = -1) {
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
	virtual bool GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
		return false;
	}

	virtual bool GetCurrentClut(GPUDebugBuffer &buffer) {
		return false;
	}

	virtual bool GetOutputFramebuffer(GPUDebugBuffer &buffer) {
		return false;
	}
};

bool GPUDebugInitExpression(GPUDebugInterface *g, const char *str, PostfixExpression &exp);
bool GPUDebugExecExpression(GPUDebugInterface *g, PostfixExpression &exp, uint32_t &result);
bool GPUDebugExecExpression(GPUDebugInterface *g, const char *str, uint32_t &result);
