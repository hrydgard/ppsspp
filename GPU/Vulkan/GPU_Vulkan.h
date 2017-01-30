// Copyright (c) 2015- PPSSPP Project.

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
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"

class FramebufferManagerVulkan;
class ShaderManagerVulkan;
class LinkedShader;

class GPU_Vulkan : public GPUCommon {
public:
	GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_Vulkan();

	// This gets called on startup and when we get back from settings.
	void CheckGPUFeatures();

	// These are where we can reset command buffers etc.
	void BeginHostFrame() override;
	void EndHostFrame() override;

	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

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

	typedef void (GPU_Vulkan::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		uint64_t flags;
		GPU_Vulkan::CmdFunc func;
	};
	
	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_VertexType(u32 op, u32 diff);
	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);
	void Execute_BoneMtxNum(u32 op, u32 diff);
	void Execute_BoneMtxData(u32 op, u32 diff);

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;
	std::vector<FramebufferInfo> GetFramebufferList() override;
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;
	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void FastLoadBoneMatrix(u32 target) override;
	void FinishDeferred() override;

private:
	void Flush() {
		drawEngine_.Flush(nullptr);
	}
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();
	void InitClearInternal() override;
	void BeginFrameInternal() override;
	void CopyDisplayToOutputInternal() override;
	void ReinitializeInternal() override;
	inline void UpdateVsyncInterval(bool force);
	void UpdateCmdInfo();
	static CommandInfo cmdInfo_[256];

	VulkanContext *vulkan_;
	FramebufferManagerVulkan *framebufferManagerVulkan_;
	TextureCacheVulkan *textureCacheVulkan_;
	DepalShaderCacheVulkan depalShaderCache_;
	DrawEngineVulkan drawEngine_;

	// Manages shaders and UBO data
	ShaderManagerVulkan *shaderManagerVulkan_;

	// Manages state and pipeline objects
	PipelineManagerVulkan *pipelineManager_;

	int lastVsync_;
	VkCommandBuffer curCmd_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
};
