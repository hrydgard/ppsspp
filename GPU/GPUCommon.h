#pragma once

#include <vector>
#include <list>

#include "ppsspp_config.h"
#include "Common/Common.h"
#include "Common/Swap.h"
#include "Core/MemMap.h"
#include "Common/MemoryUtil.h"
#include "GPU/ge_constants.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUDefinitions.h"

#if defined(__ANDROID__)
#include <atomic>
#endif

// X11, sigh.
#ifdef None
#undef None
#endif

class FramebufferManagerCommon;
class TextureCacheCommon;
class DrawEngineCommon;
class GraphicsContext;
struct PspGeListArgs;
struct GPUgstate;
class PointerWrap;
struct VirtualFramebuffer;

namespace Draw {
class DrawContext;
}

enum SignalBehavior {
	PSP_GE_SIGNAL_NONE = 0x00,
	PSP_GE_SIGNAL_HANDLER_SUSPEND = 0x01,
	PSP_GE_SIGNAL_HANDLER_CONTINUE = 0x02,
	PSP_GE_SIGNAL_HANDLER_PAUSE = 0x03,
	PSP_GE_SIGNAL_SYNC = 0x08,
	PSP_GE_SIGNAL_JUMP = 0x10,
	PSP_GE_SIGNAL_CALL = 0x11,
	PSP_GE_SIGNAL_RET = 0x12,
	PSP_GE_SIGNAL_RJUMP = 0x13,
	PSP_GE_SIGNAL_RCALL = 0x14,
	PSP_GE_SIGNAL_OJUMP = 0x15,
	PSP_GE_SIGNAL_OCALL = 0x16,

	PSP_GE_SIGNAL_RTBP0 = 0x20,
	PSP_GE_SIGNAL_RTBP1 = 0x21,
	PSP_GE_SIGNAL_RTBP2 = 0x22,
	PSP_GE_SIGNAL_RTBP3 = 0x23,
	PSP_GE_SIGNAL_RTBP4 = 0x24,
	PSP_GE_SIGNAL_RTBP5 = 0x25,
	PSP_GE_SIGNAL_RTBP6 = 0x26,
	PSP_GE_SIGNAL_RTBP7 = 0x27,
	PSP_GE_SIGNAL_OTBP0 = 0x28,
	PSP_GE_SIGNAL_OTBP1 = 0x29,
	PSP_GE_SIGNAL_OTBP2 = 0x2A,
	PSP_GE_SIGNAL_OTBP3 = 0x2B,
	PSP_GE_SIGNAL_OTBP4 = 0x2C,
	PSP_GE_SIGNAL_OTBP5 = 0x2D,
	PSP_GE_SIGNAL_OTBP6 = 0x2E,
	PSP_GE_SIGNAL_OTBP7 = 0x2F,
	PSP_GE_SIGNAL_RCBP = 0x30,
	PSP_GE_SIGNAL_OCBP = 0x38,
	PSP_GE_SIGNAL_BREAK1 = 0xF0,
	PSP_GE_SIGNAL_BREAK2 = 0xFF,
};

enum GPURunState {
	GPUSTATE_RUNNING = 0,
	GPUSTATE_DONE = 1,
	GPUSTATE_STALL = 2,
	GPUSTATE_INTERRUPT = 3,
	GPUSTATE_ERROR = 4,
};

enum GPUSyncType {
	GPU_SYNC_DRAW,
	GPU_SYNC_LIST,
};

enum class WriteStencil {
	NEEDS_CLEAR = 1,
	STENCIL_IS_ZERO = 2,
	IGNORE_ALPHA = 4,
};
ENUM_CLASS_BITOPS(WriteStencil);

enum class GPUCopyFlag {
	NONE = 0,
	FORCE_SRC_MATCH_MEM = 1,
	FORCE_DST_MATCH_MEM = 2,
	// Note: implies src == dst and FORCE_SRC_MATCH_MEM.
	MEMSET = 4,
	DEPTH_REQUESTED = 8,
	DEBUG_NOTIFIED = 16,
	DISALLOW_CREATE_VFB = 32,
};
ENUM_CLASS_BITOPS(GPUCopyFlag);

