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

#include "../GPUCommon.h"
#include "Framebuffer.h"
#include "VertexDecoder.h"
#include "TransformPipeline.h"
#include "TextureCache.h"
#include "gfx_es2/fbo.h"

class ShaderManager;
class LinkedShader;

class GLES_GPU : public GPUCommon
{
public:
	GLES_GPU();
	~GLES_GPU();
	virtual void InitClear();
	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void DrawSync(int mode);
	virtual void Continue();
	virtual void Break();
	virtual void EnableInterrupts(bool enable) {
		interruptsEnabled_ = enable;
	}

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, int format);
	virtual void CopyDisplayToOutput();
	virtual void BeginFrame();
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size);
	virtual void InvalidateCacheHint(u32 addr, int size);
	virtual void DeviceLost();  // Only happens on Android. Drop all textures and shaders.

	virtual void DumpNextFrame();
	virtual void Flush();
	virtual void DoState(PointerWrap &p);

	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	virtual void Resized();
	virtual bool DecodeTexture(u8* dest, GPUgstate state)
	{
		return textureCache_.DecodeTexture(dest, state);
	}
	virtual bool FramebufferDirty();

	std::vector<FramebufferInfo> GetFramebufferList();

private:
	void DoBlockTransfer();
	void ApplyDrawState(int prim);

	// Applies states for debugging if enabled.
	void BeginDebugDraw();
	void EndDebugDraw();

	FramebufferManager framebufferManager_;
	TextureCache textureCache_;
	TransformDrawEngine transformDraw_;
	ShaderManager *shaderManager_;

	u8 *flushBeforeCommand_;
	bool interruptsEnabled_;
	bool resized_;
};
