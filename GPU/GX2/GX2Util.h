// Copyright (c) 2017- PPSSPP Project.

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

#include <ctype.h>
#include <cstdint>
#include <wiiu/gx2.h>
#include <wiiu/os/memory.h>
#include <Common/Log.h>

#define aligndown(x, y)		((x) & ~((y) - 1))
#define alignup(x, y)		aligndown(((x) + ((y) - 1)), y)

class PushBufferGX2 {
public:
	PushBufferGX2(u32 size, u32 align, GX2InvalidateMode mode) : size_(alignup(size, align)), align_(align), mode_(mode) {
		buffer_ = (u8 *)MEM2_alloc(size_, align_);
	}
	PushBufferGX2(PushBufferGX2 &) = delete;
	~PushBufferGX2() {
		MEM2_free(buffer_);
	}
	void *Buf() const {
		return buffer_;
	}

	// Should be done each frame
	void Reset() {
		pos_ = 0;
		push_size_ = 0;
	}

	u8 *BeginPush(u32 *offset, u32 size) {
		size = alignup(size, align_);
		_assert_(size <= size_);
		if (pos_ + push_size_ + size > size_) {
			// Wrap! Note that with this method, since we return the same buffer as before, you have to do the draw immediately after.
			EndPush();
			pos_ = 0;
		}
		if(offset)
			*offset = pos_;
		push_size_ += size;
		return (u8 *)buffer_ + pos_ + push_size_ - size;
	}
	void EndPush() {
		if(push_size_) {
			GX2Invalidate(mode_, buffer_ + pos_, push_size_);
			pos_ += push_size_;
			push_size_ = 0;
		}
	}

private:
	u32 size_;
	u32 align_;
	GX2InvalidateMode mode_;
	u8 *buffer_;
	u32 pos_ = 0;
	u32 push_size_ = 0;
};

class StockGX2 {
public:
	static void Init();
	static GX2DepthStencilControlReg depthStencilDisabled;
	static GX2DepthStencilControlReg depthDisabledStencilWrite;
	static GX2TargetChannelMaskReg TargetChannelMasks[16];
	static GX2StencilMaskReg stencilMask;
	static GX2ColorControlReg blendDisabledColorWrite;
	static GX2ColorControlReg blendColorDisabled;
	static GX2Sampler samplerPoint2DWrap;
	static GX2Sampler samplerLinear2DWrap;
	static GX2Sampler samplerPoint2DClamp;
	static GX2Sampler samplerLinear2DClamp;
};
