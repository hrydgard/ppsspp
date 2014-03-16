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

#include "gfx_es2/fbo.h"

#include "GPU/GPUCommon.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TransformPipeline.h"
#include "GPU/GLES/TextureCache.h"

class ShaderManager;
class LinkedShader;

class GLES_GPU : public GPUCommon {
public:
	GLES_GPU();
	~GLES_GPU();
	virtual void InitClear();
	virtual void PreExecuteOp(u32 op, u32 diff);
	void ExecuteOpInternal(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	virtual void CopyDisplayToOutput();
	virtual void BeginFrame();
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type);
	virtual void UpdateMemory(u32 dest, u32 src, int size);
	virtual void ClearCacheNextFrame();
	virtual void DeviceLost();  // Only happens on Android. Drop all textures and shaders.

	virtual void DumpNextFrame();
	virtual void DoState(PointerWrap &p);

	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	virtual void Resized();
	virtual void ClearShaderCache();
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

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentTexture(GPUDebugBuffer &buffer);
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name);

protected:
	virtual void FastRunLoop(DisplayList &list);
	virtual void ProcessEvent(GPUEvent ev);
	virtual void FastLoadBoneMatrix(u32 target);

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

	FramebufferManager framebufferManager_;
	TextureCache textureCache_;
	TransformDrawEngine transformDraw_;
	ShaderManager *shaderManager_;

	u8 *commandFlags_;

	bool resized_;
	int lastVsync_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
};