struct DisplayListStackEntry {
	u32 pc;
	u32 offsetAddr;
	u32 baseAddr;
};

struct DisplayList {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitUntilTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	PSPPointer<u32_le> context;
	u32 offsetAddr;
	bool bboxResult;
	u32 stackAddr;

	u32 padding;  // Android x86-32 does not round the structure size up to the closest multiple of 8 like the other platforms.
};

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

struct DisplayLayoutConfig;

class GPUCommon : public GPUDebugInterface {
public:
	// The constructor might run on the loader thread.
	GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw);

	// FinishInitOnMainThread runs on the main thread, of course.
	virtual void FinishInitOnMainThread() {}

	virtual ~GPUCommon() {}

	Draw::DrawContext *GetDrawContext() {
		return draw_;
	}

	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;

	virtual u32 CheckGPUFeatures() const = 0;

	virtual void UpdateCmdInfo() = 0;

	virtual bool IsStarted() {
		return true;
	}
	virtual void Reinitialize();

	virtual void BeginHostFrame(const DisplayLayoutConfig &config);
	virtual void EndHostFrame();

	void InterruptStart(int listid);
	void InterruptEnd(int listid);
	void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads);
	void EnableInterrupts(bool enable) {
		interruptsEnabled_ = enable;
	}

	virtual void CheckDisplayResized() = 0;
	virtual void CheckConfigChanged(const DisplayLayoutConfig &config) = 0;

	virtual void NotifyDisplayResized();
	virtual void NotifyRenderResized(const DisplayLayoutConfig &config);
	virtual void NotifyConfigChanged();

	void DumpNextFrame();

	virtual void PreExecuteOp(u32 op, u32 diff) {}

	DLResult ProcessDLQueue();

	u32 UpdateStall(int listid, u32 newstall, bool *runList);
	u32 EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head, bool *runList);
	u32 DequeueList(int listid);
	virtual int ListSync(int listid, int mode);
	virtual u32 DrawSync(int mode);
	int GetStack(int index, u32 stackPtr);
	virtual bool GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits);
	virtual void ResetMatrices();
	virtual void DoState(PointerWrap &p);
	bool BusyDrawing();
	u32 Continue(bool *runList);
	u32 Break(int mode);

	virtual bool FramebufferDirty() = 0;
	virtual bool FramebufferReallyDirty() = 0;

	virtual void ReapplyGfxState();

	// Returns true if we should split the call across GE execution.
	// For example, a debugger is active.
	bool ShouldSplitOverGe() const;

	uint32_t SetAddrTranslation(uint32_t value) override;
	uint32_t GetAddrTranslation() override;

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) = 0;
	virtual void CopyDisplayToOutput(const DisplayLayoutConfig &config, bool reallyDirty) = 0;
	virtual bool PresentedThisFrame() const = 0;

	// Invalidate any cached content sourced from the specified range.
	// If size = -1, invalidate everything.
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type) = 0;

	virtual bool PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags = GPUCopyFlag::NONE);
	virtual bool PerformMemorySet(u32 dest, u8 v, int size);
	virtual bool PerformReadbackToMemory(u32 dest, int size);
	virtual bool PerformWriteColorFromMemory(u32 dest, int size);

	virtual void PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat format);
	virtual bool PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags);

	virtual void ExecuteOp(u32 op, u32 diff) = 0;

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

	void Flush() override;

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
	bool GetCurrentDrawAsDebugVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;
	int GetCurrentPrimCount() override;
	FramebufferManagerCommon *GetFramebufferManagerCommon() override {
		return nullptr;
	}

	TextureCacheCommon *GetTextureCacheCommon() override {
		return nullptr;
	}

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override { return std::vector<std::string>(); };
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override {
		return "N/A";
	}
	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	std::vector<DisplayList> ActiveDisplayLists() override;
	void ResetListPC(int listID, u32 pc) override;
	void ResetListStall(int listID, u32 stall) override;
	void ResetListState(int listID, DisplayListState state) override;

	GPUDebugOp DisassembleOp(u32 pc, u32 op) override;
	std::vector<GPUDebugOp> DisassembleOpRange(u32 startpc, u32 endpc) override;

	u32 GetRelativeAddress(u32 data) override;
	u32 GetVertexAddress() override;
	u32 GetIndexAddress() override;
	const GPUgstate &GetGState() override;
	void SetCmdValue(u32 op) override;

	DisplayList* getList(int listid) {
		return &dls[listid];
	}

	const std::list<int> &GetDisplayListQueue() override {
		return dlQueue;
	}
	const DisplayList &GetDisplayList(int index) override {
		return dls[index];
	}

	s64 GetListTicks(int listid) const {
		if (listid >= 0 && listid < DisplayListMaxCount) {
			return dls[listid].waitUntilTicks;
		}
		return -1;
	}

	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) const {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}

	void PSPFrame();

	GPURecord::Recorder *GetRecorder() override {
		return &recorder_;
	}
	GPUBreakpoints *GetBreakpoints() override {
		return &breakpoints_;
	}

	void ClearBreakNext() override;
	void SetBreakNext(GPUDebug::BreakNext next) override;
	void SetBreakCount(int c, bool relative = false) override;
	GPUDebug::BreakNext GetBreakNext() const override {
		return breakNext_;
	}
	int GetBreakCount() const override {
		return breakAtCount_;
	}
	bool SetRestrictPrims(std::string_view rule) override;
	std::string_view GetRestrictPrims() override {
		return restrictPrimRule_;
	}

	int PrimsThisFrame() const override {
		return primsThisFrame_;
	}
	int PrimsLastFrame() const override {
		return primsLastFrame_;
	}

	void NotifyFlush();

