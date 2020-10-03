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

#include "GPU/GX2/GPU_GX2.h"

#include <set>

#include "Common/Serialize/Serializer.h"
#include "Common/GraphicsContext.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/GPU_GX2.h"
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/DrawEngineGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/GX2Util.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPU_GX2::GPU_GX2(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw), drawEngine_(draw,
	(GX2ContextState *)draw->GetNativeObject(Draw::NativeObject::CONTEXT)) {
	context_ = (GX2ContextState *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	StockGX2::Init();

	shaderManagerGX2_ = new ShaderManagerGX2(draw, context_);
	framebufferManagerGX2_ = new FramebufferManagerGX2(draw);
	framebufferManager_ = framebufferManagerGX2_;
	textureCacheGX2_ = new TextureCacheGX2(draw);
	textureCache_ = textureCacheGX2_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerGX2_;
	depalShaderCache_ = new DepalShaderCacheGX2(draw);
	drawEngine_.SetShaderManager(shaderManagerGX2_);
	drawEngine_.SetTextureCache(textureCacheGX2_);
	drawEngine_.SetFramebufferManager(framebufferManagerGX2_);
	framebufferManagerGX2_->SetTextureCache(textureCacheGX2_);
	framebufferManagerGX2_->SetShaderManager(shaderManagerGX2_);
	framebufferManagerGX2_->SetDrawEngine(&drawEngine_);
	framebufferManagerGX2_->Init();
	textureCacheGX2_->SetFramebufferManager(framebufferManagerGX2_);
	textureCacheGX2_->SetDepalShaderCache(depalShaderCache_);
	textureCacheGX2_->SetShaderManager(shaderManagerGX2_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.
	UpdateCmdInfo();
	CheckGPUFeatures();

	BuildReportingInfo();

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	textureCache_->NotifyConfigChanged();
}

GPU_GX2::~GPU_GX2() {
	delete depalShaderCache_;
	framebufferManagerGX2_->DestroyAllFBOs();
	delete framebufferManagerGX2_;
	shaderManagerGX2_->ClearShaders();
	delete shaderManagerGX2_;
	delete textureCacheGX2_;
	draw_->BindPipeline(nullptr);
}

void GPU_GX2::CheckGPUFeatures() {
	u32 features = 0;

	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_PREFER_CPU_DOWNLOAD;

	// Accurate depth is required on AMD/nVidia (for reverse Z) so we ignore the compat flag to disable it on those. See #9545
	if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth) {
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;  // Breaks text in PaRappa for some reason.
	}

	features |= GPU_SUPPORTS_ANISOTROPY;
	features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;
	features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
	features |= GPU_SUPPORTS_TEXTURE_FLOAT;
	features |= GPU_SUPPORTS_INSTANCE_RENDERING;
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_SUPPORTS_16BIT_FORMATS;
	features |= GPU_SUPPORTS_LOGIC_OP;
	features |= GPU_SUPPORTS_COPY_IMAGE;
//	features |= GPU_SUPPORTS_FRAMEBUFFER_BLIT;

	if (!g_Config.bHighQualityDepth && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
		features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	} else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
		// Use fragment rounding on desktop and GLES3, most accurate.
		features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
	} else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
		features |= GPU_ROUND_DEPTH_TO_16BIT;
	}

	// The Phantasy Star hack :(
	if (PSP_CoreParameter().compat.flags().DepthRangeHack && (features & GPU_SUPPORTS_ACCURATE_DEPTH) == 0) {
		features |= GPU_USE_DEPTH_RANGE_HACK;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	gstate_c.featureFlags = features;
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_GX2::BuildReportingInfo() {
	using namespace Draw;
	DrawContext *thin3d = gfxCtx_->GetDrawContext();

	reportingPrimaryInfo_ = thin3d->GetInfoString(InfoField::VENDORSTRING);
	reportingFullInfo_ = reportingPrimaryInfo_ + " - " + System_GetProperty(SYSPROP_GPUDRIVER_VERSION) + " - " + thin3d->GetInfoString(InfoField::SHADELANGVERSION);
}

void GPU_GX2::DeviceLost() {
	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	shaderManagerGX2_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();
	textureCacheGX2_->Clear(false);
	framebufferManagerGX2_->DeviceLost();
}

void GPU_GX2::DeviceRestore() {
	// Nothing needed.
}

void GPU_GX2::InitClear() {
	if (!framebufferManager_->UseBufferedRendering()) {
		// device_->Clear(0, NULL, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
	}
}

void GPU_GX2::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	UpdateCmdInfo();
	if (resized_) {
		CheckGPUFeatures();
		framebufferManager_->Resized();
		drawEngine_.Resized();
		textureCacheGX2_->NotifyConfigChanged();
		shaderManagerGX2_->DirtyLastShader();
		resized_ = false;
	}
}

