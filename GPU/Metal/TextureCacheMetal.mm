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
#include "Common/StringUtils.h"
#include "Common/File/VFS/VFS.h"
#include "GPU/Common/PostShader.h"
#include "Common/GPU/thin3d.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
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
	CompileScalingShader();
}

void TextureCacheMetal::NotifyConfigChanged() {
	TextureCacheCommon::NotifyConfigChanged();
	CompileScalingShader();
}

static std::string ReadShaderSrc(const Path &filename) {
	size_t size = 0;
	uint8_t *data = g_VFS.ReadFile(filename.c_str(), &size);
	if (!data) {
		return std::string();
	}
	std::string src((const char *)data, size);
	delete[] data;
	return src;
}

void TextureCacheMetal::ClearScalingShaders() {
	singlePassCS_ = nil;
	multipassCS_.clear();
	multipassScratchDescs_.clear();
	multipassStageDescs_.clear();
	textureShader_.clear();
	shaderScaleFactor_ = 0;
}

id<MTLComputePipelineState> TextureCacheMetal::CompileComputeShader(const std::string &source, const char *tag) {
	MetalTranslatedShader translated;
	std::string error;
	if (!TranslateVulkanGLSLToMSL(ShaderStage::Compute, source.c_str(), &translated, &error)) {
		ERROR_LOG(Log::G3D, "Failed to translate texture scaling shader %s: %s", tag, error.c_str());
		return nil;
	}
	id<MTLFunction> function = CreateMetalFunction(rm_->Device(), translated, tag, &error);
	if (!function) {
		ERROR_LOG(Log::G3D, "Failed to compile texture scaling shader %s: %s", tag, error.c_str());
		return nil;
	}
	NSError *nsError = nil;
	id<MTLComputePipelineState> pipeline = [rm_->Device() newComputePipelineStateWithFunction:function error:&nsError];
	if (!pipeline) {
		ERROR_LOG(Log::G3D, "Failed to create texture scaling pipeline %s: %s", tag, nsError.localizedDescription.UTF8String);
	}
	return pipeline;
}

// Compiles synchronously, which only happens when the setting changes or the game starts.
void TextureCacheMetal::CompileScalingShader() {
	if (!rm_) {
		return;
	}
	if (!g_Config.bTexHardwareScaling || g_Config.sTextureShaderName != textureShader_) {
		ClearScalingShaders();
	} else if (HasScalingShader()) {
		return;
	}
	if (!g_Config.bTexHardwareScaling) {
		return;
	}

	ReloadAllPostShaderInfo(draw_);
	const TextureShaderInfo *shaderInfo = GetTextureShaderInfo(g_Config.sTextureShaderName);
	if (!shaderInfo || shaderInfo->computeShaderFiles.empty()) {
		return;
	}

	if (shaderInfo->computeShaderFiles.size() > 1) {
		if (!GetTextureScaleMultipassLayout(shaderInfo->scaleFactor, &multipassScratchDescs_, &multipassStageDescs_)) {
			ERROR_LOG(Log::G3D, "Unsupported multipass scale factor %d in section '%s'", shaderInfo->scaleFactor, shaderInfo->section.c_str());
			return;
		}
		const size_t expectedShaderCount = shaderInfo->scaleFactor == 2 ? 3 : 4;
		if (shaderInfo->computeShaderFiles.size() != expectedShaderCount) {
			ERROR_LOG(Log::G3D, "Expected %d compute shader stages in section '%s', got %d", (int)expectedShaderCount, shaderInfo->section.c_str(), (int)shaderInfo->computeShaderFiles.size());
			ClearScalingShaders();
			return;
		}
		for (const Path &file : shaderInfo->computeShaderFiles) {
			id<MTLComputePipelineState> pipeline = CompileComputeShader(ReadShaderSrc(file), shaderInfo->section.c_str());
			if (!pipeline) {
				ClearScalingShaders();
				return;
			}
			multipassCS_.push_back(pipeline);
		}
	} else {
		std::string source = StringFromFormat(textureScaleUploadShader, ReadShaderSrc(shaderInfo->computeShaderFiles[0]).c_str());
		singlePassCS_ = CompileComputeShader(source, shaderInfo->section.c_str());
		if (!singlePassCS_) {
			return;
		}
	}

	if (!shaderInfo->constantBuffer.empty()) {
		// Only the smiley test shader has one, and it's going away.
		ERROR_LOG(Log::G3D, "Texture scaling shaders with constant buffers aren't supported on Metal");
		ClearScalingShaders();
		return;
	}

	textureShader_ = g_Config.sTextureShaderName;
	shaderScaleFactor_ = shaderInfo->scaleFactor;
}

