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

#include "ppsspp_config.h"

#include <algorithm>
#include <cfloat>

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Metal/TextureCacheMetal.h"
#include "GPU/Metal/DrawEngineMetal.h"
#include "GPU/Metal/FramebufferManagerMetal.h"

static MTLPixelFormat ToMetalFormat(Draw::DataFormat fmt) {
	switch (fmt) {
	case Draw::DataFormat::BC1_RGBA_UNORM_BLOCK: return MTLPixelFormatBC1_RGBA;
	case Draw::DataFormat::BC2_UNORM_BLOCK: return MTLPixelFormatBC2_RGBA;
	case Draw::DataFormat::BC3_UNORM_BLOCK: return MTLPixelFormatBC3_RGBA;
	case Draw::DataFormat::BC4_UNORM_BLOCK: return MTLPixelFormatBC4_RUnorm;
	case Draw::DataFormat::BC5_UNORM_BLOCK: return MTLPixelFormatBC5_RGUnorm;
	case Draw::DataFormat::BC7_UNORM_BLOCK: return MTLPixelFormatBC7_RGBAUnorm;
	case Draw::DataFormat::ASTC_4x4_UNORM_BLOCK: return MTLPixelFormatASTC_4x4_LDR;
	case Draw::DataFormat::ETC2_R8G8B8_UNORM_BLOCK: return MTLPixelFormatETC2_RGB8;
	case Draw::DataFormat::ETC2_R8G8B8A1_UNORM_BLOCK: return MTLPixelFormatETC2_RGB8A1;
	case Draw::DataFormat::ETC2_R8G8B8A8_UNORM_BLOCK: return MTLPixelFormatEAC_RGBA8;
	case Draw::DataFormat::R8_UNORM: return MTLPixelFormatR8Unorm;
	// The PSP's 16-bit layouts, as they come from the decoder. See Swizzle16.
	case Draw::DataFormat::R5G6B5_UNORM_PACK16: return MTLPixelFormatB5G6R5Unorm;
	case Draw::DataFormat::A1R5G5B5_UNORM_PACK16: return MTLPixelFormatBGR5A1Unorm;
	case Draw::DataFormat::A4R4G4B4_UNORM_PACK16: return MTLPixelFormatABGR4Unorm;
	case Draw::DataFormat::R8G8B8A8_UNORM:
	default:
		return MTLPixelFormatRGBA8Unorm;
	}
}

// Metal names the channels of packed formats from the lowest bits up, and the PSP stores red lowest. So
// B5G6R5Unorm's "blue" is the PSP's red, and so on, which the swizzle puts back. The same as the Vulkan backend's
// VULKAN_565_SWIZZLE and friends.
static MTLTextureSwizzleChannels Swizzle16(MTLPixelFormat format) {
	switch (format) {
	case MTLPixelFormatB5G6R5Unorm: return MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed, MTLTextureSwizzleOne);
	case MTLPixelFormatBGR5A1Unorm: return MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed, MTLTextureSwizzleAlpha);
	case MTLPixelFormatABGR4Unorm: return MTLTextureSwizzleChannelsMake(MTLTextureSwizzleAlpha, MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed);
	default: return MTLTextureSwizzleChannelsDefault;
	}
}

