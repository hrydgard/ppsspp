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

#include "base/timeutil.h"
#include "math/lin/matrix4x4.h"
#include "math/dataconv.h"
#include "ext/native/thin3d/thin3d.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/ColorConv.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Debugger/Stepping.h"

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Common/Vulkan/VulkanImage.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

static const char tex_fs[] = R"(#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (binding = 0) uniform sampler2D sampler0;
layout (location = 0) in vec2 v_texcoord0;
layout (location = 0) out vec4 fragColor;
void main() {
  fragColor = texture(sampler0, v_texcoord0);
}
)";

static const char tex_vs[] = R"(#version 400
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

void ConvertFromRGBA8888_Vulkan(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format);

FramebufferManagerVulkan::FramebufferManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan) :
	FramebufferManagerCommon(draw),
	vulkan_(vulkan),
	drawPixelsTex_(nullptr),
	drawPixelsTexFormat_(GE_FORMAT_INVALID),
	convBuf_(nullptr),
	convBufSize_(0),
	textureCacheVulkan_(nullptr),
	shaderManagerVulkan_(nullptr),
	pixelBufObj_(nullptr),
	currentPBO_(0),
	curFrame_(0),
	pipelinePostShader_(VK_NULL_HANDLE),
	vulkan2D_(vulkan) {

	InitDeviceObjects();
}

FramebufferManagerVulkan::~FramebufferManagerVulkan() {
	delete[] convBuf_;

	vulkan2D_.Shutdown();
	DestroyDeviceObjects();
}

