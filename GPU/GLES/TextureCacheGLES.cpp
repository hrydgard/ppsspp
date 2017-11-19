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
#include "gfx/gl_debug_log.h"
#include "i18n/i18n.h"
#include "math/math_util.h"
#include "profiler/profiler.h"
#include "thin3d/GLRenderManager.h"

#include "Common/ColorConv.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "ext/native/gfx/GLStateCache.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"
#include "GPU/GLES/DepalettizeShaderGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/Common/TextureDecoder.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

TextureCacheGLES::TextureCacheGLES(Draw::DrawContext *draw)
	: TextureCacheCommon(draw) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = nullptr;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheGLES::~TextureCacheGLES() {
	Clear(true);
}

void TextureCacheGLES::SetFramebufferManager(FramebufferManagerGLES *fbManager) {
	framebufferManagerGL_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheGLES::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	DEBUG_LOG(G3D, "Deleting texture %i", entry->textureName);
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

GLenum getClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GE_CMODE_16BIT_ABGR5551:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GE_CMODE_16BIT_BGR5650:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GE_CMODE_32BIT_ABGR8888:
		return GL_UNSIGNED_BYTE;
	}
	return 0;
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

// This should not have to be done per texture! OpenGL is silly yo
void TextureCacheGLES::UpdateSamplingParams(TexCacheEntry &entry, bool force) {
	CHECK_GL_ERROR_IF_DEBUG();
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	u8 maxLevel = (entry.status & TexCacheEntry::STATUS_BAD_MIPS) ? 0 : entry.maxLevel;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, maxLevel, entry.addr, mode);

	if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		if (maxLevel != 0) {
			// TODO: What about a swap of autoMip mode?
			if (force || entry.lodBias != lodBias) {
				if (mode == GE_TEXLEVEL_MODE_AUTO) {
#ifndef USING_GLES2
					// Sigh, LOD_BIAS is not even in ES 3.0.. but we could do it in the shader via texture()...
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
#endif
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)maxLevel);
				} else if (mode == GE_TEXLEVEL_MODE_CONST) {
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, std::max(0.0f, std::min((float)maxLevel, lodBias)));
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, std::max(0.0f, std::min((float)maxLevel, lodBias)));
				} else {  // mode == GE_TEXLEVEL_MODE_SLOPE) {
					// It's incorrect to use the slope as a bias. Instead it should be passed
					// into the shader directly as an explicit lod level, with the bias on top. For now, we just kill the
					// lodBias in this mode, working around #9772.
#ifndef USING_GLES2
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.0f);
#endif
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)maxLevel);
				}
				entry.lodBias = lodBias;
			}
		} else {
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0.0f);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0.0f);
		}
	}

	if (force || entry.minFilt != minFilt || entry.magFilt != magFilt || entry.sClamp != sClamp || entry.tClamp != tClamp) {
		float aniso = 0.0f;
		render_->SetTextureSampler(sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, MagFiltGL[magFilt], MinFiltGL[minFilt], aniso);
		entry.minFilt = minFilt;
		entry.magFilt = magFilt;
		entry.sClamp = sClamp;
		entry.tClamp = tClamp;
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void TextureCacheGLES::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0, mode);

	minFilt &= 1;  // framebuffers can't mipmap.

	float aniso = 0.0f;
	render_->SetTextureSampler(sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, MagFiltGL[magFilt], MinFiltGL[minFilt], aniso);

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		return;
	}
}

static void ConvertColors(void *dstBuf, const void *srcBuf, GLuint dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		ConvertRGBA4444ToABGR4444((u16 *)dst, (const u16 *)src, numPixels);
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case GL_UNSIGNED_SHORT_5_5_5_1:
		ConvertRGBA5551ToABGR1555((u16 *)dst, (const u16 *)src, numPixels);
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
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

	clutHash_ = DoReliableHash32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);

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

// #define DEBUG_TEXTURES

