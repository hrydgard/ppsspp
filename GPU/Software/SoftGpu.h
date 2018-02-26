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
#include "GPU/Common/GPUDebugInterface.h"
#include "thin3d/thin3d.h"

struct FormatBuffer {
	FormatBuffer() { data = nullptr; }
	union {
		u8 *data;
		u16 *as16;
		u32 *as32;
	};

	inline void Set16(int x, int y, int stride, u16 v) {
		as16[x + y * stride] = v;
	}

	inline void Set32(int x, int y, int stride, u32 v) {
		as32[x + y * stride] = v;
	}

	inline u16 Get16(int x, int y, int stride) {
		return as16[x + y * stride];
	}

	inline u32 Get32(int x, int y, int stride) {
		return as32[x + y * stride];
	}
};

class SoftwareDrawEngine;

class SoftGPU : public GPUCommon {
public:
	SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *_thin3D);
	~SoftGPU();

	void CheckGPUFeatures() override {}
	void InitClear() override {}
	void ExecuteOp(u32 op, u32 diff) override;

	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void CopyDisplayToOutput() override;
	void GetStats(char *buffer, size_t bufsize) override;
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;
	void NotifyVideoUpload(u32 addr, int size, int width, int format) override;
	bool PerformMemoryCopy(u32 dest, u32 src, int size) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformMemoryDownload(u32 dest, int size) override;
	bool PerformMemoryUpload(u32 dest, int size) override;
	bool PerformStencilUpload(u32 dest, int size) override;
	void ClearCacheNextFrame() override {}

	void DeviceLost() override;
	void DeviceRestore() override;

	void Resized() override {}
	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = "Software";
		fullInfo = "Software";
	}

	bool FramebufferDirty() override;

	bool FramebufferReallyDirty() override {
		return !(gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME);
	}

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes = -1) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight);

private:
	bool framebufferDirty_;
	u32 displayFramebuf_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;

	SoftwareDrawEngine *drawEngine_ = nullptr;

	Draw::Texture *fbTex;
	Draw::Pipeline *texColor;
	std::vector<u32> fbTexBuffer;

	Draw::SamplerState *samplerNearest = nullptr;
	Draw::SamplerState *samplerLinear = nullptr;
	Draw::Buffer *vdata = nullptr;
	Draw::Buffer *idata = nullptr;
};

// TODO: These shouldn't be global.
extern u32 clut[4096];
extern FormatBuffer fb;
extern FormatBuffer depthbuf;