void FramebufferManagerVulkan::SetTextureCache(TextureCacheVulkan *tc) {
	textureCacheVulkan_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerVulkan::SetShaderManager(ShaderManagerVulkan *sm) {
	shaderManagerVulkan_ = sm;
	shaderManager_ = sm;
}

void FramebufferManagerVulkan::InitDeviceObjects() {
	// Initialize framedata
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].push_ = new VulkanPushBuffer(vulkan_, 64 * 1024);
	}

	pipelineCache2D_ = vulkan_->CreatePipelineCache();

	std::string fs_errors, vs_errors;
	fsBasicTex_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_FRAGMENT_BIT, tex_fs, &fs_errors);
	vsBasicTex_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_VERTEX_BIT, tex_vs, &vs_errors);
	assert(fsBasicTex_ != VK_NULL_HANDLE);
	assert(vsBasicTex_ != VK_NULL_HANDLE);

	// Prime the 2D pipeline cache.
	vulkan2D_.GetPipeline(pipelineCache2D_, vulkan_->GetSurfaceRenderPass(), vsBasicTex_, fsBasicTex_);
	vulkan2D_.GetPipeline(pipelineCache2D_, (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::COMPATIBLE_RENDERPASS), vsBasicTex_, fsBasicTex_);

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
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		if (frameData_[i].push_) {
			frameData_[i].push_->Destroy(vulkan_);
			delete frameData_[i].push_;
			frameData_[i].push_ = nullptr;
		}
	}
	delete drawPixelsTex_;
	drawPixelsTex_ = nullptr;

	if (fsBasicTex_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteShaderModule(fsBasicTex_);
	if (vsBasicTex_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteShaderModule(vsBasicTex_);

	if (linearSampler_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteSampler(linearSampler_);
	if (nearestSampler_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteSampler(nearestSampler_);
	// pipelineBasicTex_ and pipelineBasicTex_ come from vulkan2D_.
	if (pipelineCache2D_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeletePipelineCache(pipelineCache2D_);
}

void FramebufferManagerVulkan::NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth) {
	// if (!useBufferedRendering_) {
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);

		int mask = 0;
		// The Clear detection takes care of doing a regular draw instead if separate masking
		// of color and alpha is needed, so we can just treat them as the same.
		if (clearColor || clearAlpha)
			mask |= Draw::FBChannel::FB_COLOR_BIT;
		if (clearDepth)
			mask |= Draw::FBChannel::FB_DEPTH_BIT;
		if (clearAlpha)
			mask |= Draw::FBChannel::FB_STENCIL_BIT;

		draw_->Clear(mask, color, depth, 0);
		if (clearColor || clearAlpha) {
			SetColorUpdated(gstate_c.skipDrawReason);
		}
		if (clearDepth) {
			SetDepthUpdated();
		}
	//} else {
		// TODO: Clever render pass magic.
	//}
}

void FramebufferManagerVulkan::UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight) {
	float u_delta = 1.0f / renderWidth;
	float v_delta = 1.0f / renderHeight;
	float u_pixel_delta = u_delta;
	float v_pixel_delta = v_delta;
	if (postShaderAtOutputResolution_) {
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		u_pixel_delta = (1.0f / w) * (480.0f / bufferWidth);
		v_pixel_delta = (1.0f / h) * (272.0f / bufferHeight);
	}

	postUniforms_.texelDelta[0] = u_delta;
	postUniforms_.texelDelta[1] = v_delta;
	postUniforms_.pixelDelta[0] = u_pixel_delta;
	postUniforms_.pixelDelta[1] = v_pixel_delta;
	int flipCount = __DisplayGetFlipCount();
	int vCount = __DisplayGetVCount();
	float time[4] = { time_now(), (vCount % 60) * 1.0f / 60.0f, (float)vCount, (float)(flipCount % 60) };
	memcpy(postUniforms_.time, time, 4 * sizeof(float));
}

void FramebufferManagerVulkan::Init() {
	FramebufferManagerCommon::Init();
	// Workaround for upscaling shaders where we force x1 resolution without saving it
	Resized();
}

void FramebufferManagerVulkan::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) {
	if (drawPixelsTex_ && (drawPixelsTexFormat_ != srcPixelFormat || drawPixelsTex_->GetWidth() != width || drawPixelsTex_->GetHeight() != height)) {
		delete drawPixelsTex_;
		drawPixelsTex_ = nullptr;
	}

	if (!drawPixelsTex_) {
		drawPixelsTex_ = new VulkanTexture(vulkan_);
		drawPixelsTex_->CreateDirect(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		// Initialize backbuffer texture for DrawPixels
		drawPixelsTexFormat_ = srcPixelFormat;
	} else {
		drawPixelsTex_->TransitionForUpload();
	}

	// TODO: We can just change the texture format and flip some bits around instead of this.
	// Could share code with the texture cache perhaps.
	const uint8_t *data = srcPixels;
	if (srcPixelFormat != GE_FORMAT_8888 || srcStride != width) {
		u32 neededSize = width * height * 4;
		if (!convBuf_ || convBufSize_ < neededSize) {
			delete[] convBuf_;
			convBuf_ = new u8[neededSize];
			convBufSize_ = neededSize;
		}
		data = convBuf_;
		for (int y = 0; y < height; y++) {
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
			{
				const u16 *src = (const u16 *)srcPixels + srcStride * y;
				u8 *dst = convBuf_ + 4 * width * y;
				ConvertRGBA565ToRGBA8888((u32 *)dst, src, width);
			}
			break;

			case GE_FORMAT_5551:
			{
				const u16 *src = (const u16 *)srcPixels + srcStride * y;
				u8 *dst = convBuf_ + 4 * width * y;
				ConvertRGBA5551ToRGBA8888((u32 *)dst, src, width);
			}
			break;

			case GE_FORMAT_4444:
			{
				const u16 *src = (const u16 *)srcPixels + srcStride * y;
				u8 *dst = convBuf_ + 4 * width * y;
				ConvertRGBA4444ToRGBA8888((u32 *)dst, src, width);
			}
			break;

			case GE_FORMAT_8888:
			{
				const u8 *src = srcPixels + srcStride * 4 * y;
				u8 *dst = convBuf_ + 4 * width * y;
				memcpy(dst, src, 4 * width);
			}
			break;

			case GE_FORMAT_INVALID:
				_dbg_assert_msg_(G3D, false, "Invalid pixelFormat passed to DrawPixels().");
				break;
			}
		}
	}

	VkBuffer buffer;
	size_t offset = frameData_[curFrame_].push_->Push(data, width * height * 4, &buffer);
	drawPixelsTex_->UploadMip(0, width, height, buffer, (uint32_t)offset, width);
	drawPixelsTex_->EndCreate();

	overrideImageView_ = drawPixelsTex_->GetImageView();
}