bool TextureCacheMetal::ScaleBufferToTexture(MetalPushAlloc src, id<MTLTexture> dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight) {
	// The bindings of the shaders (Vulkan binding = Metal index): 0 the output image, 1 the input buffer,
	// 3 the input image. All have 8x8 threadgroups.

	if (singlePassCS_) {
		MetalRecordedDispatch &dispatch = rm_->RecordUploadDispatch();
		dispatch.threadsPerThreadgroup = MTLSizeMake(8, 8, 1);
		dispatch.pipeline = (__bridge void *)singlePassCS_;
		dispatch.textures[0] = (__bridge void *)dst;
		dispatch.buffers[1] = (__bridge void *)src.buffer;
		dispatch.bufferOffsets[1] = src.offset;
		const int32_t params[2] = { srcWidth, srcHeight };
		memcpy(dispatch.pushConstants, params, sizeof(params));
		dispatch.pushConstantSize = sizeof(params);
		// One thread per input pixel.
		dispatch.threadgroups = MTLSizeMake((srcWidth + 7) / 8, (srcHeight + 7) / 8, 1);
		return true;
	}
	if (multipassCS_.empty()) {
		return false;
	}

	std::vector<id<MTLTexture>> scratch;
	for (const TextureScaleScratchDesc &desc : multipassScratchDescs_) {
		MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:srcWidth * desc.widthScale height:srcHeight * desc.heightScale mipmapped:NO];
		td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		td.storageMode = MTLStorageModePrivate;
		id<MTLTexture> tex = [rm_->Device() newTextureWithDescriptor:td];
		if (!tex) {
			ERROR_LOG(Log::G3D, "Failed to allocate multipass scratch images");
			return false;
		}
		tex.label = [NSString stringWithUTF8String:desc.tag];
		// Used by this frame's upload only.
		rm_->DeferRelease(tex);
		scratch.push_back(tex);
	}

	for (const TextureScaleStageDesc &stage : multipassStageDescs_) {
		if (stage.shaderIndex >= multipassCS_.size()) {
			return false;
		}
		MetalRecordedDispatch &dispatch = rm_->RecordUploadDispatch();
		dispatch.threadsPerThreadgroup = MTLSizeMake(8, 8, 1);
		dispatch.pipeline = (__bridge void *)multipassCS_[stage.shaderIndex];
		if (stage.inputScratch < 0) {
			dispatch.buffers[1] = (__bridge void *)src.buffer;
			dispatch.bufferOffsets[1] = src.offset;
		} else {
			dispatch.textures[3] = (__bridge void *)scratch[stage.inputScratch];
		}
		dispatch.textures[0] = (__bridge void *)(stage.outputScratch < 0 ? dst : scratch[stage.outputScratch]);
		const int32_t params[4] = {
			srcWidth * stage.srcWidthScale,
			srcHeight * stage.srcHeightScale,
			stage.useFinalOutputSize ? dstWidth : srcWidth * stage.dstWidthScale,
			stage.useFinalOutputSize ? dstHeight : srcHeight * stage.dstHeightScale,
		};
		memcpy(dispatch.pushConstants, params, sizeof(params));
		dispatch.pushConstantSize = sizeof(params);
		// One thread per output pixel.
		dispatch.threadgroups = MTLSizeMake((params[2] + 7) / 8, (params[3] + 7) / 8, 1);
	}
	return true;
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
	ClearScalingShaders();
	SetBoundTexture(nil);
	SetBoundSampler(nil);
	draw_ = nullptr;
	rm_ = nullptr;
}

