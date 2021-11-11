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

#include <set>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/GraphicsContext.h"
#include "Common/System/System.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPU_D3D11::GPU_D3D11(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw), drawEngine_(draw,
	(ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE),
	(ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT)) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	D3D_FEATURE_LEVEL featureLevel = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);

	stockD3D11.Create(device_);

	shaderManagerD3D11_ = new ShaderManagerD3D11(draw, device_, context_, featureLevel);
	framebufferManagerD3D11_ = new FramebufferManagerD3D11(draw);
	framebufferManager_ = framebufferManagerD3D11_;
	textureCacheD3D11_ = new TextureCacheD3D11(draw);
	textureCache_ = textureCacheD3D11_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerD3D11_;
	depalShaderCache_ = new DepalShaderCacheD3D11(draw);
	drawEngine_.SetShaderManager(shaderManagerD3D11_);
	drawEngine_.SetTextureCache(textureCacheD3D11_);
	drawEngine_.SetFramebufferManager(framebufferManagerD3D11_);
	drawEngine_.Init();
	framebufferManagerD3D11_->SetTextureCache(textureCacheD3D11_);
	framebufferManagerD3D11_->SetShaderManager(shaderManagerD3D11_);
	framebufferManagerD3D11_->SetDrawEngine(&drawEngine_);
	framebufferManagerD3D11_->Init();
	textureCacheD3D11_->SetFramebufferManager(framebufferManagerD3D11_);
	textureCacheD3D11_->SetDepalShaderCache(depalShaderCache_);
	textureCacheD3D11_->SetShaderManager(shaderManagerD3D11_);

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

GPU_D3D11::~GPU_D3D11() {
	delete depalShaderCache_;
	framebufferManagerD3D11_->DestroyAllFBOs();
	delete framebufferManagerD3D11_;
	shaderManagerD3D11_->ClearShaders();
	delete shaderManagerD3D11_;
	delete textureCacheD3D11_;
	stockD3D11.Destroy();
}

void GPU_D3D11::CheckGPUFeatures() {
	u32 features = 0;

	features |= GPU_SUPPORTS_BLEND_MINMAX;

	// Accurate depth is required because the Direct3D API does not support inverse Z.
	// So we cannot incorrectly use the viewport transform as the depth range on Direct3D.
	// TODO: Breaks text in PaRappa for some reason?
	features |= GPU_SUPPORTS_ACCURATE_DEPTH;

#ifndef _M_ARM
	// TODO: Do proper feature detection
	features |= GPU_SUPPORTS_ANISOTROPY;
#endif

	features |= GPU_SUPPORTS_DEPTH_TEXTURE;
	features |= GPU_SUPPORTS_TEXTURE_NPOT;
	if (draw_->GetDeviceCaps().dualSourceBlend)
		features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
	if (draw_->GetDeviceCaps().depthClampSupported)
		features |= GPU_SUPPORTS_DEPTH_CLAMP;
	if (draw_->GetDeviceCaps().clipDistanceSupported)
		features |= GPU_SUPPORTS_CLIP_DISTANCE;
	if (draw_->GetDeviceCaps().cullDistanceSupported)
		features |= GPU_SUPPORTS_CULL_DISTANCE;
	if (!draw_->GetBugs().Has(Draw::Bugs::BROKEN_NAN_IN_CONDITIONAL)) {
		// Ignore the compat setting if clip and cull are both enabled.
		// When supported, we can do the depth side of range culling more correctly.
		const bool supported = draw_->GetDeviceCaps().clipDistanceSupported && draw_->GetDeviceCaps().cullDistanceSupported;
		const bool disabled = PSP_CoreParameter().compat.flags().DisableRangeCulling;
		if (supported || !disabled) {
			features |= GPU_SUPPORTS_VS_RANGE_CULLING;
		}
	}

	features |= GPU_SUPPORTS_COPY_IMAGE;
	features |= GPU_SUPPORTS_TEXTURE_FLOAT;
	features |= GPU_SUPPORTS_INSTANCE_RENDERING;
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;

	uint32_t fmt4444 = draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16);
	uint32_t fmt1555 = draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16);
	uint32_t fmt565 = draw_->GetDataFormatSupport(Draw::DataFormat::R5G6B5_UNORM_PACK16);
	if ((fmt4444 & Draw::FMT_TEXTURE) && (fmt565 & Draw::FMT_TEXTURE) && (fmt1555 & Draw::FMT_TEXTURE)) {
		features |= GPU_SUPPORTS_16BIT_FORMATS;
	}

	if (draw_->GetDeviceCaps().logicOpSupported) {
		features |= GPU_SUPPORTS_LOGIC_OP;
	}

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
void GPU_D3D11::BuildReportingInfo() {
	using namespace Draw;
	DrawContext *thin3d = gfxCtx_->GetDrawContext();

	reportingPrimaryInfo_ = thin3d->GetInfoString(InfoField::VENDORSTRING);
	reportingFullInfo_ = reportingPrimaryInfo_ + " - " + System_GetProperty(SYSPROP_GPUDRIVER_VERSION) + " - " + thin3d->GetInfoString(InfoField::SHADELANGVERSION);
}