void FramebufferManagerVulkan::SetViewport2D(int x, int y, int w, int h) {
	VkViewport vp;
	vp.minDepth = 0.0;
	vp.maxDepth = 1.0;
	vp.x = (float)x;
	vp.y = (float)y;
	vp.width = (float)w;
	vp.height = (float)h;

	// Since we're about to override it.
	draw_->FlushState();
	VkCommandBuffer cmd = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::RENDERPASS_COMMANDBUFFER);
	vkCmdSetViewport(cmd, 0, 1, &vp);
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

	draw_->FlushState();

	// TODO: Should probably use draw_ directly and not go low level

	VulkanPushBuffer *push = frameData_[curFrame_].push_;

	VkCommandBuffer cmd = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::RENDERPASS_COMMANDBUFFER);

	VkImageView view = overrideImageView_ ? overrideImageView_ : (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE_IMAGEVIEW);
	if ((flags & DRAWTEX_KEEP_TEX) == 0)
		overrideImageView_ = VK_NULL_HANDLE;
	vulkan2D_.BindDescriptorSet(cmd, view, (flags & DRAWTEX_LINEAR) ? linearSampler_ : nearestSampler_);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cur2DPipeline_);
	VkBuffer vbuffer;
	VkDeviceSize offset = push->Push(vtx, sizeof(vtx), &vbuffer);
	vkCmdBindVertexBuffers(cmd, 0, 1, &vbuffer, &offset);
	vkCmdDraw(cmd, 4, 1, 0, 0);
}

void FramebufferManagerVulkan::Bind2DShader() {
	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::COMPATIBLE_RENDERPASS);
	cur2DPipeline_ = vulkan2D_.GetPipeline(pipelineCache2D_, rp, vsBasicTex_, fsBasicTex_);
}

void FramebufferManagerVulkan::BindPostShader(const PostShaderUniforms &uniforms) {
	Bind2DShader();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
}

void FramebufferManagerVulkan::RebindFramebuffer() {
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	} else {
		// Should this even happen?
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	}
}

bool FramebufferManagerVulkan::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
	// In Vulkan we should be able to simply copy the stencil data directly to a stencil buffer without
	// messing about with bitplane textures and the like.
	return false;
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
	// (it uses 565 to write zeros to the buffer, than 4444 to actually render the shadow.)
	//
	// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
	// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
	// to exactly reproduce in 4444 and 8888 formats.

	if (old == GE_FORMAT_565) {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
	} else {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	}
}

// Except for a missing rebind and silly scissor enables, identical copy of the same function in GPU_GLES - tricky parts are in thin3d.
void FramebufferManagerVulkan::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	if (g_Config.bDisableSlowFramebufEffects) {
		return;
	}

	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;
	bool matchingRenderSize = src->renderWidth == dst->renderWidth && src->renderHeight == dst->renderHeight;

	if (gstate_c.Supports(GPU_SUPPORTS_ANY_COPY_IMAGE) && matchingDepthBuffer && matchingRenderSize && matchingSize) {
		draw_->CopyFramebufferImage(src->fbo, 0, 0, 0, 0, dst->fbo, 0, 0, 0, 0, src->renderWidth, src->renderHeight, 1, Draw::FB_DEPTH_BIT);
	} else if (matchingDepthBuffer && matchingSize) {
		int w = std::min(src->renderWidth, dst->renderWidth);
		int h = std::min(src->renderHeight, dst->renderHeight);
		draw_->BlitFramebuffer(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, Draw::FB_DEPTH_BIT, Draw::FB_BLIT_NEAREST);
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
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	// Currently rendering to this framebuffer. Need to make a copy.
	if (!skipCopy && framebuffer == currentRenderVfb_) {
		// ignore this case for now, doesn't work
		// ILOG("Texturing from current render Vfb!");
		return VK_NULL_HANDLE;

		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;
			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
			RebindFramebuffer();
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
		return (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE_IMAGEVIEW);
	} else if (framebuffer != currentRenderVfb_) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		return (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE_IMAGEVIEW);
	} else {
		ERROR_LOG_REPORT_ONCE(vulkanSelfTexture, G3D, "Attempting to texture from target");
		// To do this safely in Vulkan, we need to use input attachments.
		return VK_NULL_HANDLE;
	}
}

