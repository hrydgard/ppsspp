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

#include <algorithm>
#include <cstring>

#include "ext/xxhash.h"
#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/math_util.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/GLES/DepalettizeShaderGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/Common/TextureDecoder.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

TextureCacheGLES::TextureCacheGLES(Draw::DrawContext *draw)
	: TextureCacheCommon(draw) {
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	nextTexture_ = nullptr;

	std::vector<GLRInputLayout::Entry> entries;
	entries.push_back({ 0, 3, GL_FLOAT, GL_FALSE, 20, 0 });
	entries.push_back({ 1, 2, GL_FLOAT, GL_FALSE, 20, 12 });
	shadeInputLayout_ = render_->CreateInputLayout(entries);
}

TextureCacheGLES::~TextureCacheGLES() {
	if (shadeInputLayout_) {
		render_->DeleteInputLayout(shadeInputLayout_);
	}
	Clear(true);
}

void TextureCacheGLES::SetFramebufferManager(FramebufferManagerGLES *fbManager) {
	framebufferManagerGL_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheGLES::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	if (delete_them) {
		if (entry->textureName) {
			render_->DeleteTexture(entry->textureName);
		}
	}
	entry->textureName = nullptr;
}

void TextureCacheGLES::Clear(bool delete_them) {
	TextureCacheCommon::Clear(delete_them);
}

Draw::DataFormat getClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
	case GE_CMODE_16BIT_ABGR5551:
		return Draw::DataFormat::R5G5B5A1_UNORM_PACK16;
	case GE_CMODE_16BIT_BGR5650:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_CMODE_32BIT_ABGR8888:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}
	return Draw::DataFormat::UNDEFINED;;
}

static const GLuint MinFiltGL[8] = {
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR,
};

static const GLuint MagFiltGL[2] = {
	GL_NEAREST,
	GL_LINEAR
};

void TextureCacheGLES::ApplySamplingParams(const SamplerCacheKey &key) {
	if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		float minLod = (float)key.minLevel / 256.0f;
		float maxLod = (float)key.maxLevel / 256.0f;
		float lodBias = (float)key.lodBias / 256.0f;
		render_->SetTextureLod(0, minLod, maxLod, lodBias);
	}

	float aniso = 0.0f;
	int minKey = ((int)key.mipEnable << 2) | ((int)key.mipFilt << 1) | ((int)key.minFilt);
	render_->SetTextureSampler(0,
		key.sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, key.tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT,
		key.magFilt ? GL_LINEAR : GL_NEAREST, MinFiltGL[minKey], aniso);
}

static void ConvertColors(void *dstBuf, const void *srcBuf, Draw::DataFormat dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	switch (dstFmt) {
	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16:
		ConvertRGBA4444ToABGR4444((u16 *)dst, (const u16 *)src, numPixels);
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case Draw::DataFormat::R5G5B5A1_UNORM_PACK16:
		ConvertRGBA5551ToABGR1555((u16 *)dst, (const u16 *)src, numPixels);
		break;
	case Draw::DataFormat::R5G6B5_UNORM_PACK16:
		ConvertRGB565ToBGR565((u16 *)dst, (const u16 *)src, numPixels);
		break;
	default:
		// No need to convert RGBA8888, right order already
		if (dst != src)
			memcpy(dst, src, numPixels * sizeof(u32));
		break;
	}
}

void TextureCacheGLES::StartFrame() {
	InvalidateLastTexture();
	timesInvalidatedAllThisFrame_ = 0;
	replacementTimeThisFrame_ = 0.0;

	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	if (!lowMemoryMode_ && renderManager->SawOutOfMemory()) {
		lowMemoryMode_ = true;
		decimationCounter_ = 0;

		auto err = GetI18NCategory("Error");
		if (standardScaleFactor_ > 1) {
			host->NotifyUserMessage(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
		} else {
			host->NotifyUserMessage(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
		}
	}

	if (texelsScaledThisFrame_) {
		VERBOSE_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
	texelsScaledThisFrame_ = 0;
	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		Decimate();
	}
}

void TextureCacheGLES::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
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

	// Avoid a copy when we don't need to convert colors.
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		const int numColors = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutMaxBytes_ / sizeof(u32)) : (clutMaxBytes_ / sizeof(u16));
		ConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), numColors);
		clutBuf_ = clutBufConverted_;
	} else {
		clutBuf_ = clutBufRaw_;
	}

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0xFFF0;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | i;
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

