// Copyright (c) 2012- PPSSPP Project.

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

#include <map>
#include <algorithm>
#include <cassert>
#include <cstring>

#include <wiiu/gx2.h>

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Common/Profiler/Profiler.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GX2/FragmentShaderGeneratorGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/DepalettizeShaderGX2.h"
#include "GPU/GX2/GX2Util.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Host.h"

#include "ext/xxhash.h"
#include "Common/Math/math_util.h"

#define INVALID_TEX (GX2Texture *)(-1LL)

SamplerCacheGX2::~SamplerCacheGX2() {
	for (auto &iter : cache_) {
		delete iter.second;
	}
}

GX2Sampler *SamplerCacheGX2::GetOrCreateSampler(const SamplerCacheKey &key) {
	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		return iter->second;
	}
	GX2Sampler *sampler = new GX2Sampler();

	GX2TexClampMode sClamp = key.sClamp ? GX2_TEX_CLAMP_MODE_CLAMP : GX2_TEX_CLAMP_MODE_WRAP;
	GX2TexClampMode tClamp = key.tClamp ? GX2_TEX_CLAMP_MODE_CLAMP : GX2_TEX_CLAMP_MODE_WRAP;
	GX2InitSampler(sampler, sClamp, key.magFilt ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSamplerClamping(sampler, sClamp, tClamp, sClamp);
	// TODO: GX2TexAnisoRatio ?
	GX2InitSamplerXYFilter(sampler, key.minFilt ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT, key.magFilt ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT, GX2_TEX_ANISO_RATIO_NONE);
	GX2InitSamplerZMFilter(sampler, GX2_TEX_Z_FILTER_MODE_POINT, key.mipFilt ? GX2_TEX_MIP_FILTER_MODE_LINEAR : GX2_TEX_MIP_FILTER_MODE_POINT);
	GX2InitSamplerBorderType(sampler, GX2_TEX_BORDER_TYPE_WHITE);

	cache_[key] = sampler;
	return sampler;
}