VulkanTexture *FramebufferManagerVulkan::GetFramebufferColor(u32 fbRawAddress, VirtualFramebuffer *framebuffer, int flags) {
	if (framebuffer == NULL) {
		framebuffer = currentRenderVfb_;
	}

	if (!framebuffer->fbo || !useBufferedRendering_) {
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return nullptr;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == fbRawAddress) {
		// TODO: Enable the below code
		return nullptr; // framebuffer->fbo->GetColor();
		/*
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		VulkanFBO *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (FBOColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;

			int x = 0;
			int y = 0;
			int w = framebuffer->drawnWidth;
			int h = framebuffer->drawnHeight;

			// If max is not > min, we probably could not detect it.  Skip.
			// See the vertex decoder, where this is updated.
			if ((flags & BINDFBCOLOR_MAY_COPY_WITH_UV) == BINDFBCOLOR_MAY_COPY_WITH_UV && gstate_c.vertBounds.maxU > gstate_c.vertBounds.minU) {
				x = gstate_c.vertBounds.minU;
				y = gstate_c.vertBounds.minV;
				w = gstate_c.vertBounds.maxU - x;
				h = gstate_c.vertBounds.maxV - y;

				// If we bound a framebuffer, apply the byte offset as pixels to the copy too.
				if (flags & BINDFBCOLOR_APPLY_TEX_OFFSET) {
					x += gstate_c.curTextureXOffset;
					y += gstate_c.curTextureYOffset;
				}
			}

			BlitFramebuffer(&copyInfo, x, y, framebuffer, x, y, w, h, 0);

			return nullptr;  // fbo_bind_color_as_texture(renderCopy, 0);
		} else {
			return framebuffer->fbo->GetColor();
		}
		*/
	} else {
		return nullptr; // framebuffer->fbo->GetColor();
	}
}

void FramebufferManagerVulkan::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	PROFILE_THIS_SCOPE("gpu-readback");
	if (sync) {
		// flush async just in case when we go for synchronous update
		// Doesn't actually pack when sent a null argument.
		PackFramebufferAsync_(nullptr);
	}

	if (vfb) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
		OptimizeDownloadRange(vfb, x, y, w, h);
		BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

		// PackFramebufferSync_() - Synchronous pixel data transfer using glReadPixels
		// PackFramebufferAsync_() - Asynchronous pixel data transfer using glReadPixels with PBOs

		// TODO: Can we fall back to sync without these?
		if (!sync) {
			PackFramebufferAsync_(nvfb);
		} else {
			PackFramebufferSync_(nvfb, x, y, w, h);
		}

		textureCacheVulkan_->ForgetLastTexture();
		RebindFramebuffer();
	}
}

