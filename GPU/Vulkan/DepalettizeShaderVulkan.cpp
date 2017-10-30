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

#include "Common/Vulkan/VulkanContext.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DepalettizeShaderCommon.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/Vulkan/VulkanImage.h"

static VkFormat GetClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
	case GE_CMODE_16BIT_ABGR5551:
		return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
	case GE_CMODE_16BIT_BGR5650:
		return VK_FORMAT_R5G6B5_UNORM_PACK16;
	case GE_CMODE_32BIT_ABGR8888:
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	return VK_FORMAT_UNDEFINED;
}

DepalShaderCacheVulkan::DepalShaderCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan)
	: draw_(draw), vulkan_(vulkan) {

}

DepalShaderCacheVulkan::~DepalShaderCacheVulkan() {

}

DepalShaderVulkan *DepalShaderCacheVulkan::GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second;
	}

	char *buffer = new char[2048];

	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::FRAMEBUFFER_RENDERPASS);

	GenerateDepalShader(buffer, pixelFormat, GLSL_VULKAN);

	std::string error;
	VkShaderModule fshader = CompileShaderModule(vulkan_, VK_SHADER_STAGE_FRAGMENT_BIT, buffer, &error);
	if (fshader == VK_NULL_HANDLE) {
		return nullptr;
	}

	VkPipeline pipeline = vulkan2D_->GetPipeline(rp, vshader_, fshader);
	// Can delete the shader module now that the pipeline has been created.
	// Maybe don't even need to queue it..
	vulkan_->Delete().QueueDeleteShaderModule(fshader);

	DepalShaderVulkan *depal = new DepalShaderVulkan();
	depal->pipeline = pipeline;
	depal->code = buffer;
	cache_[id] = depal;
	return nullptr;
}

VulkanTexture *DepalShaderCacheVulkan::GetClutTexture(GEPaletteFormat clutFormat, u32 clutID, u32 *rawClut) {
	const u32 realClutID = clutID ^ clutFormat;

	auto oldtex = texCache_.find(realClutID);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	VkFormat destFormat = GetClutDestFormat(clutFormat);
	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	VkBuffer pushBuffer;
	uint32_t pushOffset = push_->PushAligned(rawClut, 1024, 4, &pushBuffer);

	VulkanTexture *vktex = new VulkanTexture(vulkan_, alloc_);
	vktex->CreateDirect(cmd_, texturePixels, 1, 1, destFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, nullptr);
	vktex->UploadMip(cmd_, 0, texturePixels, 1, pushBuffer, pushOffset, texturePixels);
	vktex->EndCreate(cmd_);

	DepalTextureVulkan *tex = new DepalTextureVulkan();
	tex->texture = vktex;
	tex->lastFrame = gpuStats.numFlips;
	texCache_[realClutID] = tex;
	return tex->texture;
}

void DepalShaderCacheVulkan::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		// Delete the shader/pipeline too.
		delete shader->second;
	}
	cache_.clear();

	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		delete tex->second->texture;
		delete tex->second;
	}
	texCache_.clear();

	// TODO: Delete vertex shader.
}

void DepalShaderCacheVulkan::Decimate() {
	// We don't bother decimating the generated shaders, there are never very many of them.
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			delete tex->second->texture;
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

u32 DepalShaderCacheVulkan::GenerateShaderID(uint32_t clutMode, GEBufferFormat pixelFormat) {
	return (clutMode & 0xFFFFFF) | (pixelFormat << 24);
}
