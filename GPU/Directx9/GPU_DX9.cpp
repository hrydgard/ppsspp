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

#include <set>

#include "Common/ChunkFile.h"
#include "Common/GraphicsContext.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "profiler/profiler.h"
#include "i18n/i18n.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "gfx/d3d9_state.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

namespace DX9 {

GPU_DX9::GPU_DX9(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw),
		depalShaderCache_(draw),
		drawEngine_(draw) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	deviceEx_ = (LPDIRECT3DDEVICE9EX)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);
	lastVsync_ = g_Config.bVSync ? 1 : 0;
	dxstate.SetVSyncInterval(g_Config.bVSync);

	shaderManagerDX9_ = new ShaderManagerDX9(draw, device_);
	framebufferManagerDX9_ = new FramebufferManagerDX9(draw);
	framebufferManager_ = framebufferManagerDX9_;
	textureCacheDX9_ = new TextureCacheDX9(draw);
	textureCache_ = textureCacheDX9_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerDX9_;

	drawEngine_.SetShaderManager(shaderManagerDX9_);
	drawEngine_.SetTextureCache(textureCacheDX9_);
	drawEngine_.SetFramebufferManager(framebufferManagerDX9_);
	framebufferManagerDX9_->Init();
	framebufferManagerDX9_->SetTextureCache(textureCacheDX9_);
	framebufferManagerDX9_->SetShaderManager(shaderManagerDX9_);
	framebufferManagerDX9_->SetDrawEngine(&drawEngine_);
	textureCacheDX9_->SetFramebufferManager(framebufferManagerDX9_);
	textureCacheDX9_->SetDepalShaderCache(&depalShaderCache_);
	textureCacheDX9_->SetShaderManager(shaderManagerDX9_);

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
	dxstate.Restore();
	textureCache_->NotifyConfigChanged();

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation bacause DX9 is still unsupported.
		g_Config.bHardwareTessellation = false;
		ERROR_LOG(G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
		I18NCategory *gr = GetI18NCategory("Graphics");
		host->NotifyUserMessage(gr->T("Turn off Hardware Tessellation - unsupported"), 2.5f, 0xFF3030FF);
	}
}

void GPU_DX9::CheckGPUFeatures() {
	u32 features = 0;
	features |= GPU_SUPPORTS_16BIT_FORMATS;
	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_PREFER_CPU_DOWNLOAD;

	auto vendor = draw_->GetDeviceCaps().vendor;
	// Accurate depth is required on AMD/nVidia (for reverse Z) so we ignore the compat flag to disable it on those. See #9545
	if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth || vendor == Draw::GPUVendor::VENDOR_AMD || vendor == Draw::GPUVendor::VENDOR_NVIDIA) {
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;
	}

	// VS range culling causes problems on Intel.
	if (vendor != Draw::GPUVendor::VENDOR_INTEL) {
		features |= GPU_SUPPORTS_VS_RANGE_CULLING;
	}

	D3DCAPS9 caps;
	ZeroMemory(&caps, sizeof(caps));
	HRESULT result = 0;
	if (deviceEx_) {
		result = deviceEx_->GetDeviceCaps(&caps);
	} else {
		result = device_->GetDeviceCaps(&caps);
	}
	if (FAILED(result)) {
		WARN_LOG_REPORT(G3D, "Direct3D9: Failed to get the device caps!");
	} else {
		if ((caps.RasterCaps & D3DPRASTERCAPS_ANISOTROPY) != 0 && caps.MaxAnisotropy > 1)
			features |= GPU_SUPPORTS_ANISOTROPY;
		if ((caps.TextureCaps & (D3DPTEXTURECAPS_NONPOW2CONDITIONAL | D3DPTEXTURECAPS_POW2)) == 0)
			features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;
	}

	if (!g_Config.bHighQualityDepth) {
		features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	} else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
		// Assume we always have a 24-bit depth buffer.
		features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	} else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
		features |= GPU_ROUND_DEPTH_TO_16BIT;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	gstate_c.featureFlags = features;
}

