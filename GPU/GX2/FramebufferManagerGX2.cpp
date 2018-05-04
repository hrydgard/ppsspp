// Copyright (c) 2017- PPSSPP Project.

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

#include "Common/Profiler/Profiler.h"
#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/GPU/thin3d.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Text/I18n.h"
#include "Common/GPU/thin3d.h"

#include "Common/ColorConv.h"
#include "Common/Math/math_util.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/ShaderTranslation.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/DrawEngineGX2.h"
#include "GPU/GX2/GX2Shaders.h"


#include <wiiu/gx2.h>
#include <algorithm>

// clang-format off
const GX2AttribStream FramebufferManagerGX2::g_QuadAttribStream[2] = {
	{ 0, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_DEFAULT },
	{ 1, 0, 12, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _0, _0), GX2_ENDIAN_SWAP_DEFAULT },
};

// STRIP geometry
__attribute__((aligned(GX2_VERTEX_BUFFER_ALIGNMENT)))
float FramebufferManagerGX2::fsQuadBuffer_[20] = {
	-1.0f,-1.0f, 0.0f, 0.0f, 0.0f,
	 1.0f,-1.0f, 0.0f, 1.0f, 0.0f,
	-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
	 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
};
// clang-format on

FramebufferManagerGX2::FramebufferManagerGX2(Draw::DrawContext *draw)
	: FramebufferManagerCommon(draw) {
	context_ = (GX2ContextState *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	quadFetchShader_.size = GX2CalcFetchShaderSize(ARRAY_SIZE(g_QuadAttribStream));
	quadFetchShader_.program = (u8 *)MEM2_alloc(quadFetchShader_.size, GX2_SHADER_ALIGNMENT);
	GX2InitFetchShader(&quadFetchShader_, quadFetchShader_.program, ARRAY_SIZE(g_QuadAttribStream), g_QuadAttribStream);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, quadFetchShader_.program, quadFetchShader_.size);

	quadBuffer_ = (float*)MEM2_alloc(quadStride_ * 4, GX2_VERTEX_BUFFER_ALIGNMENT);

	for (int i = 0; i < 256; i++) {
		GX2InitStencilMaskReg(&stencilMaskStates_[i], i, 0xFF, 0xFF, i, 0xFF, 0xFF);
	}

	ShaderTranslationInit();

//	presentation_->SetLanguage(GLSL_300);
//	preferredPixelsFormat_ = Draw::DataFormat::B8G8R8A8_UNORM;
}

FramebufferManagerGX2::~FramebufferManagerGX2() {
	ShaderTranslationShutdown();

	// Drawing cleanup
	MEM2_free(quadFetchShader_.program);
	MEM2_free(quadBuffer_);

	// Stencil cleanup
	if (stencilValueBuffer_)
		MEM2_free(stencilValueBuffer_);
}

