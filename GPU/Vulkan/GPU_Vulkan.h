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

#include <string>
#include <vector>
#include <thread>

#include "Common/File/Path.h"

#include "GPU/GPUCommonHW.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Common/TextureShaderCommon.h"

class FramebufferManagerVulkan;
class ShaderManagerVulkan;
class LinkedShader;
class TextureCacheVulkan;

class GPU_Vulkan : public GPUCommonHW {
public:
	GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPU_Vulkan();

	// This gets called on startup and when we get back from settings.
	u32 CheckGPUFeatures() const override;

	// These are where we can reset command buffers etc.
	void BeginHostFrame() override;
	void EndHostFrame() override;

	void GetStats(char *buffer, size_t bufsize) override;
	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.
	void DeviceRestore(Draw::DrawContext *draw) override;

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	TextureCacheVulkan *GetTextureCache() {
		return textureCacheVulkan_;
	}

protected:
	void FinishDeferred() override;
	void CheckRenderResized() override;

private:
	void BuildReportingInfo() override;

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void LoadCache(const Path &filename);
	void SaveCache(const Path &filename);

	FramebufferManagerVulkan *framebufferManagerVulkan_;
	TextureCacheVulkan *textureCacheVulkan_;
	DrawEngineVulkan drawEngine_;

	// Manages shaders and UBO data
	ShaderManagerVulkan *shaderManagerVulkan_;

	// Manages state and pipeline objects
	PipelineManagerVulkan *pipelineManager_;

	Path shaderCachePath_;
};
