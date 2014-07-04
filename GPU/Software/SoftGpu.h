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

typedef struct {
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
} FormatBuffer;

class ShaderManager;

class SoftGPU : public GPUCommon
{
public:
	SoftGPU();
	~SoftGPU();
	virtual void InitClear() {}
	virtual void ExecuteOp(u32 op, u32 diff);

	virtual void BeginFrame() {}
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	virtual void CopyDisplayToOutput();
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type);
	virtual bool PerformMemoryCopy(u32 dest, u32 src, int size);
	virtual bool PerformMemorySet(u32 dest, u8 v, int size);
	virtual bool PerformMemoryDownload(u32 dest, int size);
	virtual bool PerformMemoryUpload(u32 dest, int size);
	virtual bool PerformStencilUpload(u32 dest, int size);
	virtual void ClearCacheNextFrame() {};

	virtual void DeviceLost() {}
	virtual void DumpNextFrame() {}

	virtual void Resized() {}
	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
		primaryInfo = "Software";
		fullInfo = "Software";
	}

	virtual bool FramebufferDirty();

	virtual bool FramebufferReallyDirty() {
		return !(gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME);
	}

	virtual bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	virtual bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	virtual bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	virtual bool GetCurrentTexture(GPUDebugBuffer &buffer, int level);
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

protected:
	virtual void FastRunLoop(DisplayList &list);
	virtual void ProcessEvent(GPUEvent ev);
	void CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight);

private:
	void CopyDisplayToOutputInternal();

	bool framebufferDirty_;
	u32 displayFramebuf_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;
};