TextureCacheGX2::TextureCacheGX2(Draw::DrawContext *draw) : TextureCacheCommon(draw) {
	context_ = (GX2ContextState *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	isBgraBackend_ = true;
	lastBoundTexture = INVALID_TEX;

	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheGX2::~TextureCacheGX2() {
	// pFramebufferVertexDecl->Release();
	Clear(true);
}

void TextureCacheGX2::SetFramebufferManager(FramebufferManagerGX2 *fbManager) {
	framebufferManagerGX2_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheGX2::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	GX2Texture *texture = (GX2Texture *)entry->texturePtr;
	if (texture) {
		if (delete_them) {
			MEM2_free(texture->surface.image);
			delete texture;
		}
		entry->texturePtr = nullptr;
	}
}

void TextureCacheGX2::ForgetLastTexture() {
	InvalidateLastTexture();
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	//	GX2SetPixelTexture(nullptr, 0);
}

void TextureCacheGX2::InvalidateLastTexture() {
	lastBoundTexture = INVALID_TEX;
}

void TextureCacheGX2::StartFrame() {
	InvalidateLastTexture();
	timesInvalidatedAllThisFrame_ = 0;

	if (texelsScaledThisFrame_) {
		// INFO_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
	texelsScaledThisFrame_ = 0;
	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		Decimate();
	}
}

void TextureCacheGX2::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutBase * (clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	if (replacer_.Enabled())
		clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	else
		clutHash_ = XXH3_64bits((const char *)clutBufRaw_, clutExtendedBytes) & 0xFFFFFFFF;
	clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0x0FFF;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | (i << 12);
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

void TextureCacheGX2::BindTexture(TexCacheEntry *entry) {
	GX2Texture *texture = (GX2Texture *)entry->texturePtr;
	if (texture != lastBoundTexture) {
		GX2SetPixelTexture(texture, 0);
		lastBoundTexture = texture;
	}
	int maxLevel = (entry->status & TexCacheEntry::STATUS_BAD_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry->addr);
	GX2Sampler *sampler = samplerCache_.GetOrCreateSampler(samplerKey);
	GX2SetPixelSampler(sampler, 0);
}

void TextureCacheGX2::Unbind() {
	//	GX2SetPixelTexture(nullptr, 0);
	InvalidateLastTexture();
}

class TextureShaderApplierGX2 {
public:
	struct Pos {
		Pos(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		Pos() {}

		float x;
		float y;
		float z;
	};
	struct UV {
		UV(float u_, float v_) : u(u_), v(v_) {}
		UV() {}

		float u;
		float v;
	};

	struct PosUV {
		Pos pos;
		UV uv;
	};

	TextureShaderApplierGX2(GX2ContextState *context, GX2PixelShader *pshader, void *dynamicBuffer, float bufferW, float bufferH, int renderW, int renderH, float xoff, float yoff) : context_(context), pshader_(pshader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{ -1, 1, 0 },
			{ 1, 1, 0 },
			{ -1, -1, 0 },
			{ 1, -1, 0 },
		};
		static const UV uv[4] = {
			{ 0, 0 },
			{ 1, 0 },
			{ 0, 1 },
			{ 1, 1 },
		};

		for (int i = 0; i < 4; ++i) {
			verts_[i].pos = pos[i];
			verts_[i].pos.x += xoff;
			verts_[i].pos.y += yoff;
			verts_[i].uv = uv[i];
		}
		memcpy(dynamicBuffer, &verts_[0], 4 * 5 * sizeof(float));
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dynamicBuffer, 4 * 5 * sizeof(float));
		vbuffer_ = dynamicBuffer;
	}

	void ApplyBounds(const KnownVertexBounds &bounds, u32 uoff, u32 voff, float xoff, float yoff) {
		// If min is not < max, then we don't have values (wasn't set during decode.)
		if (bounds.minV < bounds.maxV) {
			const float invWidth = 1.0f / bufferW_;
			const float invHeight = 1.0f / bufferH_;
			// Inverse of half = double.
			const float invHalfWidth = invWidth * 2.0f;
			const float invHalfHeight = invHeight * 2.0f;

			const int u1 = bounds.minU + uoff;
			const int v1 = bounds.minV + voff;
			const int u2 = bounds.maxU + uoff;
			const int v2 = bounds.maxV + voff;

			const float left = u1 * invHalfWidth - 1.0f + xoff;
			const float right = u2 * invHalfWidth - 1.0f + xoff;
			const float top = v1 * invHalfHeight - 1.0f + yoff;
			const float bottom = v2 * invHalfHeight - 1.0f + yoff;
			float z = 0.0f;
			// Points are: BL, BR, TL, TR.
			verts_[0].pos = Pos(left, bottom, z);
			verts_[1].pos = Pos(right, bottom, z);
			verts_[2].pos = Pos(left, top, z);
			verts_[3].pos = Pos(right, top, z);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			verts_[0].uv = UV(uvleft, uvbottom);
			verts_[1].uv = UV(uvright, uvbottom);
			verts_[2].uv = UV(uvleft, uvtop);
			verts_[3].uv = UV(uvright, uvtop);

			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}
	}

	void Use(GX2VertexShader *vshader, GX2FetchShader *fshader) {
		GX2SetPixelShader(pshader_);
		GX2SetVertexShader(vshader);
		GX2SetFetchShader(fshader);
	}

	void Shade() {
		GX2SetViewport(0.0f, 0.0f, (float)renderW_, (float)renderH_, 0.0f, 1.0f);
		GX2SetScissor(0, 0, renderW_, renderH_);
		GX2SetColorControlReg(&StockGX2::blendDisabledColorWrite);
		GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0xF]);
		GX2SetDepthStencilControlReg(&StockGX2::depthStencilDisabled);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
		GX2SetAttribBuffer(0, 4 * stride_, stride_, (u8*)vbuffer_ + offset_);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
	}

protected:
	GX2ContextState *context_;
	GX2PixelShader *pshader_;
	void *vbuffer_;
	PosUV verts_[4];
	u32 stride_ = sizeof(PosUV);
	u32 offset_ = 0;
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCacheGX2::ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, FramebufferNotificationChannel channel) {
	GX2PixelShader *pshader = nullptr;
	u32 clutMode = gstate.clutformat & 0xFFFFFF;
	bool need_depalettize = IsClutFormat(texFormat);
	if (need_depalettize && !g_Config.bDisableSlowFramebufEffects) {
		pshader = depalShaderCache_->GetDepalettizePixelShader(clutMode, framebuffer->drawnFormat);
	}

	if (pshader) {
		bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS);
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		GX2Texture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_, expand32);

		Draw::Framebuffer *depalFBO = framebufferManagerGX2_->GetTempFBO(TempFBO::DEPAL, framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		shaderManager_->DirtyLastShader();
//		draw_->BindPipeline(nullptr);

		// Not sure why or if we need this here - we're not about to actually draw using draw_, just use its framebuffer binds.
		draw_->InvalidateCachedState();

		float xoff = -0.5f / framebuffer->renderWidth;
		float yoff = 0.5f / framebuffer->renderHeight;

		TextureShaderApplierGX2 shaderApply(context_, pshader, framebufferManagerGX2_->GetDynamicQuadBuffer(), framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight, xoff, yoff);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset, xoff, yoff);
		shaderApply.Use(depalShaderCache_->GetDepalettizeVertexShader(), depalShaderCache_->GetFetchShader());

		GX2SetPixelTexture(clutTexture, 1);
		framebufferManagerGX2_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		GX2SetPixelSampler(&StockGX2::samplerPoint2DWrap, 0);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "ApplyTextureFramebuffer");
		shaderApply.Shade();

		framebufferManagerGX2_->RebindFramebuffer("RebindFramebuffer - ApplyTextureFramebuffer");
		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, GetClutDestFormatGX2(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
	} else {
		framebufferManagerGX2_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
		framebufferManagerGX2_->RebindFramebuffer("ApplyTextureFramebuffer"); // Probably not necessary.
	}

	SamplerCacheKey samplerKey = GetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
	GX2Sampler *sampler = samplerCache_.GetOrCreateSampler(samplerKey);
	GX2SetPixelSampler(sampler, 0);

	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void TextureCacheGX2::BuildTexture(TexCacheEntry *const entry) {
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	if ((entry->bufw == 0 || (gstate.texbufwidth[0] & 0xf800) != 0) && entry->addr >= PSP_GetKernelMemoryEnd()) {
		ERROR_LOG_REPORT(G3D, "Texture with unexpected bufw (full=%d)", gstate.texbufwidth[0] & 0xffff);
		// Proceeding here can cause a crash.
		return;
	}

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	int maxLevel = entry->maxLevel;
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}

		// If size reaches 1, stop, and override maxlevel.
		int tw = gstate.getTextureWidth(i);
		int th = gstate.getTextureHeight(i);
		if (tw == 1 || th == 1) {
			maxLevel = i;
			break;
		}

		if (i > 0 && gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (tw != 1 && tw != (gstate.getTextureWidth(i - 1) >> 1))
				badMipSizes = true;
			else if (th != 1 && th != (gstate.getTextureHeight(i - 1) >> 1))
				badMipSizes = true;
		}
	}

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	u64 cachekey = replacer_.Enabled() ? entry->CacheKey() : 0;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = replacer_.FindReplacement(cachekey, entry->fullhash, w, h);
	if (replaced.GetSize(0, w, h)) {
		// We're replacing, so we won't scale.
		scaleFactor = 1;
		entry->status |= TexCacheEntry::STATUS_IS_SCALED;
		maxLevel = replaced.MaxLevel();
		badMipSizes = false;
	}

	// Don't scale the PPGe texture.
	if (entry->addr > 0x05000000 && entry->addr < PSP_GetKernelMemoryEnd())
		scaleFactor = 1;
	if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) != 0 && scaleFactor != 1) {
		// Remember for later that we /wanted/ to scale this texture.
		entry->status |= TexCacheEntry::STATUS_TO_SCALE;
		scaleFactor = 1;
	}

	if (scaleFactor != 1) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED) {
			entry->status |= TexCacheEntry::STATUS_TO_SCALE;
			scaleFactor = 1;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_TO_SCALE;
			entry->status |= TexCacheEntry::STATUS_IS_SCALED;
			texelsScaledThisFrame_ += w * h;
		}
	}

	// Seems to cause problems in Tactics Ogre.
	if (badMipSizes) {
		maxLevel = 0;
	}

	GX2SurfaceFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	if (IsFakeMipmapChange()) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		u8 level = std::max(0, gstate.getTexLevelOffset16() / 16);
		LoadTextureLevel(*entry, replaced, level, maxLevel, scaleFactor, dstFmt);
	} else {
		LoadTextureLevel(*entry, replaced, 0, maxLevel, scaleFactor, dstFmt);
	}

	if (!entry->texturePtr) {
		return;
	}

	// Mipmapping is only enabled when texture scaling is disabled.
	if (maxLevel > 0 && scaleFactor == 1) {
		for (int i = 1; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, replaced, i, maxLevel, scaleFactor, dstFmt);
		}
	}

	if (maxLevel == 0) {
		entry->status |= TexCacheEntry::STATUS_BAD_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_BAD_MIPS;
	}
	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(replaced.AlphaStatus()));
	}
}

