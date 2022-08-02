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
	int maxLevel = (entry->status & TexCacheEntry::STATUS_NO_MIPS) ? 0 : entry->maxLevel;
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

void TextureCacheGLES::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		// We're screwed?
		return;
	}

	_assert_(!entry->textureName);

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	int tw = plan.w;
	int th = plan.h;

	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	if (plan.replaced->GetSize(plan.baseLevelSrc, tw, th)) {
		dstFmt = plan.replaced->Format(plan.baseLevelSrc);
	} else if (plan.scaleFactor > 1) {
		tw *= plan.scaleFactor;
		th *= plan.scaleFactor;
		dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
	}

	if (plan.depth == 1) {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_2D, tw, tw, 1, plan.levelsToCreate);
	} else {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_3D, tw, tw, plan.depth, 1);
	}

	// Apply some additional compatibility checks.
	if (plan.levelsToLoad > 1) {
		// Avoid PowerVR driver bug
		if (plan.w > 1 && plan.h > 1 && !(plan.h > plan.w && draw_->GetBugs().Has(Draw::Bugs::PVR_GENMIPMAP_HEIGHT_GREATER))) {  // Really! only seems to fail if height > width
			// It's ok to generate mipmaps beyond the loaded levels.
		} else {
			plan.levelsToCreate = plan.levelsToLoad;
		}
	} 

	if (!gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		// Force no additional mipmaps.
		plan.levelsToCreate = plan.levelsToLoad;
	}

	if (plan.depth == 1) {
		for (int i = 0; i < plan.levelsToLoad; i++) {
			int srcLevel = i == 0 ? plan.baseLevelSrc : i;

			int w = gstate.getTextureWidth(srcLevel);
			int h = gstate.getTextureHeight(srcLevel);

			u8 *data = nullptr;
			int stride = 0;

			if (plan.replaced->GetSize(srcLevel, w, h)) {
				int bpp = (int)Draw::DataFormatSizeInBytes(plan.replaced->Format(srcLevel));
				stride = w * bpp;
				data = (u8 *)AllocateAlignedMemory(stride * h, 16);
			} else {
				if (plan.scaleFactor > 1) {
					data = (u8 *)AllocateAlignedMemory(4 * (w * plan.scaleFactor) * (h * plan.scaleFactor), 16);
					stride = w * plan.scaleFactor * 4;
				} else {
					int bpp = dstFmt == Draw::DataFormat::R8G8B8A8_UNORM ? 4 : 2;

					stride = std::max(w * bpp, 4);
					data = (u8 *)AllocateAlignedMemory(stride * h, 16);
				}
			}

			if (!data) {
				ERROR_LOG(G3D, "Ran out of RAM trying to allocate a temporary texture upload buffer (%dx%d)", w, h);
				return;
			}

			LoadTextureLevel(*entry, data, stride, *plan.replaced, srcLevel, plan.scaleFactor, dstFmt, true);

			// NOTE: TextureImage takes ownership of data, so we don't free it afterwards.
			render_->TextureImage(entry->textureName, i, w * plan.scaleFactor, h * plan.scaleFactor, 1, dstFmt, data, GLRAllocType::ALIGNED);
		}

		bool genMips = plan.levelsToCreate > plan.levelsToLoad;

		render_->FinalizeTexture(entry->textureName, plan.levelsToLoad, genMips);
	} else {
		int bpp = dstFmt == Draw::DataFormat::R8G8B8A8_UNORM ? 4 : 2;
		int stride = bpp * (plan.w * plan.scaleFactor);
		int levelStride = stride * (plan.h * plan.scaleFactor);

		u8 *data = (u8 *)AllocateAlignedMemory(levelStride * plan.depth, 16);
		memset(data, 0, levelStride * plan.depth);
		u8 *p = data;

		for (int i = 0; i < plan.depth; i++) {
			LoadTextureLevel(*entry, p, stride, *plan.replaced, i, plan.scaleFactor, dstFmt, true);
			p += levelStride;
		}

		render_->TextureImage(entry->textureName, 0, plan.w * plan.scaleFactor, plan.h * plan.scaleFactor, plan.depth, dstFmt, data, GLRAllocType::ALIGNED);

		// Signal that we support depth textures so use it as one.
		entry->status |= TexCacheEntry::STATUS_3D;

		render_->FinalizeTexture(entry->textureName, 1, false);
	}

	if (plan.replaced->Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(plan.replaced->AlphaStatus()));
	}
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
