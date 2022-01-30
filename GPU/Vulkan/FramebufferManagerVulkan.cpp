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

void FramebufferManagerVulkan::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
	float texCoords[8] = {
		u0,v0,
		u1,v0,
		u1,v1,
		u0,v1,
	};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 4; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 6; break;
		}
		for (int i = 0; i < 8; i++) {
			temp[i] = texCoords[(i + rotation) & 7];
		}
		memcpy(texCoords, temp, sizeof(temp));
	}

	Vulkan2D::Vertex vtx[4] = {
		{x,     y,     0, texCoords[0], texCoords[1]},
		{x + w, y,     0, texCoords[2], texCoords[3]},
		{x,     y + h, 0, texCoords[6], texCoords[7]},
		{x + w, y + h, 0, texCoords[4], texCoords[5]},
	};

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		vtx[i].x = vtx[i].x * invDestW - 1.0f;
		vtx[i].y = vtx[i].y * invDestH - 1.0f;
	}

	if ((flags & DRAWTEX_TO_BACKBUFFER) && g_display_rotation != DisplayRotation::ROTATE_0) {
		for (int i = 0; i < 4; i++) {
			Lin::Vec3 v(vtx[i].x, vtx[i].y, 0.0f);
			// backwards notation, should fix that...
			v = v * g_display_rot_matrix;
			vtx[i].x = v.x;
			vtx[i].y = v.y;
		}
	}

	draw_->FlushState();

	// TODO: Should probably use draw_ directly and not go low level

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	VkImageView view = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
	VkDescriptorSet descSet = vulkan2D_->GetDescriptorSet(view, (flags & DRAWTEX_LINEAR) ? linearSampler_ : nearestSampler_, VK_NULL_HANDLE, VK_NULL_HANDLE);
	VkBuffer vbuffer;
	VkDeviceSize offset = push_->Push(vtx, sizeof(vtx), &vbuffer);
	renderManager->BindPipeline(cur2DPipeline_, (PipelineFlags)0);
	renderManager->Draw(vulkan2D_->GetPipelineLayout(), descSet, 0, nullptr, vbuffer, offset, 4);
}

void FramebufferManagerVulkan::Bind2DShader() {
	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::COMPATIBLE_RENDERPASS);
	cur2DPipeline_ = vulkan2D_->GetPipeline(rp, vsBasicTex_, fsBasicTex_);
}

void FramebufferManagerVulkan::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, const char *tag) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "BlitFramebuffer_Fail");
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		}
		return;
	}

	// Perform a little bit of clipping first.
	// Block transfer coords are unsigned so I don't think we need to clip on the left side..
	if (dstX + w > dst->bufferWidth) {
		w -= dstX + w - dst->bufferWidth;
	}
	if (dstY + h > dst->bufferHeight) {
		h -= dstY + h - dst->bufferHeight;
	}
	if (srcX + w > src->bufferWidth) {
		w -= srcX + w - src->bufferWidth;
	}
	if (srcY + h > src->bufferHeight) {
		h -= srcY + h - src->bufferHeight;
	}

	if (w <= 0 || h <= 0) {
		// The whole rectangle got clipped.
		return;
	}

	float srcXFactor = (float)src->renderScaleFactor;
	float srcYFactor = (float)src->renderScaleFactor;

	// Some games use wrong-format block transfers. Simulate that.
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = (float)dst->renderScaleFactor;
	float dstYFactor = (float)dst->renderScaleFactor;
	const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
	if (dstBpp != bpp && bpp != 0) {
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY1 = dstY * dstYFactor;
	int dstY2 = (dstY + h) * dstYFactor;

	if (src == dst && srcX == dstX && srcY == dstY) {
		// Let's just skip a copy where the destination is equal to the source.
		WARN_LOG_REPORT_ONCE(blitSame, G3D, "Skipped blit with equal dst and src");
		return;
	}

	const bool sameSize = dstX2 - dstX1 == srcX2 - srcX1 && dstY2 - dstY1 == srcY2 - srcY1;
	const bool srcInsideBounds = srcX2 <= src->renderWidth && srcY2 <= src->renderHeight;
	const bool dstInsideBounds = dstX2 <= dst->renderWidth && dstY2 <= dst->renderHeight;
	const bool xOverlap = src == dst && srcX2 > dstX1 && srcX1 < dstX2;
	const bool yOverlap = src == dst && srcY2 > dstY1 && srcY1 < dstY2;
	if (sameSize && srcInsideBounds && dstInsideBounds && !(xOverlap && yOverlap)) {
		draw_->CopyFramebufferImage(src->fbo, 0, srcX1, srcY1, 0, dst->fbo, 0, dstX1, dstY1, 0, dstX2 - dstX1, dstY2 - dstY1, 1, Draw::FB_COLOR_BIT, tag);
	} else {
		draw_->BlitFramebuffer(src->fbo, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2, Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST, tag);
	}
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
