
// Copyright (c) 2015- PPSSPP Project.

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

#include "base/logging.h"
#include "profiler/profiler.h"

#include "Common/ChunkFile.h"
#include "Common/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/FramebufferCommon.h"

#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

struct VulkanCommandTableEntry {
	uint8_t cmd;
	uint8_t flags;
	uint64_t dirty;
	GPU_Vulkan::CmdFunc func;
};

GPU_Vulkan::CommandInfo GPU_Vulkan::cmdInfo_[256];

// This table gets crunched into a faster form by init.
// TODO: Share this table between the backends. Will have to make another indirection for the function pointers though..
static const VulkanCommandTableEntry commandTable[] = {
	// Changes that dirty the current texture.
	{ GE_CMD_TEXSIZE0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPU_Vulkan::Execute_TexSize0 },

	{ GE_CMD_STENCILTEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_STENCILREPLACEVALUE | DIRTY_BLEND_STATE },

	// Changing the vertex type requires us to flush.
	{ GE_CMD_VERTEXTYPE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_Vulkan::Execute_VertexType },

	{ GE_CMD_PRIM, FLAG_EXECUTE, 0, &GPU_Vulkan::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPU_Vulkan::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPU_Vulkan::Execute_Spline },

	// Changes that trigger data copies. Only flushing on change for LOADCLUT must be a bit of a hack...
	{ GE_CMD_LOADCLUT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPU_Vulkan::Execute_LoadClut },
	{ GE_CMD_TRANSFERSTART, FLAG_FLUSHBEFORE | FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_BlockTransferStart },
};

GPU_Vulkan::GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw),
		vulkan_((VulkanContext *)gfxCtx->GetAPIContext()),
		drawEngine_(vulkan_) {
	UpdateVsyncInterval(true);
	CheckGPUFeatures();

	shaderManagerVulkan_ = new ShaderManagerVulkan(vulkan_);
	pipelineManager_ = new PipelineManagerVulkan(vulkan_);
	framebufferManagerVulkan_ = new FramebufferManagerVulkan(draw, vulkan_);
	framebufferManager_ = framebufferManagerVulkan_;
	textureCacheVulkan_ = new TextureCacheVulkan(draw, vulkan_);
	textureCache_ = textureCacheVulkan_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerVulkan_;

	drawEngine_.SetTextureCache(textureCacheVulkan_);
	drawEngine_.SetFramebufferManager(framebufferManagerVulkan_);
	drawEngine_.SetShaderManager(shaderManagerVulkan_);
	drawEngine_.SetPipelineManager(pipelineManager_);
	framebufferManagerVulkan_->Init();
	framebufferManagerVulkan_->SetTextureCache(textureCacheVulkan_);
	framebufferManagerVulkan_->SetDrawEngine(&drawEngine_);
	framebufferManagerVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetFramebufferManager(framebufferManagerVulkan_);
	textureCacheVulkan_->SetDepalShaderCache(&depalShaderCache_);
	textureCacheVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetDrawEngine(&drawEngine_);

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

	UpdateCmdInfo();

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	textureCacheVulkan_->NotifyConfigChanged();
}

GPU_Vulkan::~GPU_Vulkan() {
	framebufferManagerVulkan_->DestroyAllFBOs(true);
	depalShaderCache_.Clear();
	delete pipelineManager_;
	delete shaderManagerVulkan_;
}

void GPU_Vulkan::CheckGPUFeatures() {
	uint32_t features = 0;
	features |= GPU_SUPPORTS_16BIT_FORMATS;
	if (vulkan_->GetFeaturesEnabled().wideLines) {
		features |= GPU_SUPPORTS_WIDE_LINES;
	}
	if (vulkan_->GetFeaturesEnabled().dualSrcBlend) {
		features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
	}
	if (vulkan_->GetFeaturesEnabled().logicOp) {
		features |= GPU_SUPPORTS_LOGIC_OP;
	}
	if (vulkan_->GetFeaturesEnabled().samplerAnisotropy) {
		features |= GPU_SUPPORTS_ANISOTROPY;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	// Mandatory features on Vulkan, which may be checked in "centralized" code
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_SUPPORTS_FBO;
	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_SUPPORTS_ANY_COPY_IMAGE;
	features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;
	features |= GPU_SUPPORTS_LARGE_VIEWPORTS;
	features |= GPU_SUPPORTS_16BIT_FORMATS;
	gstate_c.featureFlags = features;
}

void GPU_Vulkan::BeginHostFrame() {
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		// Draw everything directly to the backbuffer.
		drawEngine_.SetCmdBuffer(vulkan_->GetSurfaceCommandBuffer());
	}
	drawEngine_.BeginFrame();

	if (resized_) {
		CheckGPUFeatures();
		// In case the GPU changed.
		BuildReportingInfo();
		UpdateCmdInfo();
		drawEngine_.Resized();
		textureCacheVulkan_->NotifyConfigChanged();
	}
	resized_ = false;

	textureCacheVulkan_->StartFrame();
	depalShaderCache_.Decimate();

	framebufferManagerVulkan_->BeginFrameVulkan();

	shaderManagerVulkan_->DirtyShader();
	gstate_c.Dirty(DIRTY_ALL);

	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
}