#ifdef DEBUG_TEXTURES
bool SetDebugTexture() {
	static const int highlightFrames = 30;

	static int numTextures = 0;
	static int lastFrames = 0;
	static int mostTextures = 1;

	if (lastFrames != gpuStats.numFlips) {
		mostTextures = std::max(mostTextures, numTextures);
		numTextures = 0;
		lastFrames = gpuStats.numFlips;
	}

	static GLuint solidTexture = 0;

	bool changed = false;
	if (((gpuStats.numFlips / highlightFrames) % mostTextures) == numTextures) {
		if (gpuStats.numFlips % highlightFrames == 0) {
			NOTICE_LOG(G3D, "Highlighting texture # %d / %d", numTextures, mostTextures);
		}
		static const u32 solidTextureData[] = {0x99AA99FF};

		if (solidTexture == 0) {
			glGenTextures(1, &solidTexture);
			glBindTexture(GL_TEXTURE_2D, solidTexture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, solidTextureData);
		} else {
			glBindTexture(GL_TEXTURE_2D, solidTexture);
		}
		changed = true;
	}

	++numTextures;
	return changed;
}
#endif

void TextureCacheGLES::BindTexture(TexCacheEntry *entry) {
	if (entry->textureName != lastBoundTexture) {
		render_->BindTexture(0, entry->textureName);
		lastBoundTexture = entry->textureName;
	}
	UpdateSamplingParams(*entry, false);
}

void TextureCacheGLES::Unbind() {
	render_->BindTexture(0, nullptr);
	InvalidateLastTexture();
}

class TextureShaderApplier {
public:
	struct Pos {
		Pos(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
		}
		Pos() {
		}

		float x;
		float y;
		float z;
	};
	struct UV {
		UV(float u_, float v_) : u(u_), v(v_) {
		}
		UV() {
		}

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
			pos_[0] = Pos(left, bottom, -1.0f);
			pos_[1] = Pos(right, bottom, -1.0f);
			pos_[2] = Pos(right, top, -1.0f);
			pos_[3] = Pos(left, top, -1.0f);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			uv_[0] = UV(uvleft, uvbottom);
			uv_[1] = UV(uvright, uvbottom);
			uv_[2] = UV(uvright, uvtop);
			uv_[3] = UV(uvleft, uvtop);
		}
	}

	void Use(GLRenderManager *render, DrawEngineGLES *transformDraw) {
		render->BindProgram(shader_->program);

		// Restore will rebind all of the state below.
		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			static const GLubyte indices[4] = { 0, 1, 3, 2 };
			transformDraw->BindBuffer(pos_, sizeof(pos_), uv_, sizeof(uv_));
			transformDraw->BindElementBuffer(indices, sizeof(indices));
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		glEnableVertexAttribArray(shader_->a_position);
		glEnableVertexAttribArray(shader_->a_texcoord0);
	}

	void Shade() {
		static const GLubyte indices[4] = { 0, 1, 3, 2 };

		glstate.blend.force(false);
		glstate.colorMask.force(true, true, true, true);
		glstate.scissorTest.force(false);
		glstate.cullFace.force(false);
		glstate.depthTest.force(false);
		glstate.stencilTest.force(false);
#if !defined(USING_GLES2)
		glstate.colorLogicOp.force(false);
#endif
		glViewport(0, 0, renderW_, renderH_);

		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			glVertexAttribPointer(shader_->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
			glVertexAttribPointer(shader_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (void *)sizeof(pos_));
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
		} else {
			glVertexAttribPointer(shader_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos_);
			glVertexAttribPointer(shader_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, uv_);
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
		}
		glDisableVertexAttribArray(shader_->a_position);
		glDisableVertexAttribArray(shader_->a_texcoord0);

		glstate.Restore();
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

void TextureCacheGLES::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	DepalShader *depal = nullptr;
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		depal = depalShaderCache_->GetDepalettizeShader(clutMode, framebuffer->drawnFormat);
	}
	if (depal) {
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		GLRTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
		Draw::Framebuffer *depalFBO = framebufferManagerGL_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		shaderManager_->DirtyLastShader();

		TextureShaderApplier shaderApply(depal, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset);
		shaderApply.Use(render_, drawEngine_);

		render_->BindTexture(3, clutTexture);

		framebufferManagerGL_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		shaderApply.Shade();

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, getClutDestFormat(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManagerGL_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}

	framebufferManagerGL_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);

	CHECK_GL_ERROR_IF_DEBUG();

	InvalidateLastTexture();
}

ReplacedTextureFormat FromGLESFormat(GLenum fmt) {
	// TODO: 16-bit formats are incorrect, since swizzled.
	switch (fmt) {
	case GL_UNSIGNED_SHORT_5_6_5: return ReplacedTextureFormat::F_0565_ABGR;
	case GL_UNSIGNED_SHORT_5_5_5_1: return ReplacedTextureFormat::F_1555_ABGR;
	case GL_UNSIGNED_SHORT_4_4_4_4: return ReplacedTextureFormat::F_4444_ABGR;
	case GL_UNSIGNED_BYTE: default: return ReplacedTextureFormat::F_8888;
	}
}

