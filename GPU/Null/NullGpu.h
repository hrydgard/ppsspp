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

#include "../GPUCommon.h"

class ShaderManager;

class NullGPU : public GPUCommon
{
public:
	NullGPU();
	~NullGPU();
	virtual void InitClear() {}
	virtual void ExecuteOp(u32 op, u32 diff);
	virtual u32  DrawSync(int mode);

	virtual void BeginFrame() {}
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) {}
	virtual void CopyDisplayToOutput() {}
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type);
	virtual void UpdateMemory(u32 dest, u32 src, int size);
	virtual void ClearCacheNextFrame() {};
	virtual void Flush() {}

	virtual void DeviceLost() {}
	virtual void DumpNextFrame() {}

	virtual void Resized() {}
	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
		primaryInfo = "NULL";
		fullInfo = "NULL";
	}

protected:
	virtual void FastRunLoop(DisplayList &list);
};