static Draw::DataFormat Format16(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_BGR5650: return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_CMODE_16BIT_ABGR5551: return Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
	case GE_CMODE_16BIT_ABGR4444: return Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
	default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

// What the decoder should output for a texture. 16-bit textures stay 16-bit if the GPU has the formats.
static Draw::DataFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) {
	if (!gstate_c.Use(GPU_USE_16BIT_FORMATS)) {
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return Format16(clutFormat);
	case GE_TFMT_5650: return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_TFMT_5551: return Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
	case GE_TFMT_4444: return Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
	default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

TextureCacheMetal::TextureCacheMetal(Draw::DrawContext *draw, Draw2D *draw2D) : TextureCacheCommon(draw, draw2D) {
	rm_ = (MetalRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
}

TextureCacheMetal::~TextureCacheMetal() {
	Clear(true);
}

void TextureCacheMetal::SetFramebufferManager(FramebufferManagerMetal *fbManager) {
	framebufferManager_ = fbManager;
}

void TextureCacheMetal::DeviceLost() {
	TextureCacheCommon::DeviceLost();
	samplerCache_.clear();
	SetBoundTexture(nil);
	SetBoundSampler(nil);
	draw_ = nullptr;
	rm_ = nullptr;
}

void TextureCacheMetal::DeviceRestore(Draw::DrawContext *draw) {
	rm_ = (MetalRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	TextureCacheCommon::DeviceRestore(draw);
}

void TextureCacheMetal::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	if (entry->texturePtr) {
		// Balances the __bridge_retained in BuildTexture. The GPU may still be using it.
		id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)entry->texturePtr;
		if (rm_) {
			rm_->DeferRelease(tex);
		}
		entry->texturePtr = nullptr;
	}
}

void TextureCacheMetal::BindTexture(TexCacheEntry *entry) {
	if (!entry || !entry->texturePtr) {
		Unbind();
		return;
	}
	drawEngine_->SetDepalTexture(nil, false);
	SetBoundTexture(Tex(entry));
}

void TextureCacheMetal::Unbind() {
	SetBoundTexture(nil);
	SetBoundSampler(nil);
}

void TextureCacheMetal::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	id<MTLTexture> clut = (__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	drawEngine_->SetDepalTexture(clut, smooth);
}

void TextureCacheMetal::BoundFramebufferTexture() {
	SetBoundTexture((__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW));
}

void TextureCacheMetal::ApplySamplerByKey(const SamplerCacheKey &key) {
	auto iter = samplerCache_.find(key.fullKey);
	if (iter != samplerCache_.end()) {
		SetBoundSampler(iter->second);
		return;
	}
	MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
	sd.minFilter = key.minFilt ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	sd.magFilter = key.magFilt ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	if (key.mipEnable) {
		sd.mipFilter = key.mipFilt ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
	} else {
		sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
	}
	sd.sAddressMode = key.sClamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
	sd.tAddressMode = key.tClamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
	sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
	// Metal only allows anisotropy with linear filters, like we want anyway.
	if (key.aniso && key.minFilt && key.magFilt) {
		sd.maxAnisotropy = std::clamp(1 << key.anisoLevel, 1, 16);
	}
	sd.lodMinClamp = std::max(0.0f, key.minLevel / 256.0f);
	sd.lodMaxClamp = std::max(sd.lodMinClamp, key.maxLevel / 256.0f);
	// Metal samplers have no LOD bias, the shaders apply it (GPU_USE_SHADER_LOD_BIAS).
	id<MTLSamplerState> sampler = [rm_->Device() newSamplerStateWithDescriptor:sd];
	samplerCache_[key.fullKey] = sampler;
	SetBoundSampler(sampler);
}

void TextureCacheMetal::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		ReleaseTexture(entry, true);
		return;
	}

	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	if (plan.decodeToClut8) {
		dstFmt = Draw::DataFormat::R8_UNORM;
	} else if (plan.doReplace) {
		dstFmt = plan.replaced->Format();
	} else if (plan.scaleFactor > 1 || plan.saveTexture) {
		// Scaling and texture dumping work on 8888.
		dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
	}
	MTLPixelFormat mtlFmt = ToMetalFormat(dstFmt);
	int blockSize = 0;
	const bool compressed = Draw::DataFormatIsBlockCompressed(dstFmt, &blockSize);
	const int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);

	// We don't generate mips (yet), so only create the ones we load.
	int levels = plan.depth == 1 ? std::min(plan.levelsToCreate, plan.levelsToLoad) : 1;

	int tw, th;
	plan.GetMipSize(0, &tw, &th);
	tw = std::min(tw, 16384);
	th = std::min(th, 16384);

	MTLTextureDescriptor *td = [MTLTextureDescriptor new];
	td.textureType = plan.depth > 1 ? MTLTextureType3D : MTLTextureType2D;
	td.pixelFormat = mtlFmt;
	td.width = tw;
	td.height = th;
	td.depth = plan.depth;
	td.mipmapLevelCount = levels;
	td.usage = MTLTextureUsageShaderRead;
	td.storageMode = MTLStorageModePrivate;
	td.swizzle = Swizzle16(mtlFmt);
	id<MTLTexture> texture = [rm_->Device() newTextureWithDescriptor:td];
	if (!texture) {
		ERROR_LOG(Log::G3D, "Failed to create texture (%dx%d)", tw, th);
		ReleaseTexture(entry, true);
		return;
	}
	texture.label = [NSString stringWithFormat:@"tex_%08x", entry->addr];

	const int uploadCount = plan.depth > 1 ? plan.depth : levels;
	for (int i = 0; i < uploadCount; i++) {
		// For 3D textures, the loop goes over slices of the single level.
		const int mipLevel = plan.depth > 1 ? 0 : i;
		const int srcLevel = (i == 0) ? plan.baseLevelSrc : i;
		int mipWidth, mipHeight;
		plan.GetMipSize(i, &mipWidth, &mipHeight);

		int stride, rows;
		if (compressed) {
			stride = ((mipWidth + 3) / 4) * blockSize;
			rows = (mipHeight + 3) / 4;
		} else {
			stride = mipWidth * bpp;
			rows = mipHeight;
		}
		size_t size = (size_t)stride * rows;
		if (plan.doReplace && compressed) {
			size = std::max(size, (size_t)plan.replaced->GetLevelDataSizeAfterCopy(plan.baseLevelSrc + i));
		}
		MetalPushAlloc alloc = rm_->Push().Allocate(size, 16);
		LoadTextureLevel(*entry, alloc.ptr, size, stride, plan, srcLevel, dstFmt, TexDecodeFlags{});

		MTLSize copySize = MTLSizeMake(std::min(mipWidth, tw >> mipLevel), std::min(mipHeight, th >> mipLevel), 1);
		rm_->CopyBufferToTexture(alloc.buffer, alloc.offset, stride, (uint32_t)size, copySize, texture, 0, mipLevel, MTLOriginMake(0, 0, plan.depth > 1 ? i : 0), MTLBlitOptionNone, true);
	}

	ReleaseTexture(entry, true);
	entry->texturePtr = (__bridge_retained void *)texture;

	if (plan.depth > 1) {
		entry->status |= TexStatus::IS_3D;
	}
	if (levels == 1) {
		entry->status |= TexStatus::NO_MIPS;
	} else {
		entry->status &= ~TexStatus::NO_MIPS;
	}
	if (plan.doReplace) {
		entry->SetAlphaStatus(plan.replaced->AlphaStatus());
	}
}