GLenum ToGLESFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return GL_UNSIGNED_SHORT_5_6_5;
	case ReplacedTextureFormat::F_5551: return GL_UNSIGNED_SHORT_5_5_5_1;
	case ReplacedTextureFormat::F_4444: return GL_UNSIGNED_SHORT_4_4_4_4;
	case ReplacedTextureFormat::F_8888: default: return GL_UNSIGNED_BYTE;
	}
}

void TextureCacheGLES::BuildTexture(TexCacheEntry *const entry, bool replaceImages) {
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	if (entry->framebuffer) {
		// Nothing else to do here.
		return;
	}

	// Always generate a texture name unless it's a framebuffer, we might need it if the texture is replaced later.
	if (!replaceImages) {
		if (!entry->textureName) {
			entry->textureName = render_->CreateTexture(GL_TEXTURE_2D);
		}
	}

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
	GLenum dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

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
		if (replaceImages) {
			// Since we're replacing the texture, we can't replace the image inside.
			render_->DeleteTexture(entry->textureName);
			entry->textureName = render_->CreateTexture(GL_TEXTURE_2D);
			replaceImages = false;
		}

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

	// glBindTexture(GL_TEXTURE_2D, entry->textureName);
	lastBoundTexture = entry->textureName;

	// Disabled this due to issue #6075: https://github.com/hrydgard/ppsspp/issues/6075
	// This breaks Dangan Ronpa 2 with mipmapping enabled. Why? No idea, it shouldn't.
	// glTexStorage2D probably has few benefits for us anyway.
	if (false && gl_extensions.GLES3 && maxLevel > 0) {
		// glTexStorage2D requires the use of sized formats.
		GLenum actualFmt = replaced.Valid() ? ToGLESFormat(replaced.Format(0)) : dstFmt;
		GLenum storageFmt = GL_RGBA8;
		switch (actualFmt) {
		case GL_UNSIGNED_BYTE: storageFmt = GL_RGBA8; break;
		case GL_UNSIGNED_SHORT_5_6_5: storageFmt = GL_RGB565; break;
		case GL_UNSIGNED_SHORT_4_4_4_4: storageFmt = GL_RGBA4; break;
		case GL_UNSIGNED_SHORT_5_5_5_1: storageFmt = GL_RGB5_A1; break;
		default:
			ERROR_LOG(G3D, "Unknown dstfmt %i", (int)actualFmt);
			break;
		}
		// TODO: This may cause bugs, since it hard-sets the texture w/h, and we might try to reuse it later with a different size.
		glTexStorage2D(GL_TEXTURE_2D, maxLevel + 1, storageFmt, w * scaleFactor, h * scaleFactor);
		// Make sure we don't use glTexImage2D after glTexStorage2D.
		replaceImages = true;
	}

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	// Always load base level texture here 
	if (IsFakeMipmapChange()) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		u8 level = std::max(0, gstate.getTexLevelOffset16() / 16);
		LoadTextureLevel(*entry, replaced, level, replaceImages, scaleFactor, dstFmt);
	} else
		LoadTextureLevel(*entry, replaced, 0, replaceImages, scaleFactor, dstFmt);

	// Mipmapping only enable when texture scaling disable
	if (maxLevel > 0 && scaleFactor == 1) {
		if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (badMipSizes) {
				// WARN_LOG(G3D, "Bad mipmap for texture sized %dx%dx%d - autogenerating", w, h, (int)format);
				if (canAutoGen) {
					glGenerateMipmap(GL_TEXTURE_2D);
				} else {
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
					maxLevel = 0;
				}
			} else {
				for (int i = 1; i <= maxLevel; i++) {
					LoadTextureLevel(*entry, replaced, i, replaceImages, scaleFactor, dstFmt);
				}
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
			}
		} else {
			// Avoid PowerVR driver bug
			if (canAutoGen && w > 1 && h > 1 && !(h > w && (gl_extensions.bugs & BUG_PVR_GENMIPMAP_HEIGHT_GREATER))) {  // Really! only seems to fail if height > width
				// NOTICE_LOG(G3D, "Generating mipmap for texture sized %dx%d%d", w, h, (int)format);
				glGenerateMipmap(GL_TEXTURE_2D);
			} else {
				maxLevel = 0;
			}
		}
	} else if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}

	if (maxLevel == 0) {
		entry->status |= TexCacheEntry::STATUS_BAD_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_BAD_MIPS;
	}
	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(replaced.AlphaStatus()));
	}

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY)) {
		int aniso = 1 << g_Config.iAnisotropyLevel;
		float anisotropyLevel = aniso; //(float) aniso > maxAnisotropyLevel ? maxAnisotropyLevel : (float) aniso;
		if (anisotropyLevel > 1.0f) {
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropyLevel);
		}
	}

	// This will rebind it, but that's okay.
	UpdateSamplingParams(*entry, true);

	//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	//glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	CHECK_GL_ERROR_IF_DEBUG();
}

