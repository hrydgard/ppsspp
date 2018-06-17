// Copyright (c) 2017- PPSSPP Project.

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
#include <d3d11.h>

#include "GPU/GPUCommon.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DepalettizeShaderD3D11.h"
#include "GPU/Common/VertexDecoderCommon.h"

class FramebufferManagerD3D11;
class ShaderManagerD3D11;
class LinkedShaderD3D11;

class GPU_D3D11 : public GPUCommon {
public:
	GPU_D3D11(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_D3D11();

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
	void EndHostFrame() override;

protected:
	void FinishDeferred() override;

private:
	void Flush() {
		drawEngine_.Flush();
	}
	// void ApplyDrawState(int prim);
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();

	void InitClear() override;
	void BeginFrame() override;
	void CopyDisplayToOutput() override;

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	FramebufferManagerD3D11 *framebufferManagerD3D11_;
	TextureCacheD3D11 *textureCacheD3D11_;
	DepalShaderCacheD3D11 *depalShaderCache_;
	DrawEngineD3D11 drawEngine_;
	ShaderManagerD3D11 *shaderManagerD3D11_;

	int lastVsync_;
};
