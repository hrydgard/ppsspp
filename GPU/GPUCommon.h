#pragma once

#include "ppsspp_config.h"
#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"

#if defined(__ANDROID__)
#include <atomic>
#endif

class FramebufferManagerCommon;
class TextureCacheCommon;
class DrawEngineCommon;
class GraphicsContext;
struct VirtualFramebuffer;

namespace Draw {
	class DrawContext;
}

enum DrawType {
	DRAW_UNKNOWN,
	DRAW_PRIM,
	DRAW_SPLINE,
	DRAW_BEZIER,
};

enum {
	FLAG_FLUSHBEFOREONCHANGE = 2,
	FLAG_EXECUTE = 4,
	FLAG_EXECUTEONCHANGE = 8,
	FLAG_READS_PC = 16,
	FLAG_WRITES_PC = 32,
	FLAG_DIRTYONCHANGE = 64,  // NOTE: Either this or FLAG_EXECUTE*, not both!
};

struct TransformedVertex {
	union {
		struct {
			float x, y, z, pos_w;     // in case of morph, preblend during decode
		};
		float pos[4];
	};
	union {
		struct {
			float u; float v; float uv_w;   // scaled by uscale, vscale, if there
		};
		float uv[3];
	};
	float fog;
	union {
		u8 color0[4];   // prelit
		u32 color0_32;
	};
	union {
		u8 color1[4];   // prelit
		u32 color1_32;
	};

	void CopyFromWithOffset(const TransformedVertex &other, float xoff, float yoff) {
		this->x = other.x + xoff;
		this->y = other.y + yoff;
		memcpy(&this->z, &other.z, sizeof(*this) - sizeof(float) * 2);
	}
};

inline bool IsTrianglePrim(GEPrimitiveType prim) {
	// TODO: KEEP_PREVIOUS is mistakenly treated as TRIANGLE here... This isn't new.
	//
	// Interesting optimization, but not confident in performance:
	// static const bool p[8] = { false, false, false, true, true, true, false, true };
	// 10111000 = 0xB8;
	// return (0xB8U >> (u8)prim) & 1;

	return prim > GE_PRIM_LINE_STRIP && prim != GE_PRIM_RECTANGLES;
}


class GPUCommon : public GPUInterface, public GPUDebugInterface {
public:
	GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw);

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
	virtual u32 CheckGPUFeatures() const = 0;

	virtual void UpdateCmdInfo() = 0;

	bool IsStarted() override {
		return true;
	}
	void Reinitialize() override;

	void BeginHostFrame() override;
	void EndHostFrame() override;

	void InterruptStart(int listid) override;
	void InterruptEnd(int listid) override;
	void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) override;
	void EnableInterrupts(bool enable) override {
		interruptsEnabled_ = enable;
	}

	void NotifyDisplayResized() override;
	void NotifyRenderResized() override;
	void NotifyConfigChanged() override;

	void DumpNextFrame() override;

	virtual void PreExecuteOp(u32 op, u32 diff) {}

	bool InterpretList(DisplayList &list);
	void ProcessDLQueue();
	u32  UpdateStall(int listid, u32 newstall) override;
	u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) override;
	u32  DequeueList(int listid) override;
	int  ListSync(int listid, int mode) override;
	u32  DrawSync(int mode) override;
	int  GetStack(int index, u32 stackPtr) override;
	bool GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits) override;
	void ResetMatrices() override;
	void DoState(PointerWrap &p) override;
	bool BusyDrawing() override;
	u32  Continue() override;
	u32  Break(int mode) override;
	void ReapplyGfxState() override;
	uint32_t SetAddrTranslation(uint32_t value) override;
	uint32_t GetAddrTranslation() override;

	void CopyDisplayToOutput(bool reallyDirty) override = 0;
	bool PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags = GPUCopyFlag::NONE) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformReadbackToMemory(u32 dest, int size) override;
	bool PerformWriteColorFromMemory(u32 dest, int size) override;

	void PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat format) override;
	bool PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags) override;

	void Execute_OffsetAddr(u32 op, u32 diff);
	void Execute_Vaddr(u32 op, u32 diff);
	void Execute_Iaddr(u32 op, u32 diff);
	void Execute_Origin(u32 op, u32 diff);
	void Execute_Jump(u32 op, u32 diff);
	void Execute_BJump(u32 op, u32 diff);
	void Execute_Call(u32 op, u32 diff);
	void Execute_Ret(u32 op, u32 diff);
	void Execute_End(u32 op, u32 diff);

	void Execute_BoundingBox(u32 op, u32 diff);

	void Execute_MorphWeight(u32 op, u32 diff);

	void Execute_ImmVertexAlphaPrim(u32 op, u32 diff);

	void Execute_Unknown(u32 op, u32 diff);

	static int EstimatePerVertexCost();

	// Note: Not virtual!
	void Flush();
	void DispatchFlush() override;

#ifdef USE_CRT_DBG
#undef new
#endif
	void *operator new(size_t s) {
		return AllocateAlignedMemory(s, 16);
	}
	void operator delete(void *p) {
		FreeAlignedMemory(p);
	}
