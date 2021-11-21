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

#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DepalettizeShaderCommon.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/GPU/Vulkan/VulkanImage.h"

static const char depal_vs[] = R"(#version 450
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

static VkFormat GetClutDestFormat(GEPaletteFormat format, VkComponentMapping *componentMapping) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		*componentMapping = VULKAN_4444_SWIZZLE;
		return VULKAN_4444_FORMAT;
	case GE_CMODE_16BIT_ABGR5551:
		*componentMapping = VULKAN_1555_SWIZZLE;
		return VULKAN_1555_FORMAT;
	case GE_CMODE_16BIT_BGR5650:
		*componentMapping = VULKAN_565_SWIZZLE;
		return VULKAN_565_FORMAT;
	case GE_CMODE_32BIT_ABGR8888:
		*componentMapping = VULKAN_8888_SWIZZLE;
		return VULKAN_8888_FORMAT;
	}
	return VK_FORMAT_UNDEFINED;
}

DepalShaderCacheVulkan::DepalShaderCacheVulkan(Draw::DrawContext *draw)
	: draw_(draw) {
	DeviceRestore(draw);
}

DepalShaderCacheVulkan::~DepalShaderCacheVulkan() {
	DeviceLost();
}

void DepalShaderCacheVulkan::DeviceLost() {
	Clear();
	if (vshader_) {
		VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
		vulkan2D_->PurgeVertexShader(vshader_);
		vulkan->Delete().QueueDeleteShaderModule(vshader_);
		vshader_ = VK_NULL_HANDLE;
	}
	draw_ = nullptr;
}

void DepalShaderCacheVulkan::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	std::string errors;
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	vshader_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_VERTEX_BIT, depal_vs, &errors);
	_assert_(vshader_ != VK_NULL_HANDLE);
}

DepalShaderVulkan *DepalShaderCacheVulkan::GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second;
	}

	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::FRAMEBUFFER_RENDERPASS);
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	char *buffer = new char[2048];
	GenerateDepalShader(buffer, pixelFormat, GLSL_VULKAN);

	std::string error;
	VkShaderModule fshader = CompileShaderModule(vulkan, VK_SHADER_STAGE_FRAGMENT_BIT, buffer, &error);
	if (fshader == VK_NULL_HANDLE) {
		INFO_LOG(G3D, "Source:\n%s\n\n", buffer);
		Crash();
		delete[] buffer;
		return nullptr;
	}

	VkPipeline pipeline = vulkan2D_->GetPipeline(rp, vshader_, fshader);
	// Can delete the shader module now that the pipeline has been created.
	// Maybe don't even need to queue it..
	// "true" keeps the pipeline itself alive, forgetting the fshader.
	vulkan2D_->PurgeFragmentShader(fshader, true);
	vulkan->Delete().QueueDeleteShaderModule(fshader);

	DepalShaderVulkan *depal = new DepalShaderVulkan();
	depal->pipeline = pipeline;
	depal->code = buffer;
	cache_[id] = depal;
	return depal;
}

VulkanTexture *DepalShaderCacheVulkan::GetClutTexture(GEPaletteFormat clutFormat, u32 clutHash, u32 *rawClut, bool expandTo32bit) {
	u32 clutId = GetClutID(clutFormat, clutHash);
	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->texture->Touch();
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VkComponentMapping componentMapping;
	VkFormat destFormat = GetClutDestFormat(clutFormat, &componentMapping);

	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;
	int bpp = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	VkFormat dstFmt;
	uint32_t *expanded = nullptr;
	if (expandTo32bit && clutFormat != GE_CMODE_32BIT_ABGR8888) {
		expanded = new uint32_t[texturePixels];
		switch (clutFormat) {
		case GE_CMODE_16BIT_ABGR4444:
			ConvertRGBA4444ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_ABGR5551:
			ConvertRGBA5551ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_BGR5650:
			ConvertRGB565ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		default:
			break;
		}
		rawClut = expanded;
		dstFmt = VK_FORMAT_R8G8B8A8_UNORM;
		bpp = 4;
		componentMapping.r = VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY;
		componentMapping.g = VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY;
		componentMapping.b = VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY;
		componentMapping.a = VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY;
	} else {
		dstFmt = GetClutDestFormat(clutFormat, &componentMapping);
	}

	VulkanTexture *vktex = new VulkanTexture(vulkan);
	vktex->SetTag("DepalClut");
	VkCommandBuffer cmd = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::INIT_COMMANDBUFFER);
	if (!vktex->CreateDirect(cmd, texturePixels, 1, 1, destFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &componentMapping)) {
		ERROR_LOG(G3D, "Failed to create texture for CLUT");
		return nullptr;
	}

	VkBuffer pushBuffer;
	uint32_t pushOffset = push_->PushAligned(rawClut, 1024, 4, &pushBuffer);

	vktex->UploadMip(cmd, 0, texturePixels, 1, pushBuffer, pushOffset, texturePixels);
	vktex->EndCreate(cmd, false, VK_PIPELINE_STAGE_TRANSFER_BIT);

	DepalTextureVulkan *tex = new DepalTextureVulkan();
	tex->texture = vktex;
	tex->lastFrame = gpuStats.numFlips;
	texCache_[clutId] = tex;

	if (expandTo32bit) {
		delete[] expanded;
	}

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