void TextureCacheGLES::BindTexture(TexCacheEntry *entry) {
	if (entry->textureName != lastBoundTexture) {
		render_->BindTexture(0, entry->textureName);
		lastBoundTexture = entry->textureName;
	}
	int maxLevel = (entry->status & TexCacheEntry::STATUS_BAD_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry);
	ApplySamplingParams(samplerKey);
	gstate_c.SetUseShaderDepal(false);
}

void TextureCacheGLES::Unbind() {
	render_->BindTexture(TEX_SLOT_PSP_TEXTURE, nullptr);
	InvalidateLastTexture();
}

class TextureShaderApplier {
public:
	struct Pos {
		float x;
		float y;
		float z;
	};
	struct UV {
		float u;
		float v;
	};

	TextureShaderApplier(DepalShader *shader, float bufferW, float bufferH, int renderW, int renderH)
		: shader_(shader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{-1, -1, -1},
			{ 1, -1, -1},
			{ 1,  1, -1},
			{-1,  1, -1},
		};
		memcpy(pos_, pos, sizeof(pos_));

		static const UV uv[4] = {
			{0, 0},
			{1, 0},
			{1, 1},
			{0, 1},
		};
		memcpy(uv_, uv, sizeof(uv_));
	}

	void ApplyBounds(const KnownVertexBounds &bounds, u32 uoff, u32 voff) {
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

			const float left = u1 * invHalfWidth - 1.0f;
			const float right = u2 * invHalfWidth - 1.0f;
			const float top = v1 * invHalfHeight - 1.0f;
			const float bottom = v2 * invHalfHeight - 1.0f;
			// Points are: BL, BR, TR, TL.
			pos_[0] = Pos{ left, bottom, -1.0f };
			pos_[1] = Pos{ right, bottom, -1.0f };
			pos_[2] = Pos{ right, top, -1.0f };
			pos_[3] = Pos{ left, top, -1.0f };

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			uv_[0] = UV{ uvleft, uvbottom };
			uv_[1] = UV{ uvright, uvbottom };
			uv_[2] = UV{ uvright, uvtop };
			uv_[3] = UV{ uvleft, uvtop };

			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}
	}

	void Use(GLRenderManager *render, DrawEngineGLES *transformDraw, GLRInputLayout *inputLayout) {
		render->BindProgram(shader_->program);
		struct SimpleVertex {
			float pos[3];
			float uv[2];
		};
		uint32_t bindOffset;
		GLRBuffer *bindBuffer;
		SimpleVertex *verts = (SimpleVertex *)transformDraw->GetPushVertexBuffer()->Push(sizeof(SimpleVertex) * 4, &bindOffset, &bindBuffer);
		int order[4] = { 0 ,1, 3, 2 };
		for (int i = 0; i < 4; i++) {
			memcpy(verts[i].pos, &pos_[order[i]], sizeof(Pos));
			memcpy(verts[i].uv, &uv_[order[i]], sizeof(UV));
		}
		render->BindVertexBuffer(inputLayout, bindBuffer, bindOffset);
	}

	void Shade(GLRenderManager *render) {
		render->SetViewport(GLRViewport{ 0, 0, (float)renderW_, (float)renderH_, 0.0f, 1.0f });
		render->Draw(GL_TRIANGLE_STRIP, 0, 4);
	}

