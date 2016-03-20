// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY{} without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.


#include "GPU/Vulkan/DepalettizeShaderVulkan.h"

DepalShaderCacheVulkan::DepalShaderCacheVulkan() {

}

DepalShaderCacheVulkan::~DepalShaderCacheVulkan() {

}

DepalShaderVulkan *DepalShaderCacheVulkan::GetDepalettizeShader(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat) {
	return nullptr;
}

VulkanTexture *DepalShaderCacheVulkan::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut) {
	return nullptr;
}

void DepalShaderCacheVulkan::Clear() {}

void DepalShaderCacheVulkan::Decimate() {}

u32 DepalShaderCacheVulkan::GenerateShaderID(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat) {
	return 0;
}

bool DepalShaderCacheVulkan::CreateVertexShader() {
	return false;
}