void GPU_Vulkan::EndHostFrame() {
	drawEngine_.EndFrame();
	framebufferManagerVulkan_->EndFrame();
	textureCacheVulkan_->EndFrame();
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_Vulkan::BuildReportingInfo() {
	const auto &props = vulkan_->GetPhysicalDeviceProperties();
	const auto &features = vulkan_->GetFeaturesAvailable();

#define CHECK_BOOL_FEATURE(n) do { if (features.n) { featureNames += ", " #n; } } while (false)

	std::string featureNames = "";
	CHECK_BOOL_FEATURE(robustBufferAccess);
	CHECK_BOOL_FEATURE(fullDrawIndexUint32);
	CHECK_BOOL_FEATURE(imageCubeArray);
	CHECK_BOOL_FEATURE(independentBlend);
	CHECK_BOOL_FEATURE(geometryShader);
	CHECK_BOOL_FEATURE(tessellationShader);
	CHECK_BOOL_FEATURE(sampleRateShading);
	CHECK_BOOL_FEATURE(dualSrcBlend);
	CHECK_BOOL_FEATURE(logicOp);
	CHECK_BOOL_FEATURE(multiDrawIndirect);
	CHECK_BOOL_FEATURE(drawIndirectFirstInstance);
	CHECK_BOOL_FEATURE(depthClamp);
	CHECK_BOOL_FEATURE(depthBiasClamp);
	CHECK_BOOL_FEATURE(fillModeNonSolid);
	CHECK_BOOL_FEATURE(depthBounds);
	CHECK_BOOL_FEATURE(wideLines);
	CHECK_BOOL_FEATURE(largePoints);
	CHECK_BOOL_FEATURE(alphaToOne);
	CHECK_BOOL_FEATURE(multiViewport);
	CHECK_BOOL_FEATURE(samplerAnisotropy);
	CHECK_BOOL_FEATURE(textureCompressionETC2);
	CHECK_BOOL_FEATURE(textureCompressionASTC_LDR);
	CHECK_BOOL_FEATURE(textureCompressionBC);
	CHECK_BOOL_FEATURE(occlusionQueryPrecise);
	CHECK_BOOL_FEATURE(pipelineStatisticsQuery);
	CHECK_BOOL_FEATURE(vertexPipelineStoresAndAtomics);
	CHECK_BOOL_FEATURE(fragmentStoresAndAtomics);
	CHECK_BOOL_FEATURE(shaderTessellationAndGeometryPointSize);
	CHECK_BOOL_FEATURE(shaderImageGatherExtended);
	CHECK_BOOL_FEATURE(shaderStorageImageExtendedFormats);
	CHECK_BOOL_FEATURE(shaderStorageImageMultisample);
	CHECK_BOOL_FEATURE(shaderStorageImageReadWithoutFormat);
	CHECK_BOOL_FEATURE(shaderStorageImageWriteWithoutFormat);
	CHECK_BOOL_FEATURE(shaderUniformBufferArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderSampledImageArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderStorageBufferArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderStorageImageArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderClipDistance);
	CHECK_BOOL_FEATURE(shaderCullDistance);
	CHECK_BOOL_FEATURE(shaderFloat64);
	CHECK_BOOL_FEATURE(shaderInt64);
	CHECK_BOOL_FEATURE(shaderInt16);
	CHECK_BOOL_FEATURE(shaderResourceResidency);
	CHECK_BOOL_FEATURE(shaderResourceMinLod);
	CHECK_BOOL_FEATURE(sparseBinding);
	CHECK_BOOL_FEATURE(sparseResidencyBuffer);
	CHECK_BOOL_FEATURE(sparseResidencyImage2D);
	CHECK_BOOL_FEATURE(sparseResidencyImage3D);
	CHECK_BOOL_FEATURE(sparseResidency2Samples);
	CHECK_BOOL_FEATURE(sparseResidency4Samples);
	CHECK_BOOL_FEATURE(sparseResidency8Samples);
	CHECK_BOOL_FEATURE(sparseResidency16Samples);
	CHECK_BOOL_FEATURE(sparseResidencyAliased);
	CHECK_BOOL_FEATURE(variableMultisampleRate);
	CHECK_BOOL_FEATURE(inheritedQueries);

#undef CHECK_BOOL_FEATURE

	if (!featureNames.empty()) {
		featureNames = featureNames.substr(2);
	}

	char temp[16384];
	snprintf(temp, sizeof(temp), "v%08x driver v%08x (%s), vendorID=%d, deviceID=%d (features: %s)", props.apiVersion, props.driverVersion, props.deviceName, props.vendorID, props.deviceID, featureNames.c_str());
	reportingPrimaryInfo_ = props.deviceName;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void GPU_Vulkan::ReinitializeInternal() {
	textureCacheVulkan_->Clear(true);
	depalShaderCache_.Clear();
	framebufferManagerVulkan_->DestroyAllFBOs(true);
	framebufferManagerVulkan_->Resized();
}

void GPU_Vulkan::InitClearInternal() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		/*
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		*/
	}
}

