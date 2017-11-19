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
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/DepalettizeShaderGLES.h"
#include "GPU/GLES/FragmentTestCacheGLES.h"

class ShaderManagerGLES;
class LinkedShader;

class GPU_GLES : public GPUCommon {
public:
	GPU_GLES(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_GLES();

	// This gets called on startup and when we get back from settings.
	void CheckGPUFeatures();

	bool IsReady() override;

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
	void CleanupBeforeUI() override;
	bool DecodeTexture(u8 *dest, const GPUgstate &state) override {
		return textureCacheGL_->DecodeTexture(dest, state);
	}
	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}

	typedef void (GPU_GLES::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		uint64_t flags;
		GPU_GLES::CmdFunc func;
	};

	void Execute_Prim(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void BeginHostFrame() override;
	void EndHostFrame() override;

protected:
	void FastRunLoop(DisplayList &list) override;
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
	void Reinitialize() override;

	inline void UpdateVsyncInterval(bool force);
	void UpdateCmdInfo();

	static CommandInfo cmdInfo_[256];

	FramebufferManagerGLES *framebufferManagerGL_;
	TextureCacheGLES *textureCacheGL_;
	DepalShaderCacheGLES depalShaderCache_;
	DrawEngineGLES drawEngine_;
	FragmentTestCacheGLES fragmentTestCache_;
	ShaderManagerGLES *shaderManagerGL_;

#ifdef _WIN32
	int lastVsync_;
#endif
	int vertexCost_ = 0;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
	std::string shaderCachePath_;
};
