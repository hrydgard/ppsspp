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

static const char depal_vs[] = R"(#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texcoord0;
layout (location = 0) out vec2 v_texcoord0;
out gl_PerVertex { vec4 gl_Position; };
void main() {
  v_texcoord0 = a_texcoord0;
  gl_Position = vec4(a_position, 1.0);
}
)";

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
	std::string errors;
	vshader_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_VERTEX_BIT, depal_vs, &errors);
	assert(vshader_ != VK_NULL_HANDLE);
}

DepalShaderCacheVulkan::~DepalShaderCacheVulkan() {
	Clear();
	vulkan_->Delete().QueueDeleteShaderModule(vshader_);
}

DepalShaderVulkan *DepalShaderCacheVulkan::GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second;
	}

	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::FRAMEBUFFER_RENDERPASS);

	char *buffer = new char[2048];
	GenerateDepalShader(buffer, pixelFormat, GLSL_VULKAN);

	std::string error;
	VkShaderModule fshader = CompileShaderModule(vulkan_, VK_SHADER_STAGE_FRAGMENT_BIT, buffer, &error);
	if (fshader == VK_NULL_HANDLE) {
		Crash();
		delete[] buffer;
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
	return depal;
}

VulkanTexture *DepalShaderCacheVulkan::GetClutTexture(GEPaletteFormat clutFormat, u32 clutID, u32 *rawClut) {
	VkCommandBuffer cmd = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::INIT_COMMANDBUFFER);
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
	if (!vktex->CreateDirect(cmd, texturePixels, 1, 1, destFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, nullptr)) {
		Crash();
	}
	vktex->UploadMip(cmd, 0, texturePixels, 1, pushBuffer, pushOffset, texturePixels);
	vktex->EndCreate(cmd);

	DepalTextureVulkan *tex = new DepalTextureVulkan();
	tex->texture = vktex;
	tex->lastFrame = gpuStats.numFlips;
	texCache_[realClutID] = tex;
	return tex->texture;
}

void DepalShaderCacheVulkan::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		// Don't need to destroy the pipelines, they're handled by Vulkan2D.
		delete shader->second;
	}
	cache_.clear();

	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		delete tex->second->texture;
		delete tex->second;
	}
	texCache_.clear();
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