GX2SurfaceFormat GetClutDestFormatGX2(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444: return GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
	case GE_CMODE_16BIT_ABGR5551: return GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
	case GE_CMODE_16BIT_BGR5650: return GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
	case GE_CMODE_32BIT_ABGR8888: return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	}
	// Should never be here !
	return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
}

GX2SurfaceFormat TextureCacheGX2::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	if (!gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS)) {
		return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	}

	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32: return GetClutDestFormatGX2(clutFormat);
	case GE_TFMT_4444: return GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
	case GE_TFMT_5551: return GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
	case GE_TFMT_5650: return GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default: return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	}
}

TexCacheEntry::TexStatus TextureCacheGX2::CheckAlpha(const u32_le *pixelData, u32 dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4: res = CheckAlphaRGBA4444Basic(pixelData, stride, w, h); break;
	case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1: res = CheckAlphaRGBA5551Basic(pixelData, stride, w, h); break;
	case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default: res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h); break;
	}

	return (TexCacheEntry::TexStatus)res;
}

ReplacedTextureFormat FromGX2Format(u32 fmt) {
	switch (fmt) {
	case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5: return ReplacedTextureFormat::F_5650;
	case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1: return ReplacedTextureFormat::F_5551;
	case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4: return ReplacedTextureFormat::F_4444;
	case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
	default: return ReplacedTextureFormat::F_8888;
	}
}

