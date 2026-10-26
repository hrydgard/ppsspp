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

#include "Common/Log.h"
#include "Common/Data/Text/StringWriter.h"
#include "Common/GPU/thin3d.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/GPUState.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Metal/GPU_Metal.h"
#include "GPU/Metal/DrawEngineMetal.h"
#include "GPU/Metal/FramebufferManagerMetal.h"
#include "GPU/Metal/ShaderManagerMetal.h"
#include "GPU/Metal/TextureCacheMetal.h"

GPU_Metal::GPU_Metal(GraphicsContext *gfxCtx, Draw::DrawContext *draw) : GPUCommonHW(gfxCtx, draw) {
	gstate_c.SetUseFlags(CheckGPUFeatures());

	drawEngine_ = new DrawEngineMetal(draw);
	shaderManagerMetal_ = new ShaderManagerMetal(draw);
	framebufferManagerMetal_ = new FramebufferManagerMetal(draw);
	framebufferManager_ = framebufferManagerMetal_;
	textureCacheMetal_ = new TextureCacheMetal(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheMetal_;
	drawEngineCommon_ = drawEngine_;
	shaderManager_ = shaderManagerMetal_;

	drawEngine_->SetGPUCommon(this);
	drawEngine_->SetTextureCache(textureCacheMetal_);
	drawEngine_->SetFramebufferManager(framebufferManagerMetal_);
	drawEngine_->SetShaderManager(shaderManagerMetal_);
	drawEngine_->Init();
	framebufferManagerMetal_->SetTextureCache(textureCacheMetal_);
	framebufferManagerMetal_->SetDrawEngine(drawEngine_);
	framebufferManagerMetal_->SetShaderManager(shaderManagerMetal_);
	framebufferManagerMetal_->Init(msaaLevel_);
	textureCacheMetal_->SetFramebufferManager(framebufferManagerMetal_);
	textureCacheMetal_->SetShaderManager(shaderManagerMetal_);
	textureCacheMetal_->SetDrawEngine(drawEngine_);

	UpdateCmdInfo();
	BuildReportingInfo();
	textureCache_->NotifyConfigChanged();

	std::string discID = g_paramSFO.GetDiscID();
	if (!discID.empty()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) / (discID + ".mtlshadercache");
		LoadCache(shaderCachePath_);
	}
}

namespace {

// Increase when the stored keys change meaning (MetalPipelineKey, the shader IDs, the formats).
constexpr uint32_t METAL_CACHE_MAGIC = 0x4C54534D;  // "MSTL"
constexpr uint32_t METAL_CACHE_VERSION = 2;

struct MetalCacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t useFlags;
	uint32_t pipelineRecordSize;  // Catches layout changes that forgot the version.
	int32_t numVertexShaders;
	int32_t numFragmentShaders;
	int32_t numPipelines;
};

}  // namespace

// Runs on the loader thread, before the game starts. Waits for the pipelines, like the Vulkan backend, so
// they don't stutter in the first frames.
void GPU_Metal::LoadCache(const Path &filename) {
	if (!g_Config.bShaderCache) {
		return;
	}
	FILE *f = File::OpenCFile(filename, "rb");
	if (!f) {
		return;
	}
	if (!VerifyShaderCacheChecksum(filename)) {
		WARN_LOG(Log::G3D, "Old or damaged Metal shader cache (no matching checksum) - rebuilding.");
		fclose(f);
		File::Delete(filename);
		return;
	}
	MetalCacheHeader header{};
	bool ok = fread(&header, sizeof(header), 1, f) == 1 && header.magic == METAL_CACHE_MAGIC && header.version == METAL_CACHE_VERSION &&
		header.pipelineRecordSize == drawEngine_->PipelineRecordSize();
	if (ok && header.useFlags != gstate_c.GetUseFlags()) {
		// The IDs carry the flags they were made with, so this just means some of them won't be used.
		WARN_LOG(Log::G3D, "Metal shader cache useFlags mismatch, %08x, expected %08x", header.useFlags, gstate_c.GetUseFlags());
	}
	double start = time_now_d();
	ok = ok && shaderManagerMetal_->LoadCache(f, header.numVertexShaders, header.numFragmentShaders);
	ok = ok && drawEngine_->LoadPipelines(f, header.numPipelines, true);
	fclose(f);
	if (ok) {
		INFO_LOG(Log::G3D, "Loaded Metal shader cache: %d vertex and %d fragment shaders, %d pipelines in %0.1f ms",
			header.numVertexShaders, header.numFragmentShaders, header.numPipelines, (time_now_d() - start) * 1000.0);
	} else {
		WARN_LOG(Log::G3D, "Incompatible or damaged Metal shader cache, deleting it");
		File::Delete(filename);
	}
}

