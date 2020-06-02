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

#include <set>
#include <algorithm>

#include "profiler/profiler.h"

#include "base/display.h"
#include "math/lin/matrix4x4.h"
#include "math/dataconv.h"
#include "ext/native/thin3d/thin3d.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Vulkan/VulkanImage.h"
#include "thin3d/VulkanRenderManager.h"
#include "Common/ColorConv.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

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

FramebufferManagerVulkan::FramebufferManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan) :
	FramebufferManagerCommon(draw),
	vulkan_(vulkan) {
	presentation_->SetLanguage(GLSL_VULKAN);

	InitDeviceObjects();

	// After a blit we do need to rebind for the VulkanRenderManager to know what to do.
	needGLESRebinds_ = true;
}

FramebufferManagerVulkan::~FramebufferManagerVulkan() {
	DeviceLost();
}

void FramebufferManagerVulkan::SetTextureCache(TextureCacheVulkan *tc) {
	textureCacheVulkan_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerVulkan::SetShaderManager(ShaderManagerVulkan *sm) {
	shaderManagerVulkan_ = sm;
	shaderManager_ = sm;
}

void FramebufferManagerVulkan::SetDrawEngine(DrawEngineVulkan *td) {
	drawEngineVulkan_ = td;
	drawEngine_ = td;
}

void FramebufferManagerVulkan::InitDeviceObjects() {
	std::string fs_errors, vs_errors;
	fsBasicTex_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_FRAGMENT_BIT, tex_fs, &fs_errors);
	vsBasicTex_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_VERTEX_BIT, tex_vs, &vs_errors);
	assert(fsBasicTex_ != VK_NULL_HANDLE);
	assert(vsBasicTex_ != VK_NULL_HANDLE);

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &nearestSampler_);
	assert(res == VK_SUCCESS);
	samp.magFilter = VK_FILTER_LINEAR;
	samp.minFilter = VK_FILTER_LINEAR;
	res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &linearSampler_);
	assert(res == VK_SUCCESS);
}

void FramebufferManagerVulkan::DestroyDeviceObjects() {
	if (fsBasicTex_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeFragmentShader(fsBasicTex_);
		vulkan_->Delete().QueueDeleteShaderModule(fsBasicTex_);
	}
	if (vsBasicTex_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeVertexShader(vsBasicTex_);
		vulkan_->Delete().QueueDeleteShaderModule(vsBasicTex_);
	}
	if (stencilFs_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeFragmentShader(stencilFs_);
		vulkan_->Delete().QueueDeleteShaderModule(stencilFs_);
	}
	if (stencilVs_ != VK_NULL_HANDLE) {
		vulkan2D_->PurgeVertexShader(stencilVs_);
		vulkan_->Delete().QueueDeleteShaderModule(stencilVs_);
	}

	if (linearSampler_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteSampler(linearSampler_);
	if (nearestSampler_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteSampler(nearestSampler_);
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

void FramebufferManagerVulkan::Init() {
	FramebufferManagerCommon::Init();
	// Workaround for upscaling shaders where we force x1 resolution without saving it
	Resized();
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
	renderManager->BindPipeline(cur2DPipeline_);
	renderManager->Draw(vulkan2D_->GetPipelineLayout(), descSet, 0, nullptr, vbuffer, offset, 4);
}

void FramebufferManagerVulkan::Bind2DShader() {
	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::COMPATIBLE_RENDERPASS);
	cur2DPipeline_ = vulkan2D_->GetPipeline(rp, vsBasicTex_, fsBasicTex_);
}

int FramebufferManagerVulkan::GetLineWidth() {
	if (g_Config.iInternalResolution == 0) {
		return std::max(1, (int)(renderWidth_ / 480));
	} else {
		return g_Config.iInternalResolution;
	}
}

// This also binds vfb as the current render target.
void FramebufferManagerVulkan::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	// Technically, we should at this point re-interpret the bytes of the old format to the new.
	// That might get tricky, and could cause unnecessary slowness in some games.
	// For now, we just clear alpha/stencil from 565, which fixes shadow issues in Kingdom Hearts.
	// (it uses 565 to write zeros to the buffer, then 4444 to actually render the shadow.)
	//
	// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
	// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
	// to exactly reproduce in 4444 and 8888 formats.

	if (old == GE_FORMAT_565) {
		// We have to bind here instead of clear, since it can be that no framebuffer is bound.
		// The backend can sometimes directly optimize it to a clear.
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "ReformatFramebuffer"); 
		// draw_->Clear(Draw::FBChannel::FB_COLOR_BIT | Draw::FBChannel::FB_STENCIL_BIT, 0, 0.0f, 0);

		// Need to dirty anything that has command buffer dynamic state, in case we started a new pass above.
		// Should find a way to feed that information back, maybe... Or simply correct the issue in the rendermanager.
		gstate_c.Dirty(DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE);
	}
}