protected:
	DepalShader *shader_;
	Pos pos_[4];
	UV uv_[4];
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCacheGLES::ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, FramebufferNotificationChannel channel) {
	DepalShader *depalShader = nullptr;
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;
	bool need_depalettize = IsClutFormat(texFormat);

	bool depth = channel == NOTIFY_FB_DEPTH;
	bool useShaderDepal = framebufferManager_->GetCurrentRenderVFB() != framebuffer && (gstate_c.Supports(GPU_SUPPORTS_GLSL_ES_300) || gstate_c.Supports(GPU_SUPPORTS_GLSL_330)) && !depth;
	if (!gstate_c.Supports(GPU_SUPPORTS_32BIT_INT_FSHADER)) {
		useShaderDepal = false;
		depth = false;  // Can't support this
	}

	if (need_depalettize && !g_Config.bDisableSlowFramebufEffects) {
		if (useShaderDepal) {
			const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
			GLRTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
			render_->BindTexture(TEX_SLOT_CLUT, clutTexture);
			render_->SetTextureSampler(TEX_SLOT_CLUT, GL_REPEAT, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, 0.0f);
			framebufferManagerGL_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);
			SamplerCacheKey samplerKey = GetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
			samplerKey.magFilt = false;
			samplerKey.minFilt = false;
			samplerKey.mipEnable = false;
			ApplySamplingParams(samplerKey);
			InvalidateLastTexture();

			// Since we started/ended render passes, might need these.
			gstate_c.Dirty(DIRTY_DEPAL);
			gstate_c.SetUseShaderDepal(true);
			gstate_c.depalFramebufferFormat = framebuffer->drawnFormat;
			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;
			CheckAlphaResult alphaStatus = CheckAlpha((const uint8_t *)clutBuf_, getClutDestFormat(clutFormat), clutTotalColors);
			gstate_c.SetTextureFullAlpha(alphaStatus == CHECKALPHA_FULL);
			return;
		}

		depalShader = depalShaderCache_->GetDepalettizeShader(clutMode, depth ? GE_FORMAT_DEPTH16 : framebuffer->drawnFormat);
		gstate_c.SetUseShaderDepal(false);
	}
	if (depalShader) {
		shaderManager_->DirtyLastShader();

		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		GLRTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
		Draw::Framebuffer *depalFBO = framebufferManagerGL_->GetTempFBO(TempFBO::DEPAL, framebuffer->renderWidth, framebuffer->renderHeight);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "Depal");

		render_->SetScissor(GLRect2D{ 0, 0, (int)framebuffer->renderWidth, (int)framebuffer->renderHeight });
		render_->SetViewport(GLRViewport{ 0.0f, 0.0f, (float)framebuffer->renderWidth, (float)framebuffer->renderHeight, 0.0f, 1.0f });
		TextureShaderApplier shaderApply(depalShader, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset);
		shaderApply.Use(render_, drawEngine_, shadeInputLayout_);

		draw_->BindFramebufferAsTexture(framebuffer->fbo, 0, depth ? Draw::FB_DEPTH_BIT : Draw::FB_COLOR_BIT, 0);

		render_->BindTexture(TEX_SLOT_CLUT, clutTexture);
		render_->SetTextureSampler(TEX_SLOT_CLUT, GL_REPEAT, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, 0.0f);

		shaderApply.Shade(render_);

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		CheckAlphaResult alphaStatus = CheckAlpha((const uint8_t *)clutBuf_, getClutDestFormat(clutFormat), clutTotalColors);
		gstate_c.SetTextureFullAlpha(alphaStatus == CHECKALPHA_FULL);
	} else {
		framebufferManagerGL_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.SetUseShaderDepal(false);
		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}

	framebufferManagerGL_->RebindFramebuffer("ApplyTextureFramebuffer");

	SamplerCacheKey samplerKey = GetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
	ApplySamplingParams(samplerKey);

	// Since we started/ended render passes, might need these.
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
}

ReplacedTextureFormat FromDataFormat(Draw::DataFormat fmt) {
	// TODO: 16-bit formats are incorrect, since swizzled.
	switch (fmt) {
	case Draw::DataFormat::R5G6B5_UNORM_PACK16: return ReplacedTextureFormat::F_0565_ABGR;
	case Draw::DataFormat::R5G5B5A1_UNORM_PACK16: return ReplacedTextureFormat::F_1555_ABGR;
	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16: return ReplacedTextureFormat::F_4444_ABGR;
	case Draw::DataFormat::R8G8B8A8_UNORM: default: return ReplacedTextureFormat::F_8888;
	}
}

Draw::DataFormat ToDataFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case ReplacedTextureFormat::F_5551: return Draw::DataFormat::R5G5B5A1_UNORM_PACK16;
	case ReplacedTextureFormat::F_4444: return Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
	case ReplacedTextureFormat::F_8888: default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

