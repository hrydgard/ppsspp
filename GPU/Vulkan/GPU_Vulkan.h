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
	void CheckGPUFeatures() override;

	bool IsReady() override;
	void CancelReady() override;

	// These are where we can reset command buffers etc.
	void BeginHostFrame() override;
	void EndHostFrame() override;

	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

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

	TextureCacheVulkan *GetTextureCache() {
		return textureCacheVulkan_;
	}

protected:
	void FinishDeferred() override;

private:
	void Flush() {
		drawEngine_.Flush();
	}
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();
	void InitClear() override;
	void CopyDisplayToOutput() override;
	void Reinitialize() override;
	inline void UpdateVsyncInterval(bool force);

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void LoadCache(std::string filename);
	void SaveCache(std::string filename);

	VulkanContext *vulkan_;
	FramebufferManagerVulkan *framebufferManagerVulkan_;
	TextureCacheVulkan *textureCacheVulkan_;
	DepalShaderCacheVulkan depalShaderCache_;
	DrawEngineVulkan drawEngine_;

	// Manages shaders and UBO data
	ShaderManagerVulkan *shaderManagerVulkan_;

	// Manages state and pipeline objects
	PipelineManagerVulkan *pipelineManager_;

	// Simple 2D drawing engine.
	Vulkan2D vulkan2D_;

	struct FrameData {
		VulkanPushBuffer *push_;
	};

	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES]{};

	std::string shaderCachePath_;
	bool shaderCacheLoaded_ = false;
};
