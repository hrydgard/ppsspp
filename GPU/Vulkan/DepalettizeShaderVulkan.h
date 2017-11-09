// Copyright (c) 2014- PPSSPP Project.

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

#include <map>

#include "Common/CommonTypes.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "GPU/ge_constants.h"
#include "thin3d/thin3d.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

class DepalShaderVulkan {
public:
	~DepalShaderVulkan() {
		delete[] code;
	}
	// A Vulkan2D pipeline. Set texture to slot 0 and palette texture to slot 1.
	VkPipeline pipeline = VK_NULL_HANDLE;
	const char *code = nullptr;
};

class DepalTextureVulkan {
public:
	VulkanTexture *texture = nullptr;
	int lastFrame;
};

class VulkanTexture;
class Vulkan2D;

// Caches both shaders and palette textures.
// Could even avoid bothering with palette texture and just use uniform data...
class DepalShaderCacheVulkan : public DepalShaderCacheCommon {
public:
	DepalShaderCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~DepalShaderCacheVulkan();
	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw, VulkanContext *vulkan);

	// This also uploads the palette and binds the correct texture.
	DepalShaderVulkan *GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat);
	VulkanTexture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut);
	void Clear();
	void Decimate();

	void SetVulkan2D(Vulkan2D *vk2d) { vulkan2D_ = vk2d; }
	void SetPushBuffer(VulkanPushBuffer *push) { push_ = push; }
	void SetAllocator(VulkanDeviceAllocator *alloc) { alloc_ = alloc; }
	void SetVShader(VkShaderModule vshader) { vshader_ = vshader; }

private:
	Draw::DrawContext *draw_ = nullptr;
	VulkanContext *vulkan_ = nullptr;
	VulkanPushBuffer *push_ = nullptr;
	VulkanDeviceAllocator *alloc_ = nullptr;
	VkShaderModule vshader_ = VK_NULL_HANDLE;
	Vulkan2D *vulkan2D_ = nullptr;

	// GLuint vertexShader_;
	std::map<u32, DepalShaderVulkan *> cache_;
	std::map<u32, DepalTextureVulkan *> texCache_;
};