void GPU_Vulkan::DumpNextFrame() {
	dumpNextFrame_ = true;
}

void GPU_Vulkan::BeginFrame() {
	ScheduleEvent(GPU_EVENT_BEGIN_FRAME);
}

void GPU_Vulkan::UpdateVsyncInterval(bool force) {
	// TODO
}

void GPU_Vulkan::UpdateCmdInfo() {
	/*
	if (g_Config.bSoftwareSkinning) {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags &= ~FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPU_Vulkan::Execute_VertexTypeSkinning;
	} else {*/
		cmdInfo_[GE_CMD_VERTEXTYPE].flags |= FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPU_Vulkan::Execute_VertexType;
	// }
}

void GPU_Vulkan::BeginFrameInternal() {
}

void GPU_Vulkan::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManager_->SetDisplayFramebuffer(framebuf, stride, format);
}

bool GPU_Vulkan::FramebufferDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->dirtyAfterDisplay;
		vfb->dirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

bool GPU_Vulkan::FramebufferReallyDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->reallyDirtyAfterDisplay;
		vfb->reallyDirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

void GPU_Vulkan::CopyDisplayToOutputInternal() {
	// Flush anything left over.
	drawEngine_.Flush(curCmd_);

	shaderManagerVulkan_->DirtyLastShader();

	framebufferManagerVulkan_->CopyDisplayToOutput();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

// Maybe should write this in ASM...
void GPU_Vulkan::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuloop");
	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo info = cmdInfo[cmd];
		const u8 cmdFlags = info.flags;      // If we stashed the cmdFlags in the top bits of the cmdmem, we could get away with one table lookup instead of two
		const u32 diff = op ^ gstate.cmdmem[cmd];
		// Inlined CheckFlushOp here to get rid of the dumpThisFrame_ check.
		if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
			drawEngine_.Flush(curCmd_);
		}
		gstate.cmdmem[cmd] = op;  // TODO: no need to write if diff==0...
		if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
			downcount = dc;
			(this->*info.func)(op, diff);
			dc = downcount;
		} else if (diff) {
			uint64_t dirty = info.flags >> 8;
			if (dirty)
				gstate_c.Dirty(dirty);
		}
		list.pc += 4;
	}
	downcount = 0;
}

void GPU_Vulkan::FinishDeferred() {
}

inline void GPU_Vulkan::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush(curCmd_);
	}
}

void GPU_Vulkan::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_Vulkan::ExecuteOp(u32 op, u32 diff) {
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

void GPU_Vulkan::Execute_Prim(u32 op, u32 diff) {
	SetDrawType(DRAW_PRIM);

	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);

	if (count == 0)
		return;

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.

	if (gstate.isAntiAliasEnabled()) {
		// Discard AA lines in DOA
		if (prim == GE_PRIM_LINE_STRIP)
			return;
		// Discard AA lines in Summon Night 5
		if ((prim == GE_PRIM_LINES) && gstate.isSkinningEnabled())
			return;
	}

	// This also makes skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);

	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		drawEngine_.SetupVertexDecoder(gstate.vertType);
		// Rough estimate, not sure what's correct.
		cyclesExecuted += EstimatePerVertexCost() * count;
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *inds = 0;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

#ifndef MOBILE_DEVICE
	if (prim > GE_PRIM_RECTANGLES) {
		ERROR_LOG_REPORT_ONCE(reportPrim, G3D, "Unexpected prim type: %d", prim);
	}
#endif

	int bytesRead = 0;
	drawEngine_.SubmitPrim(verts, inds, prim, count, gstate.vertType, &bytesRead);

	int vertexCost = EstimatePerVertexCost();
	gpuStats.vertexGPUCycles += vertexCost * count;
	cyclesExecuted += vertexCost * count;

	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_Vulkan::Execute_VertexType(u32 op, u32 diff) {
	if (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK)) {
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET);
	}
}