void FramebufferManagerVulkan::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	PROFILE_THIS_SCOPE("gpu-readback");
	// Flush async just in case.
	PackFramebufferAsync_(nullptr);

	VirtualFramebuffer *vfb = GetVFBAt(fb_address);
	if (vfb && vfb->fb_stride != 0) {
		const u32 bpp = vfb->drawnFormat == GE_FORMAT_8888 ? 4 : 2;
		int x = 0;
		int y = 0;
		int pixels = loadBytes / bpp;
		// The height will be 1 for each stride or part thereof.
		int w = std::min(pixels % vfb->fb_stride, (int)vfb->width);
		int h = std::min((pixels + vfb->fb_stride - 1) / vfb->fb_stride, (int)vfb->height);

		// No need to download if we already have it.
		if (!vfb->memoryUpdated && vfb->clutUpdatedBytes < loadBytes) {
			// We intentionally don't call OptimizeDownloadRange() here - we don't want to over download.
			// CLUT framebuffers are often incorrectly estimated in size.
			if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
				vfb->memoryUpdated = true;
			}
			vfb->clutUpdatedBytes = loadBytes;

			// We'll pseudo-blit framebuffers here to get a resized version of vfb.
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

			PackFramebufferSync_(nvfb, x, y, w, h);

			textureCacheVulkan_->ForgetLastTexture();
			RebindFramebuffer();
		}
	}
}

bool FramebufferManagerVulkan::CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// When updating VRAM, it need to be exact format.
	if (!gstate_c.Supports(GPU_PREFER_CPU_DOWNLOAD)) {
		switch (nvfb->format) {
		case GE_FORMAT_4444:
			nvfb->colorDepth = VK_FBO_4444;
			break;
		case GE_FORMAT_5551:
			nvfb->colorDepth = VK_FBO_5551;
			break;
		case GE_FORMAT_565:
			nvfb->colorDepth = VK_FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			nvfb->colorDepth = VK_FBO_8888;
			break;
		}
	}

	/*
	nvfb->fbo = CreateFramebuffer(nvfb->width, nvfb->height, 1, false, (FBOColorDepth)nvfb->colorDepth);
	if (!(nvfb->fbo)) {
		ERROR_LOG(FRAMEBUF, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
		return false;
	}

	BindFramebufferAsRenderTarget(nvfb->fbo);
	*/
	return true;
}

void FramebufferManagerVulkan::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// _assert_msg_(G3D, nvfb->fbo, "Expecting a valid nvfb in UpdateDownloadTempBuffer");

	// Discard the previous contents of this buffer where possible.
	/*
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		BindFramebufferAsRenderTargetnvfb->fbo);
		GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_STENCIL_ATTACHMENT, GL_DEPTH_ATTACHMENT };
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
	} else if (gl_extensions.IsGLES) {
		BindFramebufferAsRenderTargetnvfb->fbo);
	}
	*/
}