#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

	// From GPUDebugInterface.
	bool GetCurrentDisplayList(DisplayList &list) override;
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override { return std::vector<std::string>(); };
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override {
		return "N/A";
	}
	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	std::vector<DisplayList> ActiveDisplayLists() override;
	void ResetListPC(int listID, u32 pc) override;
	void ResetListStall(int listID, u32 stall) override;
	void ResetListState(int listID, DisplayListState state) override;

	GPUDebugOp DissassembleOp(u32 pc, u32 op) override;
	std::vector<GPUDebugOp> DissassembleOpRange(u32 startpc, u32 endpc) override;

	void NotifySteppingEnter() override;
	void NotifySteppingExit() override;

	u32 GetRelativeAddress(u32 data) override;
	u32 GetVertexAddress() override;
	u32 GetIndexAddress() override;
	GPUgstate GetGState() override;
	void SetCmdValue(u32 op) override;

	DisplayList* getList(int listid) override {
		return &dls[listid];
	}

	const std::list<int>& GetDisplayLists() override {
		return dlQueue;
	}

	s64 GetListTicks(int listid) const override {
		if (listid >= 0 && listid < DisplayListMaxCount) {
			return dls[listid].waitTicks;
		}
		return -1;
	}

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}

	bool PresentedThisFrame() const override;

protected:
	void ClearCacheNextFrame() override {}

	virtual void CheckRenderResized() {}

	void SetDrawType(DrawType type, GEPrimitiveType prim) {
		if (type != lastDraw_) {
			// We always flush when drawing splines/beziers so no need to do so here
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
			lastDraw_ = type;
		}
		// Prim == RECTANGLES can cause CanUseHardwareTransform to flip, so we need to dirty.
		// Also, culling may be affected so dirty the raster state.
		if (IsTrianglePrim(prim) != IsTrianglePrim(lastPrim_)) {
			Flush();
			gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
			lastPrim_ = prim;
		}
	}

	void PSPFrame() override;

	virtual void CheckDepthUsage(VirtualFramebuffer *vfb) {}
	virtual void FastRunLoop(DisplayList &list) = 0;

	void SlowRunLoop(DisplayList &list);
	void UpdatePC(u32 currentPC, u32 newPC);
	void UpdateState(GPURunState state);
	void FastLoadBoneMatrix(u32 target);
	void FlushImm();
	void DoBlockTransfer(u32 skipDrawReason);

	// TODO: Unify this. Vulkan and OpenGL are different due to how they buffer data.
	virtual void FinishDeferred() {}

	void AdvanceVerts(u32 vertType, int count, int bytesRead) {
		if ((vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
			int indexShift = ((vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
			gstate_c.indexAddr += count << indexShift;
		} else {
			gstate_c.vertexAddr += bytesRead;
		}
	}

	virtual void BuildReportingInfo() = 0;

	virtual void UpdateMSAALevel(Draw::DrawContext *draw) {}

	DrawEngineCommon *drawEngineCommon_ = nullptr;

	// TODO: These should live in GPUCommonHW.
	FramebufferManagerCommon *framebufferManager_ = nullptr;
	TextureCacheCommon *textureCache_ = nullptr;

	bool flushOnParams_ = true;

	GraphicsContext *gfxCtx_;
	Draw::DrawContext *draw_;

	typedef std::list<int> DisplayListQueue;

	int nextListID;
	DisplayList dls[DisplayListMaxCount];
	DisplayList *currentList;
	DisplayListQueue dlQueue;

	bool interruptRunning = false;
	GPURunState gpuState = GPUSTATE_RUNNING;
	bool isbreak;
	u64 drawCompleteTicks;
	u64 busyTicks;

	int downcount;
	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool dumpNextFrame_ = false;
	bool dumpThisFrame_ = false;
	bool debugRecording_;
	bool interruptsEnabled_;
	bool displayResized_ = false;
	bool renderResized_ = false;
	bool configChanged_ = false;
	DrawType lastDraw_ = DRAW_UNKNOWN;
	GEPrimitiveType lastPrim_ = GE_PRIM_INVALID;

	int vertexCost_ = 0;

	// No idea how big this buffer needs to be.
	enum {
		MAX_IMMBUFFER_SIZE = 32,
	};

	TransformedVertex immBuffer_[MAX_IMMBUFFER_SIZE];
	int immCount_ = 0;
	GEPrimitiveType immPrim_ = GE_PRIM_INVALID;
	uint32_t immFlags_ = 0;
	bool immFirstSent_ = false;

	uint32_t edramTranslation_ = 0x400;

	// When matrix data overflows, the CPU visible values wrap and bleed between matrices.
	// But this doesn't actually change the values used by rendering.
	// The CPU visible values affect the GPU when list contexts are restored.
	// Note: not maintained by all backends, here for save stating.
	union {
		struct {
			u32 bone[12 * 8];
			u32 world[12];
			u32 view[12];
			u32 proj[16];
			u32 tgen[12];
		};
		u32 all[12 * 8 + 12 + 12 + 16 + 12];
	} matrixVisible;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;

private:
	void DoExecuteCall(u32 target);
	void PopDLQueue();
	void CheckDrawSync();
	int  GetNextListIndex();

	// Debug stats.
	double timeSteppingStarted_;
	double timeSpentStepping_;
};
