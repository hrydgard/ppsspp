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

#include <cstring>

#include "Common/DbgNew.h"
#include "Common/Profiler/Profiler.h"
#include "Core/System.h"
#include "Core/Util/PPGeDraw.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Null/GPU_Null.h"

namespace {

// Only here for the vertex decoders (vertex sizes, bounding box tests). Queues and draws nothing.
class NullDrawEngine : public DrawEngineCommon {
public:
	void DeviceLost() override {}
	void DeviceRestore(Draw::DrawContext *draw) override {}
	void Flush() override {}
	void DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation) override {}
};

class GPU_Null : public GPUCommon {
public:
	explicit GPU_Null(GraphicsContext *gfxCtx);

	u32 CheckGPUFeatures() const override { return 0; }
	void UpdateCmdInfo() override {}
	void ExecuteOp(u32 op, u32 diff) override;

	void GetStats(StringWriter &w) override {}
	std::vector<const VirtualFramebuffer *> GetFramebufferList() const override { return {}; }

	void DeviceLost() override { draw_ = nullptr; }
	void DeviceRestore(Draw::DrawContext *draw) override {
		draw_ = draw;
		PPGeSetDrawContext(draw_);
	}

	void CheckDisplayResized() override { displayResized_ = false; }
	void CheckConfigChanged(const DisplayLayoutConfig &config) override;

	bool FramebufferDirty() override { return true; }
	bool FramebufferReallyDirty() override { return true; }

	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override {
		NotifyDisplay(framebuf, stride, format);
	}
	void SetCurFramebufferDirty(bool dirty) override {}
	void PrepareCopyDisplayToOutput(const DisplayLayoutConfig &config) override {}
	void CopyDisplayToOutput(const DisplayLayoutConfig &config) override {}
	bool PresentedThisFrame() const override { return false; }

	// Nothing is cached, and all of PSP memory is up to date, so the copies and sets are left to the caller.
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override {}
	bool PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags = GPUCopyFlag::NONE) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	void PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat format, int rectWidth) override {
		// Remembered for EstimateFillCycles.
		NoteVideoRange(addr, (u32)size);
	}
	bool PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags) override { return false; }
	bool PerformReadbackStencilToMemory(u32 dest, int size) override { return false; }

	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BlockTransferStart(u32 op, u32 diff);

	typedef void (GPU_Null::*CmdFunc)(u32 op, u32 diff);

protected:
	void FastRunLoop(DisplayList &list) override;
	void BuildReportingInfo() override {
		reportingPrimaryInfo_ = "Null";
		reportingFullInfo_ = "Null";
	}

private:
	void SkipCurve(int numPointsU, int numPointsV);

	NullDrawEngine drawEngine_;
};

struct NullCommandInfo {
	u8 flags;
	GPU_Null::CmdFunc func;
};
static NullCommandInfo nullCmdInfo[256];

struct NullCommandTableEntry {
	u8 cmd;
	u8 flags;
	GPU_Null::CmdFunc func;
};

// Only the commands that do something beyond setting their register. The rest only matter for drawing.
const NullCommandTableEntry nullCommandTable[] = {
	{ GE_CMD_OFFSETADDR, FLAG_EXECUTE, &GPUCommon::Execute_OffsetAddr },
	{ GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, &GPUCommon::Execute_Origin },
	{ GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_Jump },
	{ GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_Call },
	{ GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_Ret },
	{ GE_CMD_END, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_End },
	{ GE_CMD_VADDR, FLAG_EXECUTE, &GPUCommon::Execute_Vaddr },
	{ GE_CMD_IADDR, FLAG_EXECUTE, &GPUCommon::Execute_Iaddr },
	{ GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_BJump },
	{ GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, &GPUCommon::Execute_BoundingBox },

	{ GE_CMD_PRIM, FLAG_EXECUTE, &GPU_Null::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_EXECUTE, &GPU_Null::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_EXECUTE, &GPU_Null::Execute_Spline },

	{ GE_CMD_MORPHWEIGHT0, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT1, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT2, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT3, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT4, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT5, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT6, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT7, FLAG_EXECUTEONCHANGE, &GPUCommon::Execute_MorphWeight },

	{ GE_CMD_TRANSFERSTART, FLAG_EXECUTE | FLAG_READS_PC, &GPU_Null::Execute_BlockTransferStart },

	{ GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_WorldMtxNum },
	{ GE_CMD_WORLDMATRIXDATA, FLAG_EXECUTE, &GPUCommon::Execute_WorldMtxData },
	{ GE_CMD_VIEWMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_ViewMtxNum },
	{ GE_CMD_VIEWMATRIXDATA, FLAG_EXECUTE, &GPUCommon::Execute_ViewMtxData },
	{ GE_CMD_PROJMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_ProjMtxNum },
	{ GE_CMD_PROJMATRIXDATA, FLAG_EXECUTE, &GPUCommon::Execute_ProjMtxData },
	{ GE_CMD_TGENMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_TgenMtxNum },
	{ GE_CMD_TGENMATRIXDATA, FLAG_EXECUTE, &GPUCommon::Execute_TgenMtxData },
	{ GE_CMD_BONEMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, &GPUCommon::Execute_BoneMtxNum },
	{ GE_CMD_BONEMATRIXDATA, FLAG_EXECUTE, &GPUCommon::Execute_BoneMtxData },

	{ GE_CMD_VAP, FLAG_EXECUTE, &GPUCommon::Execute_ImmVertexAlphaPrim },
};