GLenum TextureCacheGLES::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormat(clutFormat);
	case GE_TFMT_4444:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GE_TFMT_5551:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GE_TFMT_5650:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return GL_UNSIGNED_BYTE;
	}
}

void *TextureCacheGLES::DecodeTextureLevelOld(GETextureFormat format, GEPaletteFormat clutformat, int level, GLenum dstFmt, int scaleFactor, int *bufwout) {
	void *finalBuf = nullptr;
	u32 texaddr = gstate.getTextureAddress(level);
	int bufw = GetTextureBufw(level, texaddr, format);
	if (bufwout)
		*bufwout = bufw;

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	int decPitch = 0;
	int pixelSize = dstFmt == GL_UNSIGNED_BYTE ? 4 : 2;
	if (!(scaleFactor == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE)) && w != bufw) {
		decPitch = w * pixelSize;
	} else {
		decPitch = bufw * pixelSize;
	}

	tmpTexBufRearrange_.resize(std::max(w, bufw) * h);
	DecodeTextureLevel((u8 *)tmpTexBufRearrange_.data(), decPitch, format, clutformat, texaddr, level, bufw, true, false, false);
	return tmpTexBufRearrange_.data();
}

TexCacheEntry::TexStatus TextureCacheGLES::CheckAlpha(const u32 *pixelData, GLenum dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		res = CheckAlphaABGR4444Basic(pixelData, stride, w, h);
		break;
	case GL_UNSIGNED_SHORT_5_5_5_1:
		res = CheckAlphaABGR1555Basic(pixelData, stride, w, h);
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::TexStatus)res;
}

void TextureCacheGLES::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, bool replaceImages, int scaleFactor, GLenum dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	bool useUnpack = false;
	u32 *pixelData;

	CHECK_GL_ERROR_IF_DEBUG();

	// TODO: only do this once
	u32 texByteAlign = 1;

	gpuStats.numTexturesDecoded++;

	if (replaced.GetSize(level, w, h)) {
		PROFILE_THIS_SCOPE("replacetex");

		tmpTexBufRearrange_.resize(w * h);
		int bpp = replaced.Format(level) == ReplacedTextureFormat::F_8888 ? 4 : 2;
		replaced.Load(level, tmpTexBufRearrange_.data(), bpp * w);
		pixelData = tmpTexBufRearrange_.data();

		dstFmt = ToGLESFormat(replaced.Format(level));

		texByteAlign = bpp;
	} else {
		PROFILE_THIS_SCOPE("decodetex");

		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		int bufw;
		void *finalBuf = DecodeTextureLevelOld(GETextureFormat(entry.format), clutformat, level, dstFmt, scaleFactor, &bufw);
		if (finalBuf == NULL) {
			return;
		}

		// Can restore these and remove the fixup at the end of DecodeTextureLevel on desktop GL and GLES 3.
		if (scaleFactor == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE) && w != bufw) {
			glPixelStorei(GL_UNPACK_ROW_LENGTH, bufw);
			useUnpack = true;
		}

		// Textures are always aligned to 16 bytes bufw, so this could safely be 4 always.
		texByteAlign = dstFmt == GL_UNSIGNED_BYTE ? 4 : 2;
		pixelData = (u32 *)finalBuf;

		// We check before scaling since scaling shouldn't invent alpha from a full alpha texture.
		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha(pixelData, dstFmt, useUnpack ? bufw : w, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (scaleFactor > 1)
			scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);

		if (replacer_.Enabled()) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = videos_.find(entry.addr & 0x3FFFFFFF) != videos_.end();
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.scaleFactor = scaleFactor;
			replacedInfo.fmt = FromGLESFormat(dstFmt);

			int bpp = dstFmt == GL_UNSIGNED_BYTE ? 4 : 2;
			replacer_.NotifyTextureDecoded(replacedInfo, pixelData, (useUnpack ? bufw : w) * bpp, level, w, h);
		}
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, texByteAlign);

	CHECK_GL_ERROR_IF_DEBUG();

	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;

	GLuint components2 = components;

	if (replaceImages) {
		PROFILE_THIS_SCOPE("repltex");
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, components2, dstFmt, pixelData);
	} else {
		PROFILE_THIS_SCOPE("loadtex");
		// Avoid misleading errors in texture upload, these are common.
		GLenum err = glGetError();
		if (err) {
			WARN_LOG(G3D, "Got an error BEFORE texture upload: %08x (%s)", err, GLEnumToString(err).c_str());
		}
		if (IsFakeMipmapChange())
			glTexImage2D(GL_TEXTURE_2D, 0, components, w, h, 0, components2, dstFmt, pixelData);
		else
			glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);
		if (!lowMemoryMode_) {
			// TODO: We really, really should avoid calling glGetError.
			GLenum err = glGetError();
			if (err == GL_OUT_OF_MEMORY) {
				WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
				lowMemoryMode_ = true;
				decimationCounter_ = 0;
				Decimate();
				// Try again, now that we've cleared out textures in lowMemoryMode_.
				glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);

				I18NCategory *err = GetI18NCategory("Error");
				if (scaleFactor > 1) {
					host->NotifyUserMessage(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
				} else {
					host->NotifyUserMessage(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
				}
			} else if (err != GL_NO_ERROR) {
				// We checked the err anyway, might as well log if there is one.
				WARN_LOG(G3D, "Got an error in texture upload: %08x (%s) (components=%s components2=%s dstFmt=%s w=%d h=%d level=%d)",
					err, GLEnumToString(err).c_str(), GLEnumToString(components).c_str(), GLEnumToString(components2).c_str(), GLEnumToString(dstFmt).c_str(),
					w, h, level);
			}
		}
	}

	if (useUnpack) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