GX2SurfaceFormat ToGX2Format(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
	case ReplacedTextureFormat::F_5551: return GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
	case ReplacedTextureFormat::F_4444: return GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
	case ReplacedTextureFormat::F_8888:
	default: return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	}
}

void TextureCacheGX2::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, int scaleFactor, GX2SurfaceFormat dstFmt) {
	PROFILE_THIS_SCOPE("decodetex");
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	GX2Texture *texture = GX2Tex(&entry);
	if ((level == 0 || IsFakeMipmapChange()) && texture == nullptr) {
		// Create texture
		int levels = scaleFactor == 1 ? maxLevel + 1 : 1;
		int tw = w, th = h;
		GX2SurfaceFormat tfmt = dstFmt;
		if (replaced.GetSize(level, tw, th)) {
			tfmt = ToGX2Format(replaced.Format(level));
		} else {
			tw *= scaleFactor;
			th *= scaleFactor;
			if (scaleFactor > 1) {
				tfmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
			}
		}

		texture = new GX2Texture();
		texture->surface.width = tw;
		texture->surface.height = th;
		texture->surface.depth = 1;
		texture->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		texture->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		texture->surface.use = GX2_SURFACE_USE_TEXTURE;
		texture->viewNumSlices = 1;
		texture->surface.format = tfmt;
		switch(tfmt)
		{
		case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4:
			texture->compMap = GX2_COMP_SEL(_r, _g, _b, _a);
			break;
		case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1:
			texture->compMap = GX2_COMP_SEL(_r, _g, _b, _a);
			break;
		case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5:
			texture->compMap = GX2_COMP_SEL(_r, _g, _b, _1);
			break;
		default:
		case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
			if (dstFmt == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8) {
				texture->compMap = GX2_COMP_SEL(_r, _g, _b, _a);
			} else {
				// scaled 16-bit textures end up native-endian.
				texture->compMap = GX2_COMP_SEL(_a, _b, _g, _r);
			}
			break;
		}
#if 0 // TODO: mipmapping
		texture->surface.mipLevels = IsFakeMipmapChange() ? 1 : levels;
#endif

		GX2CalcSurfaceSizeAndAlignment(&texture->surface);
		GX2InitTextureRegs(texture);
		texture->surface.image = MEM2_alloc(texture->surface.imageSize, texture->surface.alignment);
		_assert_(texture->surface.image);

		entry.texturePtr = texture;
	}

	gpuStats.numTexturesDecoded++;

	u32 *mapData = (u32*)texture->surface.image;
	int mapRowPitch = texture->surface.pitch * ((texture->surface.format == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8)? 4 : 2);
	if (replaced.GetSize(level, w, h)) {
		replaced.Load(level, mapData, mapRowPitch);
		dstFmt = ToGX2Format(replaced.Format(level));
	} else {
		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 ? 4 : 2;
		u32 *pixelData;
		int decPitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		} else {
			pixelData = (u32 *)mapData;
			decPitch = mapRowPitch;
		}
		DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, false, false);

		// We check before scaling since scaling shouldn't invent alpha from a full alpha texture.
		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha((u32_le *)pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (scaleFactor > 1) {
			u32 scaleFmt = (u32)dstFmt;
			scaler.ScaleAlways((u32 *)mapData, pixelData, scaleFmt, w, h, scaleFactor);
			pixelData = (u32 *)mapData;

			// We always end up at 8888.  Other parts assume this.
			assert(scaleFmt == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != mapRowPitch) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				for (int y = h - 1; y >= 0; --y) {
					memcpy((u8 *)mapData + mapRowPitch * y, (u8 *)mapData + decPitch * y, w * bpp);
				}
				decPitch = mapRowPitch;
			}
		}

		if (replacer_.Enabled()) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = videos_.find(entry.addr & 0x3FFFFFFF) != videos_.end();
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.scaleFactor = scaleFactor;
			replacedInfo.fmt = FromGX2Format(dstFmt);

			replacer_.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}