void FramebufferManagerVulkan::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_)
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
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
		draw_->CopyFramebufferImage(src->fbo, 0, srcX1, srcY1, 0, dst->fbo, 0, dstX1, dstY1, 0, dstX2 - dstX1, dstY2 - dstY1, 1, Draw::FB_COLOR_BIT);
	} else {
		draw_->BlitFramebuffer(src->fbo, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2, Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST);
	}
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888_Vulkan(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
		} else {
			// Here let's assume they don't intersect
			for (u32 y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else {
		// But here it shouldn't matter if they do intersect
		u16 *dst16 = (u16 *)dst;
		switch (format) {
		case GE_FORMAT_565: // BGR 565
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_5551: // ABGR 1555
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_4444: // ABGR 4444
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA4444(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_8888:
		case GE_FORMAT_INVALID:
			// Not possible.
			break;
		}
	}
}

// One frame behind, but no stalling.
void FramebufferManagerVulkan::PackFramebufferAsync_(VirtualFramebuffer *vfb) {
	const int MAX_PBO = 2;
	uint8_t *packed = 0;
	const u8 nextPBO = (currentPBO_ + 1) % MAX_PBO;

	bool useCPU = false;

	// We'll prepare two PBOs to switch between readying and reading
	if (!pixelBufObj_) {
		if (!vfb) {
			// This call is just to flush the buffers.  We don't have any yet,
			// so there's nothing to do.
			return;
		}

		// GLuint pbos[MAX_PBO];
		// glGenBuffers(MAX_PBO, pbos);

		pixelBufObj_ = new AsyncPBOVulkan[MAX_PBO];
		for (int i = 0; i < MAX_PBO; i++) {
			// TODO
			// pixelBufObj_[i].handle = pbos[i];
			pixelBufObj_[i].maxSize = 0;
			pixelBufObj_[i].reading = false;
		}
	}

	// Receive previously requested data from a PBO
	AsyncPBOVulkan &pbo = pixelBufObj_[nextPBO];
	if (pbo.reading) {
		// glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo.handle);
		// packed = (GLubyte *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, pbo.size, GL_MAP_READ_BIT);

		if (packed) {
			DEBUG_LOG(FRAMEBUF, "Reading PBO to memory , bufSize = %u, packed = %p, fb_address = %08x, stride = %u, pbo = %u",
				pbo.size, packed, pbo.fb_address, pbo.stride, nextPBO);

			// We don't need to convert, GPU already did (or should have)
			// (vulkan: hopefully)
			Memory::MemcpyUnchecked(pbo.fb_address, packed, pbo.size);

			pbo.reading = false;
		}

		// glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	}
	
	// Order packing/readback of the framebuffer
	if (vfb) {
		// int pixelType, pixelFormat;
		int pixelSize, align;
		switch (vfb->format) {
		case GE_FORMAT_4444: // 16 bit RGBA
			// pixelType = GL_UNSIGNED_SHORT_4_4_4_4;
			// pixelFormat = GL_RGBA;
			pixelSize = 2;
			align = 2;
			break;
		case GE_FORMAT_5551: // 16 bit RGBA
			// pixelType = GL_UNSIGNED_SHORT_5_5_5_1;
			// pixelFormat = GL_RGBA;
			pixelSize = 2;
			align = 2;
			break;
		case GE_FORMAT_565: // 16 bit RGB
			// pixelType = GL_UNSIGNED_SHORT_5_6_5;
			// pixelFormat = GL_RGB;
			pixelSize = 2;
			align = 2;
			break;
		case GE_FORMAT_8888: // 32 bit RGBA
		default:
			// pixelType = GL_UNSIGNED_BYTE;
			// pixelFormat = UseBGRA8888() ? GL_BGRA_EXT : GL_RGBA;
			pixelSize = 4;
			align = 4;
			break;
		}

		// If using the CPU, we need 4 bytes per pixel always.
		u32 bufSize = vfb->fb_stride * vfb->height * 4 * (useCPU ? 4 : pixelSize);
		u32 fb_address = (0x04000000) | vfb->fb_address;

		if (vfb->fbo) {
			// fbo_bind_for_read(vfb->fbo);
		} else {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferAsync_: vfb->fbo == 0");
			// fbo_unbind_read();
			return;
		}

		// glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelBufObj_[currentPBO_].handle);
		if (pixelBufObj_[currentPBO_].maxSize < bufSize) {
			// We reserve a buffer big enough to fit all those pixels
			// glBufferData(GL_PIXEL_PACK_BUFFER, bufSize, NULL, GL_DYNAMIC_READ);
			pixelBufObj_[currentPBO_].maxSize = bufSize;
		}

		if (useCPU) {
			// If converting pixel formats on the CPU we'll always request RGBA8888
			// SafeGLReadPixels(0, 0, vfb->fb_stride, vfb->height, UseBGRA8888() ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE, 0);
		} else {
			// Otherwise we'll directly request the format we need and let the GPU sort it out
			// SafeGLReadPixels(0, 0, vfb->fb_stride, vfb->height, pixelFormat, pixelType, 0);
		}

		pixelBufObj_[currentPBO_].fb_address = fb_address;
		pixelBufObj_[currentPBO_].size = bufSize;
		pixelBufObj_[currentPBO_].stride = vfb->fb_stride;
		pixelBufObj_[currentPBO_].height = vfb->height;
		pixelBufObj_[currentPBO_].format = vfb->format;
		pixelBufObj_[currentPBO_].reading = true;
	}

	currentPBO_ = nextPBO;
}

void FramebufferManagerVulkan::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {

}

void FramebufferManagerVulkan::BeginFrameVulkan() {
	BeginFrame();

	vulkan2D_.BeginFrame();

	FrameData &frame = frameData_[curFrame_];

	frame.push_->Reset();
	frame.push_->Begin(vulkan_);
	
	if (!useBufferedRendering_) {
		// TODO: This hackery should not be necessary. Is it? Need to check.
		// We only use a single command buffer in this case.
		VkCommandBuffer cmd = vulkan_->GetSurfaceCommandBuffer();
		VkRect2D scissor;
		scissor.offset = { 0, 0 };
		scissor.extent = { (uint32_t)pixelWidth_, (uint32_t)pixelHeight_ };
		vkCmdSetScissor(cmd, 0, 1, &scissor);
	} else {
		// Each render pass will set up scissor again.
	}
}

void FramebufferManagerVulkan::EndFrame() {
	// We flush to memory last requested framebuffer, if any.
	// Only do this in the read-framebuffer modes.
	if (updateVRAM_)
		PackFramebufferAsync_(nullptr);
	FrameData &frame = frameData_[curFrame_];
	frame.push_->End();

	vulkan2D_.EndFrame();

	curFrame_++;
	if (curFrame_ >= vulkan_->GetInflightFrames()) {
		curFrame_ = 0;
	}
}

void FramebufferManagerVulkan::DeviceLost() {
	vulkan2D_.DeviceLost();

	DestroyAllFBOs();
	DestroyDeviceObjects();
}

void FramebufferManagerVulkan::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;

	vulkan2D_.DeviceRestore(vulkan_);
	InitDeviceObjects();
}

std::vector<FramebufferInfo> FramebufferManagerVulkan::GetFramebufferList() {
	std::vector<FramebufferInfo> list;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];

		FramebufferInfo info;
		info.fb_address = vfb->fb_address;
		info.z_address = vfb->z_address;
		info.format = vfb->format;
		info.width = vfb->width;
		info.height = vfb->height;
		info.fbo = vfb->fbo;
		list.push_back(info);
	}

	return list;
}