void TextureCacheGLES::BuildTexture(TexCacheEntry *const entry) {
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
	bool canAutoGen = false;
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

		if (i > 0) {
			int lastW = gstate.getTextureWidth(i - 1);
			int lastH = gstate.getTextureHeight(i - 1);

			if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
				if (tw != 1 && tw != (lastW >> 1))
					badMipSizes = true;
				else if (th != 1 && th != (lastH >> 1))
					badMipSizes = true;
			}

			if (lastW > tw || lastH > th)
				canAutoGen = true;
		}
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = FindReplacement(entry, w, h);
	if (replaced.Valid()) {
		// We're replacing, so we won't scale.
		scaleFactor = 1;
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

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	// Always load base level texture here 
	u8 level = 0;
	if (IsFakeMipmapChange()) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		level = std::max(0, gstate.getTexLevelOffset16() / 16);
	}
	LoadTextureLevel(*entry, replaced, level, scaleFactor, dstFmt);

	// Mipmapping is only enabled when texture scaling is disabled.
	int texMaxLevel = 0;
	bool genMips = false;
	if (maxLevel > 0 && scaleFactor == 1) {
		if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (badMipSizes) {
				// WARN_LOG(G3D, "Bad mipmap for texture sized %dx%dx%d - autogenerating", w, h, (int)format);
				if (canAutoGen) {
					genMips = true;
				} else {
					texMaxLevel = 0;
					maxLevel = 0;
				}
			} else {
				for (int i = 1; i <= maxLevel; i++) {
					LoadTextureLevel(*entry, replaced, i, scaleFactor, dstFmt);
				}
				texMaxLevel = maxLevel;
			}
		} else {
			// Avoid PowerVR driver bug
			if (canAutoGen && w > 1 && h > 1 && !(h > w && draw_->GetBugs().Has(Draw::Bugs::PVR_GENMIPMAP_HEIGHT_GREATER))) {  // Really! only seems to fail if height > width
				// NOTICE_LOG(G3D, "Generating mipmap for texture sized %dx%d%d", w, h, (int)format);
				genMips = true;
			} else {
				maxLevel = 0;
			}
		}
	} else if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		texMaxLevel = 0;
	}

	if (maxLevel == 0) {
		entry->status |= TexCacheEntry::STATUS_BAD_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_BAD_MIPS;
	}
	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(replaced.AlphaStatus()));
	}

	render_->FinalizeTexture(entry->textureName, texMaxLevel, genMips);
}

Draw::DataFormat TextureCacheGLES::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormat(clutFormat);
	case GE_TFMT_4444:
		return Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
	case GE_TFMT_5551:
		return Draw::DataFormat::R5G5B5A1_UNORM_PACK16;
	case GE_TFMT_5650:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

CheckAlphaResult TextureCacheGLES::CheckAlpha(const uint8_t *pixelData, Draw::DataFormat dstFmt, int w) {
	switch (dstFmt) {
	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16:
		return CheckAlpha16((const u16 *)pixelData, w, 0x000F);
	case Draw::DataFormat::R5G5B5A1_UNORM_PACK16:
		return CheckAlpha16((const u16 *)pixelData, w, 0x0001);
	case Draw::DataFormat::R5G6B5_UNORM_PACK16:
		// Never has any alpha.
		return CHECKALPHA_FULL;
	default:
		return CheckAlpha32((const u32 *)pixelData, w, 0xFF000000);  // note, the normal order here, unlike the 16-bit formats
	}
}

