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

#include <algorithm>

#include "Core/Reporting.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GLES/FramebufferManagerGLES.h"

void FramebufferManagerGLES::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	// Pixel size always 4 here because we always request float
	const u32 bufSize = vfb->z_stride * (h - y) * 4;
	const u32 z_address = vfb->z_address;
	const int packWidth = std::min(vfb->z_stride, std::min(x + w, (int)vfb->width));

	if (!convBuf_ || convBufSize_ < bufSize) {
		delete[] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

	draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_DEPTH_BIT, 0, y, packWidth, h, Draw::DataFormat::D32F, convBuf_, vfb->z_stride);

	int dstByteOffset = y * vfb->z_stride * sizeof(u16);
	u16 *depth = (u16 *)Memory::GetPointer(z_address + dstByteOffset);
	GLfloat *packed = (GLfloat *)convBuf_;

	int totalPixels = h == 1 ? packWidth : vfb->z_stride * h;
	for (int yp = 0; yp < h; ++yp) {
		int row_offset = vfb->z_stride * yp;
		for (int xp = 0; xp < packWidth; ++xp) {
			const int i = row_offset + xp;
			float scaled = FromScaledDepth(packed[i]);
			if (scaled <= 0.0f) {
				depth[i] = 0;
			} else if (scaled >= 65535.0f) {
				depth[i] = 65535;
			} else {
				depth[i] = (int)scaled;
			}
		}
	}
}
