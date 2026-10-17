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
	case Draw::DataFormat::R8G8B8A8_UNORM:
	default:
		return MTLPixelFormatRGBA8Unorm;
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
	boundTexture_ = nil;
	boundSampler_ = nil;
	draw_ = nullptr;
	rm_ = nullptr;
}

void TextureCacheMetal::DeviceRestore(Draw::DrawContext *draw) {
	rm_ = (MetalRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	TextureCacheCommon::DeviceRestore(draw);
}

void TextureCacheMetal::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	if (entry->texturePtr) {
		// Balances the __bridge_retained in BuildTexture. Command buffers that use it keep it alive as needed.
		id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)entry->texturePtr;
		tex = nil;
		entry->texturePtr = nullptr;
	}
}

void TextureCacheMetal::BindTexture(TexCacheEntry *entry) {
	if (!entry || !entry->texturePtr) {
		Unbind();
		return;
	}
	drawEngine_->SetDepalTexture(nil, false);
	boundTexture_ = Tex(entry);
}

void TextureCacheMetal::Unbind() {
	boundTexture_ = nil;
	boundSampler_ = nil;
}

void TextureCacheMetal::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	id<MTLTexture> clut = (__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	drawEngine_->SetDepalTexture(clut, smooth);
}

void TextureCacheMetal::BoundFramebufferTexture() {
	boundTexture_ = (__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
}

void TextureCacheMetal::ApplySamplerByKey(const SamplerCacheKey &key) {
	auto iter = samplerCache_.find(key.fullKey);
	if (iter != samplerCache_.end()) {
		boundSampler_ = iter->second;
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
	boundSampler_ = sampler;
}

void TextureCacheMetal::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		ReleaseTexture(entry, true);
		return;
	}

	Draw::DataFormat dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
	if (plan.decodeToClut8) {
		dstFmt = Draw::DataFormat::R8_UNORM;
	} else if (plan.doReplace) {
		dstFmt = plan.replaced->Format();
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
	id<MTLTexture> texture = [rm_->Device() newTextureWithDescriptor:td];
	if (!texture) {
		ERROR_LOG(Log::G3D, "Failed to create texture (%dx%d)", tw, th);
		ReleaseTexture(entry, true);
		return;
	}
	texture.label = [NSString stringWithFormat:@"tex_%08x", entry->addr];

	id<MTLBlitCommandEncoder> upload = rm_->UploadEncoder();
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
		[upload copyFromBuffer:alloc.buffer sourceOffset:alloc.offset sourceBytesPerRow:stride sourceBytesPerImage:size sourceSize:copySize
			toTexture:texture destinationSlice:0 destinationLevel:mipLevel destinationOrigin:MTLOriginMake(0, 0, plan.depth > 1 ? i : 0)];
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
	default: return false;
	}
	int w = std::max(1, (int)texture.width >> level);
	int h = std::max(1, (int)texture.height >> level);
	id<MTLBuffer> readback = [rm_->Device() newBufferWithLength:w * h * bpp options:MTLResourceStorageModeShared];
	[rm_->BlitEncoder() copyFromTexture:texture sourceSlice:0 sourceLevel:level sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
		toBuffer:readback destinationOffset:0 destinationBytesPerRow:w * bpp destinationBytesPerImage:w * h * bpp];
	rm_->Flush(true);
	memcpy(buffer.GetData(), readback.contents, w * h * bpp);
	*isFramebuffer = false;
	return true;
}