// Except for a missing rebind and silly scissor enables, identical copy of the same function in GPU_GLES - tricky parts are in thin3d.
void FramebufferManagerVulkan::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;
	bool matchingRenderSize = src->renderWidth == dst->renderWidth && src->renderHeight == dst->renderHeight;
	if (matchingDepthBuffer && matchingRenderSize && matchingSize) {
		// TODO: Currently, this copies depth AND stencil, which is a problem.  See #9740.
		draw_->CopyFramebufferImage(src->fbo, 0, 0, 0, 0, dst->fbo, 0, 0, 0, 0, src->renderWidth, src->renderHeight, 1, Draw::FB_DEPTH_BIT, "BlitFramebufferDepth");
		dst->last_frame_depth_updated = gpuStats.numFlips;
	} else if (matchingDepthBuffer && matchingSize) {
		/*
		int w = std::min(src->renderWidth, dst->renderWidth);
		int h = std::min(src->renderHeight, dst->renderHeight);
		draw_->BlitFramebuffer(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, Draw::FB_DEPTH_BIT, Draw::FB_BLIT_NEAREST);
		*/
	}
}

VkImageView FramebufferManagerVulkan::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return VK_NULL_HANDLE;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
	if (GPUStepping::IsStepping()) {
		skipCopy = true;
	}
	// Currently rendering to this framebuffer. Need to make a copy.
	if (!skipCopy && framebuffer == currentRenderVfb_) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(TempFBO::COPY, framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;
			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
			RebindFramebuffer("RebindFramebuffer - BindFramebufferAsColorTexture");
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
		return (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
	} else if (framebuffer != currentRenderVfb_ || (flags & BINDFBCOLOR_FORCE_SELF) != 0) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		return (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
	} else {
		ERROR_LOG_REPORT_ONCE(vulkanSelfTexture, G3D, "Attempting to texture from target (src=%08x / target=%08x / flags=%d)", framebuffer->fb_address, currentRenderVfb_->fb_address, flags);
		// To do this safely in Vulkan, we need to use input attachments.
		return VK_NULL_HANDLE;
	}
}

void FramebufferManagerVulkan::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// Nothing to do here.
}

void FramebufferManagerVulkan::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
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

	float srcXFactor = (float)src->renderWidth / (float)src->bufferWidth;
	float srcYFactor = (float)src->renderHeight / (float)src->bufferHeight;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = (float)dst->renderWidth / (float)dst->bufferWidth;
	float dstYFactor = (float)dst->renderHeight / (float)dst->bufferHeight;
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

	// BlitFramebuffer can clip, but CopyFramebufferImage is more restricted.
	// In case the src goes outside, we just skip the optimization in that case.
	const bool sameSize = dstX2 - dstX1 == srcX2 - srcX1 && dstY2 - dstY1 == srcY2 - srcY1;
	const bool sameDepth = dst->colorDepth == src->colorDepth;
	const bool srcInsideBounds = srcX2 <= src->renderWidth && srcY2 <= src->renderHeight;
	const bool dstInsideBounds = dstX2 <= dst->renderWidth && dstY2 <= dst->renderHeight;
	const bool xOverlap = src == dst && srcX2 > dstX1 && srcX1 < dstX2;
	const bool yOverlap = src == dst && srcY2 > dstY1 && srcY1 < dstY2;
	if (sameSize && sameDepth && srcInsideBounds && dstInsideBounds && !(xOverlap && yOverlap)) {
		draw_->CopyFramebufferImage(src->fbo, 0, srcX1, srcY1, 0, dst->fbo, 0, dstX1, dstY1, 0, dstX2 - dstX1, dstY2 - dstY1, 1, Draw::FB_COLOR_BIT, "BlitFramebuffer_Copy");
	} else {
		draw_->BlitFramebuffer(src->fbo, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2, Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST, "BlitFramebuffer_Blit");
	}
}

void FramebufferManagerVulkan::BeginFrameVulkan() {
	BeginFrame();
}

void FramebufferManagerVulkan::EndFrame() {
}

void FramebufferManagerVulkan::DeviceLost() {
	DestroyAllFBOs();
	DestroyDeviceObjects();
	presentation_->DeviceLost();
}

void FramebufferManagerVulkan::DeviceRestore(VulkanContext *vulkan, Draw::DrawContext *draw) {
	vulkan_ = vulkan;
	draw_ = draw;
	presentation_->DeviceRestore(draw);

	InitDeviceObjects();
}