void GPU_Metal::SaveCache(const Path &filename) {
	// After DeviceLost (which saved it), the shaders are gone. Don't overwrite the cache with nothing.
	if (!g_Config.bShaderCache || !filename.Valid() || !draw_) {
		return;
	}
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f) {
		return;
	}
	MetalCacheHeader header{};
	header.magic = METAL_CACHE_MAGIC;
	header.version = METAL_CACHE_VERSION;
	header.useFlags = gstate_c.GetUseFlags();
	header.pipelineRecordSize = drawEngine_->PipelineRecordSize();
	header.numVertexShaders = shaderManagerMetal_->GetNumVertexShaders();
	header.numFragmentShaders = shaderManagerMetal_->GetNumFragmentShaders();
	header.numPipelines = drawEngine_->GetNumPipelines();
	bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
	ok = ok && shaderManagerMetal_->SaveCache(f);
	ok = ok && drawEngine_->SavePipelines(f);
	fclose(f);
	ok = ok && AppendShaderCacheChecksum(filename);
	if (ok) {
		INFO_LOG(Log::G3D, "Saved Metal shader cache: %d vertex and %d fragment shaders, %d pipelines", header.numVertexShaders, header.numFragmentShaders, header.numPipelines);
	} else {
		ERROR_LOG(Log::G3D, "Failed to write the Metal shader cache, disk full?");
		File::Delete(filename);
	}
}

GPU_Metal::~GPU_Metal() {
	SaveCache(shaderCachePath_);
	if (draw_) {
		MetalRenderManager *rm = (MetalRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->WaitIdle();
	}
	// The other managers are deleted in ~GPUCommonHW.
	delete drawEngine_;
	drawEngine_ = nullptr;
	drawEngineCommon_ = nullptr;
}

u32 GPU_Metal::CheckGPUFeatures() const {
	u32 features = GPUCommonHW::CheckGPUFeatures();
	uint32_t fmt4444 = draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16);
	uint32_t fmt1555 = draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16);
	uint32_t fmt565 = draw_->GetDataFormatSupport(Draw::DataFormat::R5G6B5_UNORM_PACK16);
	if ((fmt4444 & Draw::FMT_TEXTURE) && (fmt565 & Draw::FMT_TEXTURE) && (fmt1555 & Draw::FMT_TEXTURE)) {
		features |= GPU_USE_16BIT_FORMATS;
	}
	return CheckGPUFeaturesLate(features);
}

void GPU_Metal::BeginHostFrame(const DisplayLayoutConfig &config) {
	GPUCommonHW::BeginHostFrame(config);

	drawEngine_->BeginFrame();
	textureCache_->StartFrame();
	framebufferManager_->BeginFrame(config);

	gstate_c.Dirty(DIRTY_ALL);

	if (gstate_c.useFlagsChanged) {
		WARN_LOG(Log::G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		drawEngine_->ClearPipelines();
		shaderManager_->ClearShaders();
		framebufferManager_->ClearAllDepthBuffers();
		gstate_c.useFlagsChanged = false;
	}
}

void GPU_Metal::FinishDeferred() {
	drawEngine_->FinishDeferred();
}

void GPU_Metal::DeviceLost() {
	// The managers free their caches (pipelines, samplers, depth stencil states) right away, so nothing
	// recorded can still be waiting to use them.
	if (draw_) {
		MetalRenderManager *rm = (MetalRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->WaitIdle();
	}
	SaveCache(shaderCachePath_);
	drawEngine_->ClearPipelines();
	GPUCommonHW::DeviceLost();
}

void GPU_Metal::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);

	// DeviceLost saved the cache and then threw everything away. Load it again, or the next save would
	// overwrite the file with only what gets drawn from now on.
	if (shaderCachePath_.Valid()) {
		LoadCache(shaderCachePath_);
	}
}

void GPU_Metal::GetStats(StringWriter &w) {
	w.F("Vertex, Fragment shaders, Pipelines loaded: %d, %d, %d\n",
		shaderManagerMetal_->GetNumVertexShaders(),
		shaderManagerMetal_->GetNumFragmentShaders(),
		drawEngine_->GetNumPipelines());
	FormatGPUStatsCommon(w);
}
