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

#include "Common/GPU/thin3d.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"

class TextureCacheGLES;
class DrawEngineGLES;
class ShaderManagerGLES;
class GLRProgram;

class FramebufferManagerGLES : public FramebufferManagerCommon {
public:
	FramebufferManagerGLES(Draw::DrawContext *draw);
	~FramebufferManagerGLES();

	void NotifyDisplayResized() override;
	void DeviceLost() override;

	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

protected:
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	bool ReadbackDepthbufferSync(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride) override;
	bool ReadbackStencilbufferSync(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint8_t *pixels, int pixelsStride) override;

private:
	u8 *convBuf_ = nullptr;
	u32 convBufSize_ = 0;

	GLRProgram *depthDownloadProgram_ = nullptr;
	int u_depthDownloadTex = -1;
	int u_depthDownloadFactor = -1;
	int u_depthDownloadShift = -1;
	int u_depthDownloadTo8 = -1;
};
