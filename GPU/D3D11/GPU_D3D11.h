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
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DepalettizeShaderD3D11.h"
#include "GPU/Common/VertexDecoderCommon.h"

class ShaderManagerD3D11;
class LinkedShaderD3D11;

class GPU_D3D11 : public GPUCommon {
public:
	GPU_D3D11(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_D3D11();

	void CheckGPUFeatures();
	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

	void ReapplyGfxStateInternal() override;
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void BeginFrame() override;
	void GetStats(char *buffer, size_t bufsize) override;
	void ClearCacheNextFrame() override;
	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.
	void DeviceRestore() override;

	void DumpNextFrame() override;
	void DoState(PointerWrap &p) override;

	void ClearShaderCache() override;
	bool DecodeTexture(u8 *dest, const GPUgstate &state) override {
		return false;
	}
	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}
	std::vector<FramebufferInfo> GetFramebufferList() override;

	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;

	typedef void (GPU_D3D11::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		uint64_t flags;
		GPU_D3D11::CmdFunc func;
	};

	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_VertexType(u32 op, u32 diff);
	void Execute_VertexTypeSkinning(u32 op, u32 diff);
	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void EndHostFrame() override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void FinishDeferred() override;

private:
	void UpdateCmdInfo();

	void Flush() {
		drawEngine_.Flush();
	}
	// void ApplyDrawState(int prim);
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();

	void InitClearInternal() override;
	void BeginFrameInternal() override;
	void CopyDisplayToOutputInternal() override;

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	FramebufferManagerD3D11 *framebufferManagerD3D11_;
	TextureCacheD3D11 *textureCacheD3D11_;
	DepalShaderCacheD3D11 *depalShaderCache_;
	DrawEngineD3D11 drawEngine_;
	ShaderManagerD3D11 *shaderManagerD3D11_;

	static CommandInfo cmdInfo_[256];

	int lastVsync_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
};