void GPU_Vulkan::Execute_Bezier(u32 op, u32 diff) {
	SetDrawType(DRAW_BEZIER);

	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	// This also make skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) {
		DEBUG_LOG_REPORT(G3D, "Bezier + morph: %i", (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT);
	}
	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Bezier + skinning: %i", vertTypeGetNumBoneWeights(gstate.vertType));
	}

	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	int bz_ucount = op & 0xFF;
	int bz_vcount = (op >> 8) & 0xFF;
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;
	int bytesRead = 0;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.bezier = true;
		if (gstate_c.spline_count_u != bz_ucount) {
		    gstate_c.Dirty(DIRTY_BEZIERCOUNTU);
			gstate_c.spline_count_u = bz_ucount;
		}
	}

	drawEngine_.SubmitBezier(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), bz_ucount, bz_vcount, patchPrim, computeNormals, patchFacing, gstate.vertType, &bytesRead);

	gstate_c.bezier = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = bz_ucount * bz_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_Vulkan::Execute_Spline(u32 op, u32 diff) {
	SetDrawType(DRAW_SPLINE);

	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	// This also make skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) {
		DEBUG_LOG_REPORT(G3D, "Spline + morph: %i", (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT);
	}
	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Spline + skinning: %i", vertTypeGetNumBoneWeights(gstate.vertType));
	}

	int sp_ucount = op & 0xFF;
	int sp_vcount = (op >> 8) & 0xFF;
	int sp_utype = (op >> 16) & 0x3;
	int sp_vtype = (op >> 18) & 0x3;
	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;
	u32 vertType = gstate.vertType;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.spline = true;
		if (gstate_c.spline_count_u != sp_ucount) {
		    gstate_c.Dirty(DIRTY_SPLINECOUNTU);
			gstate_c.spline_count_u = sp_ucount;
		}
		if (gstate_c.spline_count_v != sp_vcount) {
		    gstate_c.Dirty(DIRTY_SPLINECOUNTV);
			gstate_c.spline_count_v = sp_vcount;
		}
		if (gstate_c.spline_type_u != sp_utype) {
		    gstate_c.Dirty(DIRTY_SPLINETYPEU);
			gstate_c.spline_type_u = sp_utype;
		}
		if (gstate_c.spline_type_v != sp_vtype) {
		    gstate_c.Dirty(DIRTY_SPLINETYPEV);
			gstate_c.spline_type_v = sp_vtype;
		}
	}

	int bytesRead = 0;
	drawEngine_.SubmitSpline(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), sp_ucount, sp_vcount, sp_utype, sp_vtype, patchPrim, computeNormals, patchFacing, vertType, &bytesRead);

	gstate_c.spline = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = sp_ucount * sp_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_Vulkan::Execute_TexSize0(u32 op, u32 diff) {
	// Render to texture may have overridden the width/height.
	// Don't reset it unless the size is different / the texture has changed.
	if (diff || gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS)) {
		gstate_c.curTextureWidth = gstate.getTextureWidth(0);
		gstate_c.curTextureHeight = gstate.getTextureHeight(0);
		// We will need to reset the texture now.
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET | DIRTY_TEXTURE_PARAMS);
	}
}

void GPU_Vulkan::Execute_LoadClut(u32 op, u32 diff) {
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	textureCacheVulkan_->LoadClut(gstate.getClutAddress(), gstate.getClutLoadBytes());
}

