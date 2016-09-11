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

#include "GPU/GPUState.h"
#include "GPU/GPUCommon.h"

class ShaderManager;

class NullGPU : public GPUCommon {
public:
	NullGPU();
	~NullGPU();
	void InitClear() override {}
	void ExecuteOp(u32 op, u32 diff) override;

	void BeginFrame() override {}
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override {}
	void CopyDisplayToOutput() override {}
	void GetStats(char *buffer, size_t bufsize) override;
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;
	void NotifyVideoUpload(u32 addr, int size, int width, int format) override;
	bool PerformMemoryCopy(u32 dest, u32 src, int size) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformMemoryDownload(u32 dest, int size) override;
	bool PerformMemoryUpload(u32 dest, int size) override;
	bool PerformStencilUpload(u32 dest, int size) override;
	void ClearCacheNextFrame() override {}

	void DeviceLost() override {}
	void DeviceRestore() override {}
	void DumpNextFrame() override {}

	void Resized() override {}
	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = "NULL";
		fullInfo = "NULL";
	}

	bool FramebufferReallyDirty() override;

protected:
	void FastRunLoop(DisplayList &list) override;
};