// Only used by Qt UI?
bool TextureCacheGLES::DecodeTexture(u8* output, const GPUgstate &state) {
	GPUgstate oldState = gstate;
	gstate = state;

	u32 texaddr = gstate.getTextureAddress(0);

	if (!Memory::IsValidAddress(texaddr)) {
		return false;
	}

	GLenum dstFmt = 0;

	GETextureFormat format = gstate.getTextureFormat();
	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	u8 level = 0;

	int bufw = GetTextureBufw(level, texaddr, format);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	void *finalBuf = DecodeTextureLevelOld(format, clutformat, level, dstFmt, 1);
	if (finalBuf == NULL) {
		return false;
	}

	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 r = ((val>>12) & 0xF) * 17;
				u32 g = ((val>> 8) & 0xF) * 17;
				u32 b = ((val>> 4) & 0xF) * 17;
				u32 a = ((val>> 0) & 0xF) * 17;
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	case GL_UNSIGNED_SHORT_5_5_5_1:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert5To8((val>> 6) & 0x1F);
				u32 b = Convert5To8((val>> 1) & 0x1F);
				u32 a = (val & 0x1) * 255;
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	case GL_UNSIGNED_SHORT_5_6_5:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 a = 0xFF;
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert6To8((val>> 5) & 0x3F);
				u32 b = Convert5To8((val    ) & 0x1F);
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	default:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u32*)finalBuf)[y*bufw + x];
				((u32*)output)[y*w + x] = ((val & 0xFF000000)) | ((val & 0x00FF0000)>>16) | ((val & 0x0000FF00)) | ((val & 0x000000FF)<<16);
			}
		break;
	}

	gstate = oldState;
	return true;
}

bool TextureCacheGLES::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
#ifndef USING_GLES2
	GPUgstate saved;
	if (level != 0) {
		saved = gstate;

		// The way we set textures is a bit complex.  Let's just override level 0.
		gstate.texsize[0] = gstate.texsize[level];
		gstate.texaddr[0] = gstate.texaddr[level];
		gstate.texbufwidth[0] = gstate.texbufwidth[level];
	}

	SetTexture(true);
	ApplyTexture();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

	if (level != 0) {
		gstate = saved;
	}

	buffer.Allocate(w, h, GE_FORMAT_8888, false);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
#else
	return false;
#endif
}

void TextureCacheGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}
