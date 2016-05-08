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

#include "GPUDebugInterface.h"

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GEBufferFormat fmt, bool flipped, bool reversed) {
	GPUDebugBufferFormat actualFmt = GPUDebugBufferFormat(fmt);
	if (reversed && actualFmt < GPU_DBG_FORMAT_8888) {
		actualFmt |= GPU_DBG_FORMAT_REVERSE_FLAG;
	}
	Allocate(stride, height, actualFmt, flipped);
}

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GPUDebugBufferFormat fmt, bool flipped) {
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

	u32 pixelSize = PixelSize(fmt);
	data_ = new u8[pixelSize * stride * height];
}

void GPUDebugBuffer::Free() {
	if (alloc_ && data_ != NULL) {
		delete [] data_;
	}
	data_ = NULL;
}

u32 GPUDebugBuffer::PixelSize(GPUDebugBufferFormat fmt) const {	
	switch (fmt) {
	case GPU_DBG_FORMAT_8888:
	case GPU_DBG_FORMAT_8888_BGRA:
	case GPU_DBG_FORMAT_FLOAT:
	case GPU_DBG_FORMAT_24BIT_8X:
	case GPU_DBG_FORMAT_24X_8BIT:
	case GPU_DBG_FORMAT_FLOAT_DIV_256:
	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
		return 4;

	case GPU_DBG_FORMAT_888_RGB:
		return 3;

	case GPU_DBG_FORMAT_8BIT:
		return 1;

	default:
		return 2;
	}
}

u32 GPUDebugBuffer::GetRawPixel(int x, int y) const {
	if (data_ == nullptr) {
		return 0;
	}

	if (flipped_) {
		y = height_ - y - 1;
	}

	u32 pixelSize = PixelSize(fmt_);
	u32 byteOffset = pixelSize * (stride_ * y + x);
	const u8 *ptr = &data_[byteOffset];

	switch (pixelSize) {
	case 4:
		return *(const u32 *)ptr;
	case 3:
		return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
	case 2:
		return *(const u16 *)ptr;
	case 1:
		return *(const u8 *)ptr;
	default:
		return 0;
	}
}