protected:
	// While debugging is active, these may block.
	void NotifyDisplay(u32 framebuf, u32 stride, int format);

	bool NeedsSlowInterpreter() const;
	GPUDebug::NotifyResult NotifyCommand(u32 pc, GPUBreakpoints *breakpoints);

	virtual void ClearCacheNextFrame() {}

	virtual void CheckRenderResized(const DisplayLayoutConfig &config) {}

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

	virtual void CheckDepthUsage(VirtualFramebuffer *vfb) {}
	virtual void FastRunLoop(DisplayList &list) = 0;

	bool SlowRunLoop(DisplayList &list);  // Returns false on breakpoint.
	void UpdatePC(u32 currentPC, u32 newPC);
	void UpdateState(GPURunState state);
	void FastLoadBoneMatrix(u32 target);
	void FlushImm();
	void DoBlockTransfer(u32 skipDrawReason);

	// TODO: Unify this. Vulkan and OpenGL are different due to how they buffer data.
	virtual void FinishDeferred() {}

	void AdvanceVerts(u32 vertType, int count, int bytesRead) {
		if ((vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
			const int indexShift = ((vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
			gstate_c.indexAddr += count << indexShift;
		} else {
			gstate_c.vertexAddr += bytesRead;
		}
	}

	virtual void BuildReportingInfo() = 0;

	virtual void UpdateMSAALevel(Draw::DrawContext *draw) {}

	enum {
		DisplayListMaxCount = 64
	};

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
	bool isbreak;  // This doesn't mean debugger breakpoints.
	u64 drawCompleteTicks;
	u64 busyTicks;

	int downcount;
	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool resumingFromDebugBreak_ = false;
	bool dumpNextFrame_ = false;
	bool dumpThisFrame_ = false;
	bool useFastRunLoop_ = false;
	bool interruptsEnabled_ = false;
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

	// Debugging state
	bool debugRecording_ = false;

	GPURecord::Recorder recorder_;
	GPUBreakpoints breakpoints_;

	GPUDebug::BreakNext breakNext_ = GPUDebug::BreakNext::NONE;
	int breakAtCount_ = -1;

	int primsLastFrame_ = 0;
	int primsThisFrame_ = 0;
	int thisFlipNum_ = 0;

	bool primAfterDraw_ = false;

	uint32_t skipPcOnce_ = 0;

	std::vector<std::pair<int, int>> restrictPrimRanges_;
	std::string restrictPrimRule_;

private:
	void DoExecuteCall(u32 target);
	void PopDLQueue();
	void CheckDrawSync();
};
