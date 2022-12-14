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

#include "Common/Serialize/Serializer.h"
#include "Common/GraphicsContext.h"
#include "Common/System/System.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "Common/GPU/D3D9/D3D9StateCache.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPU_DX9::GPU_DX9(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw),
	  drawEngine_(draw) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	deviceEx_ = (LPDIRECT3DDEVICE9EX)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);

	shaderManagerDX9_ = new ShaderManagerDX9(draw, device_);
	framebufferManagerDX9_ = new FramebufferManagerDX9(draw);
	framebufferManager_ = framebufferManagerDX9_;
	textureCacheDX9_ = new TextureCacheDX9(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheDX9_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerDX9_;

	drawEngine_.SetShaderManager(shaderManagerDX9_);
	drawEngine_.SetTextureCache(textureCacheDX9_);
	drawEngine_.SetFramebufferManager(framebufferManagerDX9_);
	drawEngine_.Init();
	framebufferManagerDX9_->SetTextureCache(textureCacheDX9_);
	framebufferManagerDX9_->SetShaderManager(shaderManagerDX9_);
	framebufferManagerDX9_->SetDrawEngine(&drawEngine_);
	framebufferManagerDX9_->Init(msaaLevel_);
	textureCacheDX9_->SetFramebufferManager(framebufferManagerDX9_);
	textureCacheDX9_->SetShaderManager(shaderManagerDX9_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.
	UpdateCmdInfo();
	gstate_c.SetUseFlags(CheckGPUFeatures());

	BuildReportingInfo();

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	dxstate.Restore();
	textureCache_->NotifyConfigChanged();

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation bacause DX9 is still unsupported.
		ERROR_LOG(G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
		auto gr = GetI18NCategory("Graphics");
		host->NotifyUserMessage(gr->T("Turn off Hardware Tessellation - unsupported"), 2.5f, 0xFF3030FF);
	}
}

u32 GPU_DX9::CheckGPUFeatures() const {
	u32 features = GPUCommon::CheckGPUFeatures();
	features |= GPU_USE_16BIT_FORMATS;
	features |= GPU_USE_TEXTURE_LOD_CONTROL;

	// Accurate depth is required because the Direct3D API does not support inverse Z.
	// So we cannot incorrectly use the viewport transform as the depth range on Direct3D.
	features |= GPU_USE_ACCURATE_DEPTH;

	return CheckGPUFeaturesLate(features);
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
	shaderManagerDX9_->ClearCache(false);
	textureCacheDX9_->Clear(false);
	GPUCommon::DeviceLost();
}

void GPU_DX9::DeviceRestore() {
	GPUCommon::DeviceRestore();
	// Nothing needed.
}

void GPU_DX9::InitClear() {
	if (!framebufferManager_->UseBufferedRendering()) {
		dxstate.depthWrite.set(true);
		dxstate.colorMask.set(0xF);
		device_->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
	}
}

void GPU_DX9::ReapplyGfxState() {
	dxstate.Restore();
	GPUCommon::ReapplyGfxState();
}

void GPU_DX9::BeginFrame() {
	textureCacheDX9_->StartFrame();
	drawEngine_.BeginFrame();

	GPUCommon::BeginFrame();
	shaderManagerDX9_->DirtyShader();

	framebufferManager_->BeginFrame();
}

void GPU_DX9::CopyDisplayToOutput(bool reallyDirty) {
	dxstate.depthWrite.set(true);
	dxstate.colorMask.set(0xF);

	drawEngine_.Flush();

	framebufferManagerDX9_->CopyDisplayToOutput(reallyDirty);

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
	size_t offset = FormatGPUStatsCommon(buffer, bufsize);
	buffer += offset;
	bufsize -= offset;
	if ((int)bufsize < 0)
		return;
	snprintf(buffer, bufsize,
		"Vertex, Fragment shaders loaded: %i, %i\n",
		shaderManagerDX9_->GetNumVertexShaders(),
		shaderManagerDX9_->GetNumFragmentShaders()
	);
}

void GPU_DX9::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_->Clear(true);
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_DX9::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_TEXTURE:
		return textureCache_->GetTextureShaderCache()->DebugGetShaderIDs(type);
	default:
		return shaderManagerDX9_->DebugGetShaderIDs(type);
	}
}

std::string GPU_DX9::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_TEXTURE:
		return textureCache_->GetTextureShaderCache()->DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerDX9_->DebugGetShaderString(id, type, stringType);
	}
}
