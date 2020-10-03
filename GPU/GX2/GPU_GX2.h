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
#include <wiiu/gx2.h>

#include "GPU/GPUCommon.h"
#include "GPU/GX2/DrawEngineGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/DepalettizeShaderGX2.h"
#include "GPU/Common/VertexDecoderCommon.h"

class FramebufferManagerGX2;
class ShaderManagerGX2;
class LinkedShaderGX2;

class GPU_GX2 : public GPUCommon {
public:
	GPU_GX2(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_GX2();

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
	void CopyDisplayToOutput(bool reallyDirty) override;

	GX2ContextState *context_;

	FramebufferManagerGX2 *framebufferManagerGX2_;
	TextureCacheGX2 *textureCacheGX2_;
	DepalShaderCacheGX2 *depalShaderCache_;
	DrawEngineGX2 drawEngine_;
	ShaderManagerGX2 *shaderManagerGX2_;

	int vertexCost_ = 0;
};