GPU_Null::GPU_Null(GraphicsContext *gfxCtx) : GPUCommon(gfxCtx, nullptr) {
	memset(nullCmdInfo, 0, sizeof(nullCmdInfo));
	for (const NullCommandTableEntry &entry : nullCommandTable) {
		_dbg_assert_(nullCmdInfo[entry.cmd].func == nullptr);
		nullCmdInfo[entry.cmd].flags = entry.flags;
		nullCmdInfo[entry.cmd].func = entry.func;
	}

	drawEngine_.SetGPUCommon(this);
	drawEngine_.Init();
	drawEngineCommon_ = &drawEngine_;

	// Nothing is queued, so there's nothing to flush.
	flushOnParams_ = false;

	BuildReportingInfo();
}

void GPU_Null::CheckConfigChanged(const DisplayLayoutConfig &config) {
	if (configChanged_) {
		drawEngine_.NotifyConfigChanged();
		configChanged_ = false;
	}
}

void GPU_Null::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("null_runloop");
	if (!Memory::IsValidAddress(list.pc)) {
		downcount = 0;
		return;
	}
	const NullCommandInfo *cmdInfo = nullCmdInfo;
	int dc = downcount;
	for (; dc > 0; --dc) {
		const u32 op = Memory::ReadUnchecked_U32(list.pc);
		const u32 cmd = op >> 24;
		const NullCommandInfo &info = cmdInfo[cmd];
		const u32 diff = op ^ gstate.cmdmem[cmd];
		if (diff) {
			gstate.cmdmem[cmd] = op;
		}
		if ((info.flags & FLAG_EXECUTE) || (diff && (info.flags & FLAG_EXECUTEONCHANGE))) {
			downcount = dc;
			(this->*info.func)(op, diff);
			dc = downcount;
		}
		list.pc += 4;
	}
	downcount = 0;
}

void GPU_Null::ExecuteOp(u32 op, u32 diff) {
	const NullCommandInfo &info = nullCmdInfo[op >> 24];
	if ((info.flags & FLAG_EXECUTE) || (diff && (info.flags & FLAG_EXECUTEONCHANGE))) {
		(this->*info.func)(op, diff);
	}
}

void GPU_Null::Execute_Prim(u32 op, u32 diff) {
	const u32 count = op & 0xFFFF;
	if (count == 0)
		return;
	FlushImm();

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG(Log::G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}
	const void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *inds = nullptr;
	const u32 vertType = gstate.vertType;
	if ((vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const int indexShift = ((vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
		if (!Memory::IsValidRange(gstate_c.indexAddr, count << indexShift)) {
			ERROR_LOG(Log::G3D, "Bad index address %08x (%d)!", gstate_c.indexAddr, count);
			return;
		}
		inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	const VertexDecoder *dec = drawEngine_.GetVertexDecoder(GetVertTypeID(vertType, gstate.getUVGenMode()));
	const GEPrimitiveType prim = static_cast<GEPrimitiveType>((op >> 16) & 7);
	if (gstate.isModeThrough()) {
		cyclesExecuted += EstimateFillCycles(prim, verts, inds, count, dec, vertType);
	}
	const int cycles = EstimatePerVertexCost() * count;
	gpuStats.perFrame.vertexGPUCycles += cycles;
	if (!PSP_CoreParameter().compat.flags().FastEmulatedGPU) {
		cyclesExecuted += cycles;
	}

	gstate_c.AdvanceVerts(vertType, count, count * dec->VertexSize());
}

// Moves the pointers past a curve like DrawEngineCommon::SubmitCurve would.
void GPU_Null::SkipCurve(int numPointsU, int numPointsV) {
	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG(Log::G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}
	const u32 vertType = gstate.vertType;
	if ((vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE && !Memory::IsValidAddress(gstate_c.indexAddr)) {
		ERROR_LOG(Log::G3D, "Bad index address %08x!", gstate_c.indexAddr);
		return;
	}

	const int count = numPointsU * numPointsV;
	int bytesRead = 0;
	// SubmitCurve draws nothing, and reads nothing, with fewer than four points either way.
	if (numPointsU >= 4 && numPointsV >= 4) {
		bytesRead = count * drawEngine_.GetVertexDecoder(GetVertTypeID(vertType, gstate.getUVGenMode()))->VertexSize();
	}
	gstate_c.AdvanceVerts(vertType, count, bytesRead);
}

void GPU_Null::Execute_Bezier(u32 op, u32 diff) {
	SkipCurve(op & 0xFF, (op >> 8) & 0xFF);
}

void GPU_Null::Execute_Spline(u32 op, u32 diff) {
	SkipCurve(op & 0xFF, (op >> 8) & 0xFF);
}

void GPU_Null::Execute_BlockTransferStart(u32 op, u32 diff) {
	DoBlockTransfer(gstate_c.skipDrawReason);
}

bool GPU_Null::PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags) {
	if (!(flags & GPUCopyFlag::DEBUG_NOTIFIED))
		recorder_.NotifyMemcpy(dest, src, size);
	return false;
}

bool GPU_Null::PerformMemorySet(u32 dest, u8 v, int size) {
	recorder_.NotifyMemset(dest, v, size);
	return false;
}

}  // namespace

GPUCommon *CreateNullGPU(GraphicsContext *gfxCtx) {
#ifdef USE_CRT_DBG
#undef new
#endif
	return new GPU_Null(gfxCtx);
#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif
}
