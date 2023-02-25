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

#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"

class FramebufferManagerDX9 : public FramebufferManagerCommon {
public:
	FramebufferManagerDX9(Draw::DrawContext *draw);

protected:
	// TODO: The non-color path of FramebufferManagerCommon::ReadbackDepthbufferSync seems to work just as well.
	bool ReadbackDepthbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride, int destW, int destH, Draw::ReadbackMode mode) override;
};