void TextureCacheMetal::DeviceRestore(Draw::DrawContext *draw) {
	rm_ = (MetalRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	TextureCacheCommon::DeviceRestore(draw);
	CompileScalingShader();
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
	// Like the Vulkan backend, mipmapping is controlled by the LOD clamps alone (maxLevel 0 means the first level).
	sd.mipFilter = key.mipFilt ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
	sd.sAddressMode = key.sClamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
	sd.tAddressMode = key.tClamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
	sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
	// Metal only allows anisotropy with linear filters, like we want anyway.
	if (key.aniso && key.minFilt && key.magFilt) {
		sd.maxAnisotropy = std::clamp(1 << key.anisoLevel, 1, 16);
	}
	sd.lodMinClamp = std::max(0.0f, key.minLevel / 256.0f);
	sd.lodMaxClamp = key.maxLevel == 9 * 256 ? FLT_MAX : std::max(sd.lodMinClamp, key.maxLevel / 256.0f);
	// Metal samplers have no LOD bias, the shaders apply it (GPU_USE_SHADER_LOD_BIAS).
	id<MTLSamplerState> sampler = [rm_->Device() newSamplerStateWithDescriptor:sd];
	samplerCache_[key.fullKey] = sampler;
	SetBoundSampler(sampler);
}

void TextureCacheMetal::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	plan.hardwareScaling = g_Config.bTexHardwareScaling && HasScalingShader();
	plan.slowScaler = !plan.hardwareScaling;
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

	// Like the Vulkan backend: at max quality, give small textures (rarely menu backgrounds, unlike 512x512 ones)
	// a full mip chain, generating the levels the game didn't supply. That needs a format the GPU can render
	// mips in, so not compressed or 16-bit ones.
	if (g_Config.iTexFiltering == TEX_FILTER_AUTO_MAX_QUALITY && plan.w <= 256 && plan.h <= 256 && (!plan.doReplace || plan.replaced->Format() == Draw::DataFormat::R8G8B8A8_UNORM)) {
		if (plan.maxPossibleLevels > plan.levelsToCreate) {
			dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
		}
		plan.levelsToCreate = plan.maxPossibleLevels;
	}
	MTLPixelFormat mtlFmt = ToMetalFormat(dstFmt);
	int blockSize = 0;
	const bool compressed = Draw::DataFormatIsBlockCompressed(dstFmt, &blockSize);
	const int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);

	int levels = plan.depth == 1 ? plan.levelsToCreate : 1;
	const int levelsToLoad = plan.depth == 1 ? std::min(plan.levelsToCreate, plan.levelsToLoad) : 1;

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
	// Scaled with a compute shader, which decodes unscaled and writes each level.
	const bool computeUpload = plan.hardwareScaling && plan.scaleFactor > 1 && dstFmt == Draw::DataFormat::R8G8B8A8_UNORM &&
		plan.depth == 1 && !plan.doReplace && !plan.saveTexture;

	td.usage = MTLTextureUsageShaderRead | (computeUpload ? MTLTextureUsageShaderWrite : 0);
	td.storageMode = MTLStorageModePrivate;
	td.swizzle = Swizzle16(mtlFmt);
	id<MTLTexture> texture = [rm_->Device() newTextureWithDescriptor:td];
	if (!texture) {
		ERROR_LOG(Log::G3D, "Failed to create texture (%dx%d)", tw, th);
		ReleaseTexture(entry, true);
		return;
	}
	texture.label = [NSString stringWithFormat:@"tex_%08x", entry->addr];

	const int uploadCount = plan.depth > 1 ? plan.depth : levelsToLoad;
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
			size = std::max(size, (size_t)plan.replaced->GetLevelDataSizeAfterCopy(i));
		}
		if (computeUpload) {
			const int srcWidth = gstate.getTextureWidth(i);
			const int srcHeight = gstate.getTextureHeight(i);
			const int srcStride = srcWidth * 4;
			const size_t srcSize = (size_t)srcStride * srcHeight;
			BuildTexturePlan unscaledPlan = plan;
			unscaledPlan.scaleFactor = 1;
			MetalPushAlloc alloc = rm_->Push().Allocate(srcSize, 16);
			LoadTextureLevel(*entry, alloc.ptr, srcSize, srcStride, unscaledPlan, srcLevel, dstFmt, TexDecodeFlags{});
			id<MTLTexture> view = [texture newTextureViewWithPixelFormat:mtlFmt textureType:MTLTextureType2D levels:NSMakeRange(i, 1) slices:NSMakeRange(0, 1)];
			if (!ScaleBufferToTexture(alloc, view, srcWidth, srcHeight, mipWidth, mipHeight)) {
				// Leaves the level empty.
				WARN_LOG_ONCE(mtlscalefail, Log::G3D, "Hardware texture scaling failed");
			}
			rm_->DeferRelease(view);
			continue;
		}

		MetalPushAlloc alloc = rm_->Push().Allocate(size, 16);
		LoadTextureLevel(*entry, alloc.ptr, size, stride, plan, srcLevel, dstFmt, TexDecodeFlags{});

		MTLSize copySize = MTLSizeMake(std::min(mipWidth, tw >> mipLevel), std::min(mipHeight, th >> mipLevel), 1);
		rm_->CopyBufferToTexture(alloc.buffer, alloc.offset, stride, (uint32_t)size, copySize, texture, 0, mipLevel, MTLOriginMake(0, 0, plan.depth > 1 ? i : 0), MTLBlitOptionNone, true);
	}

	if (levels > levelsToLoad) {
		// generateMipmapsForTexture rebuilds every level below the first, so a view keeps the loaded ones.
		const int base = levelsToLoad - 1;
		id<MTLTexture> view = [texture newTextureViewWithPixelFormat:mtlFmt textureType:MTLTextureType2D levels:NSMakeRange(base, levels - base) slices:NSMakeRange(0, 1)];
		rm_->GenerateMips(view, true);
		rm_->DeferRelease(view);
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