void TextureCacheGLES::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int scaleFactor, Draw::DataFormat dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	uint8_t *pixelData;
	int decPitch = 0;

	gpuStats.numTexturesDecoded++;

	if (!entry.textureName) {
		// TODO: Actually pass in correct size here. The size here is not yet used for anything else
		// than determining if we can wrap this texture size, that is, it's pow2 or not on very old hardware, else true.
		// This will be easy after .. well, yet another refactoring, where I hoist the size calculation out of LoadTextureLevel
		// and unify BuildTexture.
		entry.textureName = render_->CreateTexture(GL_TEXTURE_2D, 16, 16, 1);
	}

	if (replaced.GetSize(level, w, h)) {
		PROFILE_THIS_SCOPE("replacetex");

		int bpp = replaced.Format(level) == ReplacedTextureFormat::F_8888 ? 4 : 2;
		decPitch = w * bpp;
		uint8_t *rearrange = (uint8_t *)AllocateAlignedMemory(decPitch * h, 16);
		double replaceStart = time_now_d();
		replaced.Load(level, rearrange, decPitch);
		replacementTimeThisFrame_ += time_now_d() - replaceStart;
		pixelData = rearrange;

		dstFmt = ToDataFormat(replaced.Format(level));
	} else {
		PROFILE_THIS_SCOPE("decodetex");

		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, GETextureFormat(entry.format));

		int pixelSize = dstFmt == Draw::DataFormat::R8G8B8A8_UNORM ? 4 : 2;
		// We leave GL_UNPACK_ALIGNMENT at 4, so this must be at least 4.
		decPitch = std::max(w * pixelSize, 4);

		pixelData = (uint8_t *)AllocateAlignedMemory(decPitch * h * pixelSize, 16);

		CheckAlphaResult alphaStatus = DecodeTextureLevel(pixelData, decPitch, GETextureFormat(entry.format), clutformat, texaddr, level, bufw, true, false, false);
		entry.SetAlphaStatus(alphaStatus, level);

		if (scaleFactor > 1) {
			uint8_t *rearrange = (uint8_t *)AllocateAlignedMemory(w * scaleFactor * h * scaleFactor * 4, 16);
			u32 dFmt = (u32)dstFmt;
			scaler.ScaleAlways((u32 *)rearrange, (u32 *)pixelData, dFmt, w, h, scaleFactor);
			dstFmt = (Draw::DataFormat)dFmt;
			FreeAlignedMemory(pixelData);
			pixelData = rearrange;
			decPitch = w * 4;
		}

		if (replacer_.Enabled()) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = IsVideo(entry.addr);
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.scaleFactor = scaleFactor;
			replacedInfo.fmt = FromDataFormat(dstFmt);

			replacer_.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}
	
	PROFILE_THIS_SCOPE("loadtex");
	if (IsFakeMipmapChange())
		render_->TextureImage(entry.textureName, 0, w, h, dstFmt, pixelData, GLRAllocType::ALIGNED);
	else
		render_->TextureImage(entry.textureName, level, w, h, dstFmt, pixelData, GLRAllocType::ALIGNED);
}

bool TextureCacheGLES::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	GPUgstate saved;
	if (level != 0) {
		saved = gstate;

		// The way we set textures is a bit complex.  Let's just override level 0.
		gstate.texsize[0] = gstate.texsize[level];
		gstate.texaddr[0] = gstate.texaddr[level];
		gstate.texbufwidth[0] = gstate.texbufwidth[level];
	}

	InvalidateLastTexture();
	SetTexture();

	if (!nextTexture_) {
		if (nextFramebufferTexture_) {
			VirtualFramebuffer *vfb = nextFramebufferTexture_;
			buffer.Allocate(vfb->bufferWidth, vfb->bufferHeight, GPU_DBG_FORMAT_8888, false);
			bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->bufferWidth, vfb->bufferHeight, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), vfb->bufferWidth, "GetCurrentTextureDebug");
			// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
			// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
			// We may have blitted to a temp FBO.
			framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
			if (!retval)
				ERROR_LOG(G3D, "Failed to get debug texture: copy to memory failed");
			return retval;
		} else {
			ERROR_LOG(G3D, "Failed to get debug texture: no texture set");
			return false;
		}
	}

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	// We might need a render pass to set the sampling params, unfortunately.  Otherwise BuildTexture may crash.
	framebufferManagerGL_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
	ApplyTexture();

	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	// Not a framebuffer, so let's assume these are right.
	// TODO: But they may definitely not be, if the texture was scaled.
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	if (level != 0) {
		gstate = saved;
	}

	bool result = entry->textureName != nullptr;
	if (result) {
		buffer.Allocate(w, h, GE_FORMAT_8888, false);
		renderManager->CopyImageToMemorySync(entry->textureName, level, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, (uint8_t *)buffer.GetData(), w, "GetCurrentTextureDebug");
	} else {
		ERROR_LOG(G3D, "Failed to get debug texture: texture is null");
	}
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");

	return result;
}

void TextureCacheGLES::DeviceLost() {
	if (shadeInputLayout_) {
		render_->DeleteInputLayout(shadeInputLayout_);
		shadeInputLayout_ = nullptr;
	}
	Clear(false);
	draw_ = nullptr;
	render_ = nullptr;
}

void TextureCacheGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	if (!shadeInputLayout_) {
		std::vector<GLRInputLayout::Entry> entries;
		entries.push_back({ 0, 3, GL_FLOAT, GL_FALSE, 20, 0 });
		entries.push_back({ 1, 2, GL_FLOAT, GL_FALSE, 20, 12 });
		shadeInputLayout_ = render_->CreateInputLayout(entries);
	}
}
