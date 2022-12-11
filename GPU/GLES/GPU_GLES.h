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

#include <string>
#include <vector>

#include "Common/File/Path.h"

#include "GPU/GPUCommon.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/FragmentTestCacheGLES.h"

class ShaderManagerGLES;
class TextureCacheGLES;
class LinkedShader;

class GPU_GLES : public GPUCommon {
public:
	GPU_GLES(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_GLES();

	// This gets called on startup and when we get back from settings.
	u32 CheckGPUFeatures() const override;

	bool IsReady() override;
	void CancelReady() override;

	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

	void ReapplyGfxState() override;
	void GetStats(char *buffer, size_t bufsize) override;

	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.
	void DeviceRestore() override;

	void DoState(PointerWrap &p) override;

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void BeginHostFrame() override;
	void EndHostFrame() override;

protected:
	void FinishDeferred() override;

private:
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo() override;

	void InitClear() override;
	void BeginFrame() override;
	void CopyDisplayToOutput(bool reallyDirty) override;
	void Reinitialize() override;

	FramebufferManagerGLES *framebufferManagerGL_;
	TextureCacheGLES *textureCacheGL_;
	DrawEngineGLES drawEngine_;
	FragmentTestCacheGLES fragmentTestCache_;
	ShaderManagerGLES *shaderManagerGL_;

	Path shaderCachePath_;
};