void FramebufferManagerGX2::SetTextureCache(TextureCacheGX2 *tc) {
	textureCacheGX2_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerGX2::SetShaderManager(ShaderManagerGX2 *sm) {
	shaderManagerGX2_ = sm;
	shaderManager_ = sm;
}

void FramebufferManagerGX2::SetDrawEngine(DrawEngineGX2 *td) {
	drawEngineGX2_ = td;
	drawEngine_ = td;
}

void FramebufferManagerGX2::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
	struct Coord {
		Lin::Vec3 pos; float u, v;
	};
	Coord coord[4] = {
		{ { x, y, 0 }, u0, v0 },
		{ { x + w, y, 0 }, u1, v0 },
		{ { x + w, y + h, 0 }, u1, v1 },
		{ { x, y + h, 0 }, u0, v1 },
	};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 1; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 3; break;
		}
		for (int i = 0; i < 4; i++) {
			temp[i * 2] = coord[((i + rotation) & 3)].u;
			temp[i * 2 + 1] = coord[((i + rotation) & 3)].v;
		}

		for (int i = 0; i < 4; i++) {
			coord[i].u = temp[i * 2];
			coord[i].v = temp[i * 2 + 1];
		}
	}

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		coord[i].pos.x = coord[i].pos.x * invDestW - 1.0f;
		coord[i].pos.y = -(coord[i].pos.y * invDestH - 1.0f);
	}

	if (g_display_rotation != DisplayRotation::ROTATE_0) {
		for (int i = 0; i < 4; i++) {
			// backwards notation, should fix that...
			coord[i].pos = coord[i].pos * g_display_rot_matrix;
		}
	}

	// The above code is for FAN geometry but we can only do STRIP. So rearrange it a little.
	memcpy(quadBuffer_, coord, sizeof(Coord));
	memcpy(quadBuffer_ + 5, coord + 1, sizeof(Coord));
	memcpy(quadBuffer_ + 10, coord + 3, sizeof(Coord));
	memcpy(quadBuffer_ + 15, coord + 2, sizeof(Coord));
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, quadBuffer_, sizeof(coord));

	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
	GX2SetColorControlReg(&StockGX2::blendDisabledColorWrite);
	GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0xF]);
	GX2SetDepthStencilControlReg(&StockGX2::depthStencilDisabled);
	GX2SetPixelSampler((flags & DRAWTEX_LINEAR) ? &StockGX2::samplerLinear2DClamp : &StockGX2::samplerPoint2DClamp, 0);
	GX2SetAttribBuffer(0, sizeof(coord), sizeof(*coord), quadBuffer_);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
}

void FramebufferManagerGX2::Bind2DShader() {
	GX2SetFetchShader(&quadFetchShader_);
	GX2SetPixelShader(&defPShaderGX2);
	GX2SetVertexShader(&defVShaderGX2);
}

void FramebufferManagerGX2::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
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
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "ReformatFramebuffer");

		// TODO: There's no way this does anything useful :(
		GX2SetDepthStencilControlReg(&StockGX2::depthDisabledStencilWrite);
		GX2SetStencilMask(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF); // TODO, and maybe GX2SetStencilMaskReg?
		GX2SetColorControlReg(&StockGX2::blendColorDisabled);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
		GX2SetFetchShader(&quadFetchShader_);
		GX2SetPixelShader(&defPShaderGX2);
		GX2SetVertexShader(&defVShaderGX2);
		GX2SetAttribBuffer(0, sizeof(fsQuadBuffer_), quadStride_, fsQuadBuffer_);
		GX2SetPixelSampler(&StockGX2::samplerPoint2DClamp, 0);
//		GX2SetPixelTexture(nullptr, 0);
		shaderManagerGX2_->DirtyLastShader();
		GX2SetViewport( 0.0f, 0.0f, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f);
		GX2SetScissor(0, 0, vfb->renderWidth, vfb->renderHeight);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);

		textureCache_->ForgetLastTexture();
		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE);
	}
}

static void CopyPixelDepthOnly(u32 *dstp, const u32 *srcp, size_t c) {
	for (size_t x = 0; x < c; ++x) {
		memcpy(dstp + x, srcp + x, 3);
	}
}

void FramebufferManagerGX2::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		// GX2SetPixelTexture(nullptr, 1); // TODO: what is the correct way to unbind a texture ?
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
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
	} else if (framebuffer != currentRenderVfb_) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
	} else {
		ERROR_LOG_REPORT_ONCE(GX2SelfTexture, G3D, "Attempting to texture from target (src=%08x / target=%08x / flags=%d)", framebuffer->fb_address, currentRenderVfb_->fb_address, flags);
		// Badness on GX2 to bind the currently rendered-to framebuffer as a texture.
		// GX2SetPixelTexture(nullptr, 1); // TODO: what is the correct way to unbind a texture ?
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}
}

void FramebufferManagerGX2::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// Nothing to do here.
}

