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

#include <list>
#include <deque>

#include "GPU/GPUCommon.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/VertexDecoderDX9.h"
#include "GPU/Directx9/TransformPipelineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/helper/fbo.h"

namespace DX9 {

class ShaderManagerDX9;
class LinkedShaderDX9;

class DIRECTX9_GPU : public GPUCommon
{
public:
	DIRECTX9_GPU();
	~DIRECTX9_GPU();
	virtual void InitClear();
	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	virtual void CopyDisplayToOutput();
	virtual void BeginFrame();
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type);
	virtual bool PerformMemoryCopy(u32 dest, u32 src, int size);
	virtual bool PerformMemorySet(u32 dest, u8 v, int size);
	virtual bool PerformMemoryDownload(u32 dest, int size);
	virtual bool PerformMemoryUpload(u32 dest, int size);
	virtual bool PerformStencilUpload(u32 dest, int size);
	virtual void ClearCacheNextFrame();
	virtual void DeviceLost();  // Only happens on Android. Drop all textures and shaders.

	virtual void DumpNextFrame();
	virtual void DoState(PointerWrap &p);
	
	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	virtual void Resized();
	virtual bool DecodeTexture(u8* dest, GPUgstate state) {
		return textureCache_.DecodeTexture(dest, state);
	}
	virtual bool FramebufferDirty();
	virtual bool FramebufferReallyDirty();

	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}
	std::vector<FramebufferInfo> GetFramebufferList();

protected:
	virtual void FastRunLoop(DisplayList &list);
	virtual void ProcessEvent(GPUEvent ev);

private:
	void Flush() {
		transformDraw_.Flush();
	}
	void DoBlockTransfer();
	void ApplyDrawState(int prim);
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();
	void InitClearInternal();
	void BeginFrameInternal();
	void CopyDisplayToOutputInternal();
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);

	FramebufferManagerDX9 framebufferManager_;
	TextureCacheDX9 textureCache_;
	TransformDrawEngineDX9 transformDraw_;
	ShaderManagerDX9 *shaderManager_;

	u8 *commandFlags_;

	bool resized_;
	int lastVsync_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
};

};

typedef DX9::DIRECTX9_GPU DIRECTX9_GPU;