#if 0 // TODO: mipmapping
	if (IsFakeMipmapChange())
		context_->UpdateSubresource(texture, 0, nullptr, mapData, mapRowPitch, 0);
	else
		context_->UpdateSubresource(texture, level, nullptr, mapData, mapRowPitch, 0);
#endif
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, texture->surface.image, texture->surface.imageSize);
}

bool TextureCacheGX2::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	SetTexture();
	if (!nextTexture_) {
		if (nextFramebufferTexture_) {
			VirtualFramebuffer *vfb = nextFramebufferTexture_;
			buffer.Allocate(vfb->bufferWidth, vfb->bufferHeight, GPU_DBG_FORMAT_8888, false);
			bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->bufferWidth, vfb->bufferHeight, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), vfb->bufferWidth, "GetCurrentTextureDebug");
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
			// We may have blitted to a temp FBO.
			framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
			return retval;
		} else {
			return false;
		}
	}

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	ApplyTexture();

	GX2Texture *texture = (GX2Texture *)entry->texturePtr;
	if (!texture)
		return false;

	if (texture->surface.format != GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8) {
		// TODO: Support the other formats
		return false;
	}
#if 0 // TODO: mipmapping
	int width = texture->surface.width >> level;
	int height = texture->surface.height >> level;
#else
	int width = texture->surface.width;
	int height = texture->surface.height;
#endif
	buffer.Allocate(width, height, GPU_DBG_FORMAT_8888);

	for (int y = 0; y < height; y++) {
		memcpy(buffer.GetData() + 4 * width * y, (const uint8_t *)texture->surface.image + texture->surface.pitch * y, 4 * width);
	}

	return true;
}
