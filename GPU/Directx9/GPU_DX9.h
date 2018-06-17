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
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DepalettizeShaderDX9.h"
#include "GPU/Common/VertexDecoderCommon.h"

namespace DX9 {

class ShaderManagerDX9;
class LinkedShaderDX9;

class GPU_DX9 : public GPUCommon {
public:
	GPU_DX9(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_DX9();

	void CheckGPUFeatures() override;
	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

	void ReapplyGfxState() override;
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void GetStats(char *buffer, size_t bufsize) override;
	void ClearCacheNextFrame() override;
	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.
	void DeviceRestore() override;

	void DoState(PointerWrap &p) override;

	void ClearShaderCache() override;

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void BeginHostFrame() override;

protected:
	void FinishDeferred() override;

private:
	void Flush() {
		drawEngine_.Flush();
	}
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();

	void InitClear() override;
	void BeginFrame() override;
	void CopyDisplayToOutput() override;

	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;

	FramebufferManagerDX9 *framebufferManagerDX9_;
	TextureCacheDX9 *textureCacheDX9_;
	DepalShaderCacheDX9 depalShaderCache_;
	DrawEngineDX9 drawEngine_;
	ShaderManagerDX9 *shaderManagerDX9_;

	int lastVsync_;
};

}  // namespace DX9

typedef DX9::GPU_DX9 DIRECTX9_GPU;