void FramebufferManagerGX2::SimpleBlit(
	Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
	Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2, bool linearFilter) {

	int destW, destH, srcW, srcH;
	draw_->GetFramebufferDimensions(src, &srcW, &srcH);
	draw_->GetFramebufferDimensions(dest, &destW, &destH);

	if (srcW == destW && srcH == destH && destX2 - destX1 == srcX2 - srcX1 && destY2 - destY1 == srcY2 - srcY1) {
		// Optimize to a copy
		draw_->CopyFramebufferImage(src, 0, (int)srcX1, (int)srcY1, 0, dest, 0, (int)destX1, (int)destY1, 0, (int)(srcX2 - srcX1), (int)(srcY2 - srcY1), 1, Draw::FB_COLOR_BIT, "SimpleBlit");
		return;
	}

	float dX = 1.0f / (float)destW;
	float dY = 1.0f / (float)destH;
	float sX = 1.0f / (float)srcW;
	float sY = 1.0f / (float)srcH;
	struct Vtx {
		float x, y, z, u, v;
	};
	Vtx vtx[4] = {
		{ -1.0f + 2.0f * dX * destX1, 1.0f - 2.0f * dY * destY1, 0.0f, sX * srcX1, sY * srcY1 },
		{ -1.0f + 2.0f * dX * destX2, 1.0f - 2.0f * dY * destY1, 0.0f, sX * srcX2, sY * srcY1 },
		{ -1.0f + 2.0f * dX * destX1, 1.0f - 2.0f * dY * destY2, 0.0f, sX * srcX1, sY * srcY2 },
		{ -1.0f + 2.0f * dX * destX2, 1.0f - 2.0f * dY * destY2, 0.0f, sX * srcX2, sY * srcY2 },
	};

	memcpy(quadBuffer_, vtx, 4 * sizeof(Vtx));
	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, quadBuffer_, 4 * sizeof(Vtx));

	// Unbind the texture first to avoid the GX2 hazard check (can't set render target to things bound as textures and vice versa, not even temporarily).
	draw_->BindTexture(0, nullptr);
	draw_->BindFramebufferAsRenderTarget(dest, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "SimpleBlit");
	draw_->BindFramebufferAsTexture(src, 0, Draw::FB_COLOR_BIT, 0);

	Bind2DShader();
	GX2SetViewport( 0.0f, 0.0f, (float)destW, (float)destH, 0.0f, 1.0f );
	GX2SetScissor(0, 0, destW, destH);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
	GX2SetColorControlReg(&StockGX2::blendDisabledColorWrite);
	GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0xF]);
	GX2SetDepthStencilControlReg(&StockGX2::depthStencilDisabled);
	GX2SetPixelSampler(linearFilter ? &StockGX2::samplerLinear2DClamp : &StockGX2::samplerPoint2DClamp, 0);
	GX2SetAttribBuffer(0, 4 * sizeof(Vtx), sizeof(Vtx), quadBuffer_);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE);
}

void FramebufferManagerGX2::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "BlitFramebuffer_Fail");
		}
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

	// Direct3D doesn't support rect -> self.
	Draw::Framebuffer *srcFBO = src->fbo;
	if (src == dst) {
		Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::BLIT, src->renderWidth, src->renderHeight, (Draw::FBColorDepth)src->colorDepth);
		SimpleBlit(tempFBO, dstX1, dstY1, dstX2, dstY2, src->fbo, srcX1, srcY1, srcX2, srcY2, false);
		srcFBO = tempFBO;
	}
	SimpleBlit(dst->fbo, dstX1, dstY1, dstX2, dstY2, srcFBO, srcX1, srcY1, srcX2, srcY2, false);
}

// Nobody calls this yet.
void FramebufferManagerGX2::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	const u32 z_address = vfb->z_address;
	// TODO
}

void FramebufferManagerGX2::EndFrame() {}

void FramebufferManagerGX2::DeviceLost() { DestroyAllFBOs(); }