GPU_DX9::~GPU_DX9() {
	framebufferManagerDX9_->DestroyAllFBOs();
	delete framebufferManagerDX9_;
	delete textureCache_;
	shaderManagerDX9_->ClearCache(true);
	delete shaderManagerDX9_;
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_DX9::BuildReportingInfo() {
	using namespace Draw;
	DrawContext *thin3d = gfxCtx_->GetDrawContext();

	reportingPrimaryInfo_ = thin3d->GetInfoString(InfoField::VENDORSTRING);
	reportingFullInfo_ = reportingPrimaryInfo_ + " - " + System_GetProperty(SYSPROP_GPUDRIVER_VERSION) + " - " + thin3d->GetInfoString(InfoField::SHADELANGVERSION);
}

void GPU_DX9::DeviceLost() {
	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	shaderManagerDX9_->ClearCache(false);
	textureCacheDX9_->Clear(false);
	framebufferManagerDX9_->DeviceLost();
}

void GPU_DX9::DeviceRestore() {
	// Nothing needed.
}

void GPU_DX9::InitClear() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		dxstate.depthWrite.set(true);
		dxstate.colorMask.set(true, true, true, true);
		device_->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
	}
}

void GPU_DX9::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	UpdateCmdInfo();
	if (resized_) {
		CheckGPUFeatures();
		framebufferManager_->Resized();
		drawEngine_.Resized();
		shaderManagerDX9_->DirtyShader();
		textureCacheDX9_->NotifyConfigChanged();
		resized_ = false;
	}
}

void GPU_DX9::ReapplyGfxState() {
	dxstate.Restore();
	GPUCommon::ReapplyGfxState();
}

void GPU_DX9::BeginFrame() {
	// Turn off vsync when unthrottled
	int desiredVSyncInterval = g_Config.bVSync ? 1 : 0;
	if (PSP_CoreParameter().unthrottle || PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL)
		desiredVSyncInterval = 0;
	if (desiredVSyncInterval != lastVsync_) {
		dxstate.SetVSyncInterval(desiredVSyncInterval);
		lastVsync_ = desiredVSyncInterval;
	}

	textureCacheDX9_->StartFrame();
	drawEngine_.DecimateTrackedVertexArrays();
	depalShaderCache_.Decimate();
	// fragmentTestCache_.Decimate();

	GPUCommon::BeginFrame();
	shaderManagerDX9_->DirtyShader();

	framebufferManager_->BeginFrame();
}

void GPU_DX9::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	GPUDebug::NotifyDisplay(framebuf, stride, format);
	framebufferManagerDX9_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_DX9::CopyDisplayToOutput() {
	dxstate.depthWrite.set(true);
	dxstate.colorMask.set(true, true, true, true);

	drawEngine_.Flush();

	framebufferManagerDX9_->CopyDisplayToOutput();
	framebufferManagerDX9_->EndFrame();

	// shaderManager_->EndFrame();
	shaderManagerDX9_->DirtyLastShader();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void GPU_DX9::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_DX9::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE)) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_DX9::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_DX9::ExecuteOp(u32 op, u32 diff) {
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

void GPU_DX9::GetStats(char *buffer, size_t bufsize) {
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
		(int)framebufferManagerDX9_->NumVFBs(),
		(int)textureCacheDX9_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
		shaderManagerDX9_->GetNumVertexShaders(),
		shaderManagerDX9_->GetNumFragmentShaders()
	);
}

void GPU_DX9::ClearCacheNextFrame() {
	textureCacheDX9_->ClearNextFrame();
}

void GPU_DX9::ClearShaderCache() {
	shaderManagerDX9_->ClearCache(true);
}

void GPU_DX9::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCacheDX9_->Clear(true);
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManagerDX9_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_DX9::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_.DebugGetShaderIDs(type);
	default:
		return shaderManagerDX9_->DebugGetShaderIDs(type);
	}
}

std::string GPU_DX9::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_.DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerDX9_->DebugGetShaderString(id, type, stringType);
	}
}

}  // namespace DX9