void GPU_GX2::ReapplyGfxState() {
	GPUCommon::ReapplyGfxState();

	// TODO: Dirty our caches for depth states etc
}

void GPU_GX2::EndHostFrame() {
	// Tell the DrawContext that it's time to reset everything.
	draw_->BindPipeline(nullptr);
}

void GPU_GX2::BeginFrame() {
	GPUCommon::BeginFrame();

	textureCacheGX2_->StartFrame();
	drawEngine_.BeginFrame();
	depalShaderCache_->Decimate();
	// fragmentTestCache_.Decimate();

	shaderManagerGX2_->DirtyLastShader();

	framebufferManagerGX2_->BeginFrame();
	gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
}

void GPU_GX2::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// TODO: Some games like Spongebob - Yellow Avenger, never change framebuffer, they blit to it.
	// So breaking on frames doesn't work. Might want to move this to sceDisplay vsync.
	GPUDebug::NotifyDisplay(framebuf, stride, format);
	framebufferManagerGX2_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_GX2::CopyDisplayToOutput(bool reallyDirty) {
	GX2SetColorControlReg(&StockGX2::blendDisabledColorWrite);
	GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0xF]);

	drawEngine_.Flush();

	framebufferManagerGX2_->CopyDisplayToOutput(reallyDirty);
	framebufferManagerGX2_->EndFrame();

	// shaderManager_->EndFrame();
	shaderManagerGX2_->DirtyLastShader();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void GPU_GX2::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_GX2::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE)) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_GX2::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_GX2::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const CommandInfo info = cmdInfo_[cmd];
	const u8 cmdFlags = info.flags;
	if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
		(this->*info.func)(op, diff);
	} else if (diff) {
		uint64_t dirty = info.flags >> 8;
		if (dirty)
			gstate_c.Dirty(dirty);
	}
}

void GPU_GX2::GetStats(char *buffer, size_t bufsize) {
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	snprintf(buffer, bufsize - 1,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %i, flushes %i, clears %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"GPU cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices submitted: %i\n"
		"Cached, Uncached Vertices Drawn: %i, %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i  invalidated: %i\n"
		"Readbacks: %d, uploads: %d\n"
		"Vertex, Fragment shaders loaded: %i, %i\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numClears,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManagerGX2_->NumVFBs(),
		(int)textureCacheGX2_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
		shaderManagerGX2_->GetNumVertexShaders(),
		shaderManagerGX2_->GetNumFragmentShaders()
	);
}

void GPU_GX2::ClearCacheNextFrame() {
	textureCacheGX2_->ClearNextFrame();
}

void GPU_GX2::ClearShaderCache() {
	shaderManagerGX2_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();
}

void GPU_GX2::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_->Clear(true);
		depalShaderCache_->Clear();
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_GX2::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_->DebugGetShaderIDs(type);
	default:
		return shaderManagerGX2_->DebugGetShaderIDs(type);
	}
}

std::string GPU_GX2::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_->DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerGX2_->DebugGetShaderString(id, type, stringType);
	}
}