void *TextureCacheMetal::GetNativeTextureView(const TexCacheEntry *entry, bool flat) const {
	return entry->texturePtr;
}

bool TextureCacheMetal::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	TextureApplyResult textureResult = ApplyTexture(false);
	if (textureResult.framebuffer) {
		*isFramebuffer = true;
		return GetFramebufferTextureDebug(textureResult.framebuffer, textureResult.framebufferTextureChannel, buffer);
	}
	const TexCacheEntry *entry = textureResult.texCacheEntry;
	if (!entry || !entry->texturePtr) {
		return false;
	}
	id<MTLTexture> texture = Tex(entry);
	if (texture.textureType != MTLTextureType2D || level >= (int)texture.mipmapLevelCount) {
		return false;
	}
	int bpp;
	switch (texture.pixelFormat) {
	case MTLPixelFormatRGBA8Unorm: buffer.Allocate((u32)(texture.width >> level), (u32)(texture.height >> level), GPU_DBG_FORMAT_8888); bpp = 4; break;
	case MTLPixelFormatR8Unorm: buffer.Allocate((u32)(texture.width >> level), (u32)(texture.height >> level), GPU_DBG_FORMAT_8BIT); bpp = 1; break;
	// These hold the PSP's own layouts.
	case MTLPixelFormatB5G6R5Unorm: buffer.Allocate((u32)(texture.width >> level), (u32)(texture.height >> level), GPU_DBG_FORMAT_565); bpp = 2; break;
	case MTLPixelFormatBGR5A1Unorm: buffer.Allocate((u32)(texture.width >> level), (u32)(texture.height >> level), GPU_DBG_FORMAT_5551); bpp = 2; break;
	case MTLPixelFormatABGR4Unorm: buffer.Allocate((u32)(texture.width >> level), (u32)(texture.height >> level), GPU_DBG_FORMAT_4444); bpp = 2; break;
	default: return false;
	}
	int w = std::max(1, (int)texture.width >> level);
	int h = std::max(1, (int)texture.height >> level);
	id<MTLBuffer> readback = [rm_->Device() newBufferWithLength:w * h * bpp options:MTLResourceStorageModeShared];
	rm_->CopyTextureToBuffer(texture, level, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, 1), readback, 0, w * bpp, w * h * bpp, MTLBlitOptionNone);
	rm_->Flush(true);
	memcpy(buffer.GetData(), readback.contents, w * h * bpp);
	*isFramebuffer = false;
	return true;
}
