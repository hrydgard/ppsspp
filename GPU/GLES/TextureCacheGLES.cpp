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
#include "Common/System/OSD.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/Common/TextureDecoder.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

TextureCacheGLES::TextureCacheGLES(Draw::DrawContext *draw, Draw2D *draw2D)
	: TextureCacheCommon(draw, draw2D) {
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

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
	return Draw::DataFormat::UNDEFINED;
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
	if (gstate_c.Use(GPU_USE_TEXTURE_LOD_CONTROL)) {
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
	TextureCacheCommon::StartFrame();

	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	if (!lowMemoryMode_ && renderManager->SawOutOfMemory()) {
		lowMemoryMode_ = true;
		decimationCounter_ = 0;

		auto err = GetI18NCategory(I18NCat::ERRORS);
		if (standardScaleFactor_ > 1) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
		} else {
			g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
		}
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
	if (!entry) {
		render_->BindTexture(0, nullptr);
		lastBoundTexture = nullptr;
		return;
	}
	if (entry->textureName != lastBoundTexture) {
		render_->BindTexture(0, entry->textureName);
		lastBoundTexture = entry->textureName;
	}
	int maxLevel = (entry->status & TexCacheEntry::STATUS_NO_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry);
	ApplySamplingParams(samplerKey);
	gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
}

void TextureCacheGLES::Unbind() {
	render_->BindTexture(TEX_SLOT_PSP_TEXTURE, nullptr);
	ForgetLastTexture();
}

void TextureCacheGLES::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	GLRTexture *glrTex = (GLRTexture *)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	render_->BindTexture(TEX_SLOT_CLUT, glrTex);
	render_->SetTextureSampler(TEX_SLOT_CLUT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, smooth ? GL_LINEAR : GL_NEAREST, smooth ? GL_LINEAR : GL_NEAREST, 0.0f);
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

	int tw = plan.createW;
	int th = plan.createH;

	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	if (plan.doReplace) {
		plan.replaced->GetSize(plan.baseLevelSrc, &tw, &th);
		dstFmt = plan.replaced->Format();
	} else if (plan.scaleFactor > 1 || plan.saveTexture) {
		dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (plan.decodeToClut8) {
		dstFmt = Draw::DataFormat::R8_UNORM;
	}

	if (plan.depth == 1) {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_2D, tw, th, 1, plan.levelsToCreate);
	} else {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_3D, tw, th, plan.depth, 1);
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

	if (!gstate_c.Use(GPU_USE_TEXTURE_LOD_CONTROL)) {
		// If the mip chain is not full..
		if (plan.levelsToCreate != plan.maxPossibleLevels) {
			// We need to avoid creating mips at all, or generate them all - can't be incomplete
			// on this hardware (strict OpenGL rules).
			plan.levelsToCreate = 1;
			plan.levelsToLoad = 1;
			entry->status |= TexCacheEntry::STATUS_NO_MIPS;
		}
	}

	if (plan.depth == 1) {
		for (int i = 0; i < plan.levelsToLoad; i++) {
			int srcLevel = i == 0 ? plan.baseLevelSrc : i;

			int mipWidth;
			int mipHeight;
			plan.GetMipSize(i, &mipWidth, &mipHeight);

			u8 *data = nullptr;
			int stride = 0;
			int dataSize;

			bool bc = false;

			if (plan.doReplace) {
				int blockSize = 0;
				if (Draw::DataFormatIsBlockCompressed(plan.replaced->Format(), &blockSize)) {
					stride = mipWidth * 4;
					dataSize = plan.replaced->GetLevelDataSizeAfterCopy(i);
					bc = true;
				} else {
					int bpp = (int)Draw::DataFormatSizeInBytes(plan.replaced->Format());
					stride = mipWidth * bpp;
					dataSize = stride * mipHeight;
				}
			} else {
				int bpp = 0;
				if (plan.scaleFactor > 1) {
					bpp = 4;
				} else {
					bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);
				}
				stride = mipWidth * bpp;
				dataSize = stride * mipHeight;
			}

			data = (u8 *)AllocateAlignedMemory(dataSize, 16);

			if (!data) {
				ERROR_LOG(Log::G3D, "Ran out of RAM trying to allocate a temporary texture upload buffer (%dx%d)", mipWidth, mipHeight);
				return;
			}

			LoadTextureLevel(*entry, data, dataSize, stride, plan, srcLevel, dstFmt, TexDecodeFlags::REVERSE_COLORS);

			// NOTE: TextureImage takes ownership of data, so we don't free it afterwards.
			render_->TextureImage(entry->textureName, i, mipWidth, mipHeight, 1, dstFmt, data, GLRAllocType::ALIGNED);
		}

		bool genMips = plan.levelsToCreate > plan.levelsToLoad;

		render_->FinalizeTexture(entry->textureName, plan.levelsToLoad, genMips);
	} else {
		int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);
		int stride = bpp * (plan.w * plan.scaleFactor);
		int levelStride = stride * (plan.h * plan.scaleFactor);

		size_t dataSize = levelStride * plan.depth;
		u8 *data = (u8 *)AllocateAlignedMemory(dataSize, 16);
		memset(data, 0, levelStride * plan.depth);
		u8 *p = data;

		for (int i = 0; i < plan.depth; i++) {
			LoadTextureLevel(*entry, p, dataSize, stride, plan, i, dstFmt, TexDecodeFlags::REVERSE_COLORS);
			p += levelStride;
		}

		render_->TextureImage(entry->textureName, 0, plan.w * plan.scaleFactor, plan.h * plan.scaleFactor, plan.depth, dstFmt, data, GLRAllocType::ALIGNED);

		// Signal that we support depth textures so use it as one.
		entry->status |= TexCacheEntry::STATUS_3D;

		render_->FinalizeTexture(entry->textureName, 1, false);
	}

	if (plan.doReplace) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(plan.replaced->AlphaStatus()));
	}
}

Draw::DataFormat TextureCacheGLES::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) {
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

bool TextureCacheGLES::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	ForgetLastTexture();
	SetTexture();
	if (!nextTexture_) {
		return GetCurrentFramebufferTextureDebug(buffer, isFramebuffer);
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

	bool result = entry->textureName != nullptr;
	if (result) {
		buffer.Allocate(w, h, GE_FORMAT_8888, false);
		renderManager->CopyImageToMemorySync(entry->textureName, level, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, (uint8_t *)buffer.GetData(), w, "GetCurrentTextureDebug");
	} else {
		ERROR_LOG(Log::G3D, "Failed to get debug texture: texture is null");
	}
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");

	*isFramebuffer = false;
	return result;
}

void TextureCacheGLES::DeviceLost() {
	textureShaderCache_->DeviceLost();
	Clear(false);
	draw_ = nullptr;
	render_ = nullptr;
}

void TextureCacheGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	textureShaderCache_->DeviceRestore(draw);
}

void *TextureCacheGLES::GetNativeTextureView(const TexCacheEntry *entry) {
	GLRTexture *tex = entry->textureName;
	return (void *)tex;
}