void GPU_Vulkan::Execute_BoneMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_BONEMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.boneMatrix + (op & 0x7F));
	const int end = 12 * 8 - (op & 0x7F);
	int i = 0;

	// If we can't use software skinning, we have to flush and dirty.
	while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
		}
		if (++i >= end) {
			break;
		}
	}

	const int numPlusCount = (op & 0x7F) + i;
	for (int num = op & 0x7F; num < numPlusCount; num += 12) {
		gstate_c.Dirty(DIRTY_BONEMATRIX0 << (num / 12));
	}

	const int count = i;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op + count) & 0x7F);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_Vulkan::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x7F;
	u32 newVal = op << 8;
	if (num < 96 && newVal != ((const u32 *)gstate.boneMatrix)[num]) {
		// Bone matrices should NOT flush when software skinning is enabled!
		Flush();
		gstate_c.Dirty(DIRTY_BONEMATRIX0 << (num / 12));
		((u32 *)gstate.boneMatrix)[num] = newVal;
	}
	num++;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void GPU_Vulkan::FastLoadBoneMatrix(u32 target) {
	const int num = gstate.boneMatrixNumber & 0x7F;
	const int mtxNum = num / 12;
	uint32_t uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if ((num - 12 * mtxNum) != 0) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}
	Flush();
	gstate_c.Dirty(uniformsToDirty);
	gstate.FastLoadBoneMatrix(target);
}

void GPU_Vulkan::DeviceLost() {
	framebufferManagerVulkan_->DeviceLost();
	drawEngine_.DeviceLost();
	pipelineManager_->DeviceLost();
	textureCacheVulkan_->DeviceLost();
	depalShaderCache_.Clear();
	shaderManagerVulkan_->ClearShaders();
}

void GPU_Vulkan::DeviceRestore() {
	vulkan_ = (VulkanContext *)PSP_CoreParameter().graphicsContext->GetAPIContext();
	CheckGPUFeatures();
	BuildReportingInfo();
	UpdateCmdInfo();

	framebufferManagerVulkan_->DeviceRestore(vulkan_);
	drawEngine_.DeviceRestore(vulkan_);
	pipelineManager_->DeviceRestore(vulkan_);
	textureCacheVulkan_->DeviceRestore(vulkan_);
	shaderManagerVulkan_->DeviceRestore(vulkan_);
}

void GPU_Vulkan::GetStats(char *buffer, size_t bufsize) {
	const DrawEngineVulkanStats &drawStats = drawEngine_.GetStats();
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	snprintf(buffer, bufsize - 1,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %i, flushes %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"GPU cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices submitted: %i\n"
		"Cached, Uncached Vertices Drawn: %i, %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i  invalidated: %i\n"
		"Vertex, Fragment, Pipelines loaded: %i, %i, %i\n"
		"Pushbuffer space used: UBO %d, Vtx %d, Idx %d\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManager_->NumVFBs(),
		(int)textureCacheVulkan_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		shaderManagerVulkan_->GetNumVertexShaders(),
		shaderManagerVulkan_->GetNumFragmentShaders(),
		pipelineManager_->GetNumPipelines(),
		drawStats.pushUBOSpaceUsed,
		drawStats.pushVertexSpaceUsed,
		drawStats.pushIndexSpaceUsed
	);
}

void GPU_Vulkan::ClearCacheNextFrame() {
	textureCacheVulkan_->ClearNextFrame();
}

void GPU_Vulkan::ClearShaderCache() {
	// TODO
}

std::vector<FramebufferInfo> GPU_Vulkan::GetFramebufferList() {
	return framebufferManagerVulkan_->GetFramebufferList();
}

void GPU_Vulkan::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCacheVulkan_->Clear(true);
		depalShaderCache_.Clear();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManagerVulkan_->DestroyAllFBOs(true);
		shaderManagerVulkan_->ClearShaders();
		pipelineManager_->Clear();
	}
}

bool GPU_Vulkan::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	return drawEngine_.GetCurrentSimpleVertices(count, vertices, indices);
}

bool GPU_Vulkan::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (drawEngine_.IsCodePtrVertexDecoder(ptr)) {
		name = "VertexDecoderJit";
		return true;
	}
	return false;
}

std::vector<std::string> GPU_Vulkan::DebugGetShaderIDs(DebugShaderType type) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderIDs();
	} else if (type == SHADER_TYPE_PIPELINE) {
		return pipelineManager_->DebugGetObjectIDs(type);
	} else {
		return shaderManagerVulkan_->DebugGetShaderIDs(type);
	}
}

std::string GPU_Vulkan::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	} else if (type == SHADER_TYPE_PIPELINE) {
		return pipelineManager_->DebugGetObjectString(id, type, stringType);
	} else {
		return shaderManagerVulkan_->DebugGetShaderString(id, type, stringType);
	}
}
