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

#include <algorithm>

#include "Common/Profiler/Profiler.h"

#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/GPU/thin3d.h"

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

using namespace PPSSPP_VK;

static const char tex_fs[] = R"(#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (binding = 0) uniform sampler2D sampler0;
layout (location = 0) in vec2 v_texcoord0;
layout (location = 0) out vec4 fragColor;
void main() {
  fragColor = texture(sampler0, v_texcoord0);
}
)";

static const char tex_vs[] = R"(#version 450
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

FramebufferManagerVulkan::FramebufferManagerVulkan(Draw::DrawContext *draw) :
	FramebufferManagerCommon(draw) {
	presentation_->SetLanguage(GLSL_VULKAN);

	InitDeviceObjects();

	// After a blit we do need to rebind for the VulkanRenderManager to know what to do.
	needGLESRebinds_ = true;
}

FramebufferManagerVulkan::~FramebufferManagerVulkan() {
	DeviceLost();
}

void FramebufferManagerVulkan::SetTextureCache(TextureCacheVulkan *tc) {
	textureCache_ = tc;
}

void FramebufferManagerVulkan::SetShaderManager(ShaderManagerVulkan *sm) {
	shaderManager_ = sm;
}

void FramebufferManagerVulkan::SetDrawEngine(DrawEngineVulkan *td) {
	drawEngine_ = td;
}

void FramebufferManagerVulkan::InitDeviceObjects() {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	std::string fs_errors, vs_errors;
	fsBasicTex_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_FRAGMENT_BIT, tex_fs, &fs_errors);
	vsBasicTex_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_VERTEX_BIT, tex_vs, &vs_errors);
	_assert_(fsBasicTex_ != VK_NULL_HANDLE);
	_assert_(vsBasicTex_ != VK_NULL_HANDLE);

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	VkResult res = vkCreateSampler(vulkan->GetDevice(), &samp, nullptr, &nearestSampler_);
	_assert_(res == VK_SUCCESS);
	samp.magFilter = VK_FILTER_LINEAR;
	samp.minFilter = VK_FILTER_LINEAR;
	res = vkCreateSampler(vulkan->GetDevice(), &samp, nullptr, &linearSampler_);
	_assert_(res == VK_SUCCESS);
}

void FramebufferManagerVulkan::DestroyDeviceObjects() {
	if (!draw_)
		return;
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	if (fsBasicTex_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeFragmentShader(fsBasicTex_);
		vulkan->Delete().QueueDeleteShaderModule(fsBasicTex_);
	}
	if (vsBasicTex_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeVertexShader(vsBasicTex_);
		vulkan->Delete().QueueDeleteShaderModule(vsBasicTex_);
	}
	if (stencilFs_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeFragmentShader(stencilFs_);
		vulkan->Delete().QueueDeleteShaderModule(stencilFs_);
	}
	if (stencilVs_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeVertexShader(stencilVs_);
		vulkan->Delete().QueueDeleteShaderModule(stencilVs_);
	}

	if (linearSampler_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteSampler(linearSampler_);
	if (nearestSampler_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteSampler(nearestSampler_);
}

void FramebufferManagerVulkan::NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth) {
	int mask = 0;
	// The Clear detection takes care of doing a regular draw instead if separate masking
	// of color and alpha is needed, so we can just treat them as the same.
	if (clearColor || clearAlpha)
		mask |= Draw::FBChannel::FB_COLOR_BIT;
	if (clearDepth)
		mask |= Draw::FBChannel::FB_DEPTH_BIT;
	if (clearAlpha)
		mask |= Draw::FBChannel::FB_STENCIL_BIT;

	// Note that since the alpha channel and the stencil channel are shared on the PSP,
	// when we clear alpha, we also clear stencil to the same value.
	draw_->Clear(mask, color, depth, color >> 24);
	if (clearColor || clearAlpha) {
		SetColorUpdated(gstate_c.skipDrawReason);
	}
	if (clearDepth) {
		SetDepthUpdated();
	}
}

void FramebufferManagerVulkan::Bind2DShader() {
	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::COMPATIBLE_RENDERPASS);
	cur2DPipeline_ = vulkan2D_->GetPipeline(rp, vsBasicTex_, fsBasicTex_);
}

void FramebufferManagerVulkan::BeginFrameVulkan() {
	BeginFrame();
}

void FramebufferManagerVulkan::EndFrame() {
}

void FramebufferManagerVulkan::DeviceLost() {
	DestroyDeviceObjects();
	FramebufferManagerCommon::DeviceLost();
}

void FramebufferManagerVulkan::DeviceRestore(Draw::DrawContext *draw) {
	FramebufferManagerCommon::DeviceRestore(draw);
	InitDeviceObjects();
}
