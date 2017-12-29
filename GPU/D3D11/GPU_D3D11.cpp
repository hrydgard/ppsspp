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

#include "GPU/D3D11/GPU_D3D11.h"

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
#include "Core/Reporting.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

struct D3D11CommandTableEntry {
	uint8_t cmd;
	uint8_t flags;
	uint64_t dirty;
	GPU_D3D11::CmdFunc func;
};

// This table gets crunched into a faster form by init.
static const D3D11CommandTableEntry commandTable[] = {
	// Changes that dirty the current texture.
	{ GE_CMD_TEXSIZE0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPUCommon::Execute_TexSize0 },

	// Changing the vertex type requires us to flush.
	{ GE_CMD_VERTEXTYPE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_VertexType },

	{ GE_CMD_PRIM, FLAG_EXECUTE, 0, &GPU_D3D11::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPUCommon::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPUCommon::Execute_Spline },

	// Changes that trigger data copies. Only flushing on change for LOADCLUT must be a bit of a hack...
	{ GE_CMD_LOADCLUT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPU_D3D11::Execute_LoadClut },
};

GPU_D3D11::CommandInfo GPU_D3D11::cmdInfo_[256]{};

GPU_D3D11::GPU_D3D11(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw), drawEngine_(draw,
	(ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE),
	(ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT)) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	D3D_FEATURE_LEVEL featureLevel = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);
	lastVsync_ = g_Config.bVSync ? 1 : 0;

	stockD3D11.Create(device_);

	shaderManagerD3D11_ = new ShaderManagerD3D11(device_, context_, featureLevel);
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
	framebufferManagerD3D11_->Init();
	framebufferManagerD3D11_->SetTextureCache(textureCacheD3D11_);
	framebufferManagerD3D11_->SetShaderManager(shaderManagerD3D11_);
	framebufferManagerD3D11_->SetDrawEngine(&drawEngine_);
	textureCacheD3D11_->SetFramebufferManager(framebufferManagerD3D11_);
	textureCacheD3D11_->SetDepalShaderCache(depalShaderCache_);
	textureCacheD3D11_->SetShaderManager(shaderManagerD3D11_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	memset(cmdInfo_, 0, sizeof(cmdInfo_));

	// Import both the global and local command tables, and check for dupes
	std::set<u8> dupeCheck;
	for (size_t i = 0; i < commonCommandTableSize; i++) {
		const u8 cmd = commonCommandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		cmdInfo_[cmd].flags |= (uint64_t)commonCommandTable[i].flags | (commonCommandTable[i].dirty << 8);
		cmdInfo_[cmd].func = commonCommandTable[i].func;
		if ((cmdInfo_[cmd].flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) && !cmdInfo_[cmd].func) {
			Crash();
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(commandTable); i++) {
		const u8 cmd = commandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		cmdInfo_[cmd].flags |= (uint64_t)commandTable[i].flags | (commandTable[i].dirty << 8);
		cmdInfo_[cmd].func = commandTable[i].func;
		if ((cmdInfo_[cmd].flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) && !cmdInfo_[cmd].func) {
			Crash();
		}
	}

	// Find commands missing from the table.
	for (int i = 0; i < 0xEF; i++) {
		if (dupeCheck.find((u8)i) == dupeCheck.end()) {
			ERROR_LOG(G3D, "Command missing from table: %02x (%i)", i, i);
		}
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.
	UpdateCmdInfo();

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
	draw_->BindPipeline(nullptr);
	stockD3D11.Destroy();
}

void GPU_D3D11::UpdateCmdInfo() {
	if (g_Config.bSoftwareSkinning) {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags &= ~FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexTypeSkinning;
	} else {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags |= FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexType;
	}

	CheckGPUFeatures();
}

void GPU_D3D11::CheckGPUFeatures() {
	u32 features = 0;

	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_PREFER_CPU_DOWNLOAD;

	// Accurate depth is required on AMD so we ignore the compat flag to disable it on those. See #9545
	if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth || draw_->GetDeviceCaps().vendor == Draw::GPUVendor::VENDOR_AMD) {
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;  // Breaks text in PaRappa for some reason.
	}

#ifndef _M_ARM
	// TODO: Do proper feature detection
	features |= GPU_SUPPORTS_ANISOTROPY;
#endif

	features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;
	features |= GPU_SUPPORTS_LARGE_VIEWPORTS;
	if (draw_->GetDeviceCaps().dualSourceBlend)
		features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
	features |= GPU_SUPPORTS_ANY_COPY_IMAGE;
	features |= GPU_SUPPORTS_TEXTURE_FLOAT;
	features |= GPU_SUPPORTS_INSTANCE_RENDERING;
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_SUPPORTS_FBO;

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
	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	shaderManagerD3D11_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();
	textureCacheD3D11_->Clear(false);
	framebufferManagerD3D11_->DeviceLost();
}

void GPU_D3D11::DeviceRestore() {
	// Nothing needed.
}

void GPU_D3D11::InitClear() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		// device_->Clear(0, NULL, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
	}
}