void GPU_D3D11::DeviceLost() {
	draw_->InvalidateCachedState();
	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	shaderManagerD3D11_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();
	textureCacheD3D11_->Clear(false);

	GPUCommon::DeviceLost();
}

void GPU_D3D11::DeviceRestore() {
	GPUCommon::DeviceRestore();
	// Nothing needed.
}

void GPU_D3D11::InitClear() {
	if (!framebufferManager_->UseBufferedRendering()) {
		// device_->Clear(0, NULL, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
	}
}

void GPU_D3D11::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	UpdateCmdInfo();
	if (resized_) {
		CheckGPUFeatures();
		framebufferManager_->Resized();
		drawEngine_.Resized();
		textureCacheD3D11_->NotifyConfigChanged();
		shaderManagerD3D11_->DirtyLastShader();
		resized_ = false;
	}
}

void GPU_D3D11::ReapplyGfxState() {
	GPUCommon::ReapplyGfxState();

	// TODO: Dirty our caches for depth states etc
}

void GPU_D3D11::EndHostFrame() {
	// Probably not really necessary.
	draw_->InvalidateCachedState();
}

void GPU_D3D11::BeginFrame() {
	GPUCommon::BeginFrame();

	textureCacheD3D11_->StartFrame();
	drawEngine_.BeginFrame();
	depalShaderCache_->Decimate();
	// fragmentTestCache_.Decimate();

	shaderManagerD3D11_->DirtyLastShader();

	framebufferManagerD3D11_->BeginFrame();
	gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
}

void GPU_D3D11::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// TODO: Some games like Spongebob - Yellow Avenger, never change framebuffer, they blit to it.
	// So breaking on frames doesn't work. Might want to move this to sceDisplay vsync.
	GPUDebug::NotifyDisplay(framebuf, stride, format);
	framebufferManagerD3D11_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_D3D11::CopyDisplayToOutput(bool reallyDirty) {
	// Flush anything left over.
	drawEngine_.Flush();

	float blendColor[4]{};
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], blendColor, 0xFFFFFFFF);

	framebufferManagerD3D11_->CopyDisplayToOutput(reallyDirty);
	framebufferManagerD3D11_->EndFrame();

	// shaderManager_->EndFrame();
	shaderManagerD3D11_->DirtyLastShader();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void GPU_D3D11::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_D3D11::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE)) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_D3D11::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_D3D11::ExecuteOp(u32 op, u32 diff) {
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

void GPU_D3D11::GetStats(char *buffer, size_t bufsize) {
	size_t offset = FormatGPUStatsCommon(buffer, bufsize);
	buffer += offset;
	bufsize -= offset;
	if ((int)bufsize < 0)
		return;
	snprintf(buffer, bufsize,
		"Vertex, Fragment shaders loaded: %d, %d\n",
		shaderManagerD3D11_->GetNumVertexShaders(),
		shaderManagerD3D11_->GetNumFragmentShaders()
	);
}

void GPU_D3D11::ClearCacheNextFrame() {
	textureCacheD3D11_->ClearNextFrame();
}

void GPU_D3D11::ClearShaderCache() {
	shaderManagerD3D11_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();
}

void GPU_D3D11::DoState(PointerWrap &p) {
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

std::vector<std::string> GPU_D3D11::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_->DebugGetShaderIDs(type);
	default:
		return shaderManagerD3D11_->DebugGetShaderIDs(type);
	}
}

std::string GPU_D3D11::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_->DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerD3D11_->DebugGetShaderString(id, type, stringType);
	}
}