void FramebufferManagerVulkan::DestroyAllFBOs() {
	currentRenderVfb_ = 0;
	displayFramebuf_ = 0;
	prevDisplayFramebuf_ = 0;
	prevPrevDisplayFramebuf_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		INFO_LOG(FRAMEBUF, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();

	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();
}

void FramebufferManagerVulkan::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.

	// TODO: It's really bad that we are calling SetRenderFramebuffer here with
	// all the irrelevant state checking it'll use to decide what to do. Should
	// do something more focused here.
	SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (!draw_->GetNativeObject(Draw::NativeObject::CURRENT_RENDERPASS))
		Crash();
	drawEngine_->Flush();
}

void FramebufferManagerVulkan::Resized() {
	FramebufferManagerCommon::Resized();

	if (UpdateSize()) {
		DestroyAllFBOs();
	}
}

bool FramebufferManagerVulkan::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxStride) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
/*
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, format);
		return true;
	}

	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GE_FORMAT_8888, false, true);
	if (vfb->fbo)
		fbo_bind_for_read(vfb->fbo);
	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		glReadBuffer(GL_COLOR_ATTACHMENT0);

	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	/*
	fbo_unbind_read();

	int pw = PSP_CoreParameter().pixelWidth;
	int ph = PSP_CoreParameter().pixelHeight;

	// The backbuffer is flipped.
	buffer.Allocate(pw, ph, GPU_DBG_FORMAT_888_RGB, true);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	SafeGLReadPixels(0, 0, pw, ph, GL_RGB, GL_UNSIGNED_BYTE, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
		return true;
	}

	/*
	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_FLOAT, false);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_DEPTH_COMPONENT, GL_FLOAT, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		// TODO: Actually get the stencil.
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, GPU_DBG_FORMAT_8888);
		return true;
	}

	/*
	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_8BIT, false);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
	*/
	return false;
}