void GPU_D3D11::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	UpdateCmdInfo();
	if (resized_) {
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
	// Tell the DrawContext that it's time to reset everything.
	draw_->BindPipeline(nullptr);
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
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManagerD3D11_->SetDisplayFramebuffer(framebuf, stride, format);
}

bool GPU_D3D11::FramebufferDirty() {
	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->dirtyAfterDisplay;
		vfb->dirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

bool GPU_D3D11::FramebufferReallyDirty() {
	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->reallyDirtyAfterDisplay;
		vfb->reallyDirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

void GPU_D3D11::CopyDisplayToOutput() {
	float blendColor[4]{};
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], blendColor, 0xFFFFFFFF);

	drawEngine_.Flush();

	framebufferManagerD3D11_->CopyDisplayToOutput();
	framebufferManagerD3D11_->EndFrame();

	// shaderManager_->EndFrame();
	shaderManagerD3D11_->DirtyLastShader();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

// Maybe should write this in ASM...
void GPU_D3D11::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuloop");
	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo &info = cmdInfo[cmd];
		const u32 diff = op ^ gstate.cmdmem[cmd];
		if (diff == 0) {
			if (info.flags & FLAG_EXECUTE) {
				downcount = dc;
				(this->*info.func)(op, diff);
				dc = downcount;
			}
		} else {
			uint64_t flags = info.flags;
			if (flags & FLAG_FLUSHBEFOREONCHANGE) {
				drawEngine_.Flush();
			}
			gstate.cmdmem[cmd] = op;
			if (flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) {
				downcount = dc;
				(this->*info.func)(op, diff);
				dc = downcount;
			} else {
				uint64_t dirty = flags >> 8;
				if (dirty)
					gstate_c.Dirty(dirty);
			}
		}
		list.pc += 4;
	}
	downcount = 0;
}

void GPU_D3D11::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_D3D11::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
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

void GPU_D3D11::Execute_Prim(u32 op, u32 diff) {
	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	if (count == 0)
		return;

	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);
	SetDrawType(DRAW_PRIM, prim);

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.

	if (gstate.isAntiAliasEnabled()) {
		// Discard AA lines in DOA
		if (prim == GE_PRIM_LINE_STRIP)
			return;
		// Discard AA lines in Summon Night 5
		if ((prim == GE_PRIM_LINES) && gstate.isSkinningEnabled())
			return;
	}

	// This also make skipping drawing very effective.
	framebufferManagerD3D11_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		drawEngine_.SetupVertexDecoder(gstate.vertType);
		// Rough estimate, not sure what's correct.
		cyclesExecuted += EstimatePerVertexCost() * count;
		return;
	}

	u32 vertexAddr = gstate_c.vertexAddr;
	if (!Memory::IsValidAddress(vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", vertexAddr);
		return;
	}

	void *verts = Memory::GetPointerUnchecked(vertexAddr);
	void *inds = 0;
	u32 vertexType = gstate.vertType;
	if ((vertexType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		u32 indexAddr = gstate_c.indexAddr;
		if (!Memory::IsValidAddress(indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(indexAddr);
	}

#ifndef MOBILE_DEVICE
	if (prim > GE_PRIM_RECTANGLES) {
		ERROR_LOG_REPORT_ONCE(reportPrim, G3D, "Unexpected prim type: %d", prim);
	}
#endif

	if (gstate_c.dirty & DIRTY_VERTEXSHADER_STATE) {
		vertexCost_ = EstimatePerVertexCost();
	}
	gpuStats.vertexGPUCycles += vertexCost_ * count;
	cyclesExecuted += vertexCost_* count;

	int bytesRead = 0;
	UpdateUVScaleOffset();
	drawEngine_.SubmitPrim(verts, inds, prim, count, vertexType, &bytesRead);

	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(vertexType, count, bytesRead);
}

void GPU_D3D11::Execute_LoadClut(u32 op, u32 diff) {
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	textureCacheD3D11_->LoadClut(gstate.getClutAddress(), gstate.getClutLoadBytes());
	// This could be used to "dirty" textures with clut.
}

void GPU_D3D11::GetStats(char *buffer, size_t bufsize) {
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
		(int)framebufferManagerD3D11_->NumVFBs(),
		(int)textureCacheD3D11_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
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
		textureCacheD3D11_->Clear(true);
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManagerD3D11_->DestroyAllFBOs();
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
