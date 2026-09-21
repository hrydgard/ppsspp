#pragma once

#include <vector>
#include <list>

#include "ppsspp_config.h"
#include "Common/Common.h"
#include "Common/Swap.h"
#include "Core/MemMap.h"
#include "Common/MemoryUtil.h"
#include "GPU/GPU.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/GPUDefinitions.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/ge_constants.h"

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
struct GEState;
class PointerWrap;
struct VirtualFramebuffer;

namespace Draw {
class DrawContext;
}

class StringWriter;
struct DisplayLayoutConfig;

inline bool IsTrianglePrim(GEPrimitiveType prim) {
	// TODO: KEEP_PREVIOUS is mistakenly treated as TRIANGLE here... This isn't new.
	//
	// Interesting optimization, but not confident in performance:
	// static const bool p[8] = { false, false, false, true, true, true, false, true };
	// 10111000 = 0xB8;
	// return (0xB8U >> (u8)prim) & 1;

	return prim > GE_PRIM_LINE_STRIP && prim != GE_PRIM_RECTANGLES;
}

struct TransformStats;

// The frame of a game that an application of the emulator (a plugin injected into it) is handed
// where it draws in between the world and the UI of the frame, see GPUCommon::SetBeforeUIDrawDraw.
// A plain struct on purpose: an application gets it without any header of the emulator. The two
// pointers are the Draw::DrawContext and the Draw::Framebuffer of Common/GPU/thin3d.h.
struct PPSSPPBeforeUIDrawTarget {
	void *draw = nullptr;
	void *frame = nullptr;
	int width = 0;                 // size of that framebuffer, in pixels
	int height = 0;
	// The size the frame is shown at in the window. A frame that is not shown in its own proportion
	// is stretched, and a drawing that has to stay round needs to know that.
	int shownWidth = 0;
	int shownHeight = 0;
	// The memory of the game, and the address it reported this frame point from, see
	// PPSSPP_DEVCTL__BEFORE_UI_DRAW. The only way to reach that memory from here: asking the
	// window of the emulator means asking its UI thread, which is waiting for this very frame.
	void *memory = nullptr;
	uint32_t reportAddress = 0;
	// Everything an application makes with the drawing is reference counted and only the emulator
	// can release one: what it made is given back through this.
	void (*release)(void *object) = nullptr;
};

typedef void (*PPSSPPBeforeUIDrawDrawFn)(const PPSSPPBeforeUIDrawTarget *target);

class GPUCommon {
public:
	// The constructor might run on the loader thread.
	GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	virtual ~GPUCommon() = default;

	virtual void GetStats(StringWriter &w) = 0;
	virtual std::vector<const VirtualFramebuffer *> GetFramebufferList() const = 0;
		
	// Needs to be called from the GPU thread, so on the same thread as a notification is fine.
	// Calling from a separate thread (e.g. UI) may fail.
	virtual bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes = -1) {
		// False means unsupported.
		return false;
	}
	virtual bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) { return false; }
	virtual bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) { return false; }
	virtual bool GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) { return false; }
	virtual bool GetCurrentClut(GPUDebugBuffer &buffer) { return false;}
	virtual bool GetOutputFramebuffer(GPUDebugBuffer &buffer) { return false; }

	bool GetCurrentDisplayList(DisplayList &list) const;
	bool GetCurrentDrawAsDebugVertices(GECommand cmd, GEPrimitiveType prim, GEPrimitiveType *outPrim, int count, std::vector<GPUDebugVertex> *vertices, std::vector<u16> *indices, int *lowerIndexBound, TransformStats *stats, DebugVertexFlags flags) const;
	int GetCurrentPrim(GEPrimitiveType *prim, GECommand *outCmd) const;  // Return value has the count.

	// FinishInitOnMainThread runs on the main thread, of course.
	virtual void FinishInitOnMainThread() {}

	Draw::DrawContext *GetDrawContext() {
		return draw_;
	}

	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;

	virtual u32 CheckGPUFeatures() const = 0;

	virtual void UpdateCmdInfo() = 0;

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

	// The report of a game that is between the world and the UI of its frame, from
	// Core/HLE/sceIo.cpp. listPos is where the commands the game has written into its display list
	// end at that moment, which is the end of the world of the frame. The counter is counted once
	// the GE has run the list up to that point, so when it moves the world is really drawn and the
	// UI is not. A game that cannot say where that point is passes 0 and the counter moves right
	// away.
	void ReportBeforeUIDraw(u32 counterAddr, u32 listPos);

	// Counts the counter of a report that is waiting, if there is one, and clears it.
	void CountBeforeUIDraw();

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

	uint32_t SetAddrTranslation(uint32_t value);
	uint32_t GetAddrTranslation();

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) = 0;
	virtual void SetCurFramebufferDirty(bool dirty) = 0;
	virtual void PrepareCopyDisplayToOutput(const DisplayLayoutConfig &config) = 0;
	virtual void CopyDisplayToOutput(const DisplayLayoutConfig &config) = 0;
	virtual bool PresentedThisFrame() const = 0;

	// Invalidate any cached content sourced from the specified range.
	// If size = -1, invalidate everything.
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type) = 0;

	// These return true if they handled the operation enough that the actual memory operation should be skipped. Not always a clear-cut case...
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

	// The frame of the game being drawn at the moment, for the drawing of an application of the
	// emulator to be handed, see PPSSPPBeforeUIDrawTarget.
	PPSSPPBeforeUIDrawTarget GetBeforeUIDrawTarget();

	// Sets the drawing of an application of the emulator. It is called from the thread the frame is
	// run on, where the game reported its frame, so what it draws is part of that frame and the UI
	// the game sends afterwards lands on top of it. Passing nothing takes it out again.
	static void SetBeforeUIDrawDraw(PPSSPPBeforeUIDrawDrawFn fn);

	virtual void Flush();

	// The texture slot the mark of the frame of a game goes on, see MarkBeforeUIDraw: one a game
	// leaves empty, so a plugin can tell the mark from the drawing of the game.
	static constexpr int BEFORE_UI_MARK_SLOT = 2;

	// Puts a mark into the drawing of the frame of the moment, at the point the game reported. The
	// drawing of a frame is recorded in the order it is submitted whatever the backend is, so the
	// mark runs at the same place of the frame even where the backend records the frame and runs it
	// later. A backend that draws where it is called has nothing to do here.
	virtual void MarkBeforeUIDraw() {}

	// Runs whatever the guest has queued but not run yet, so that everything it drew so far is
	// really drawn. Counting a report of a game needs this, see GPUCommon::CountBeforeUIDraw.
	// TODO: Unify this. Vulkan and OpenGL are different due to how they buffer data.
	virtual void FinishDeferred() {}

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

	virtual FramebufferManagerCommon *GetFramebufferManagerCommon() { return nullptr; }
	virtual TextureCacheCommon *GetTextureCacheCommon() { return nullptr; }
	const DrawEngineCommon *GetDrawEngineCommon() const { return drawEngineCommon_; }

	virtual std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) { return std::vector<std::string>(); };
	virtual std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) {
		return "N/A";
	}

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name);

	std::vector<DisplayList> ActiveDisplayLists() const;
	void ResetListPC(int listID, u32 pc);
	void ResetListStall(int listID, u32 stall);
	void ResetListState(int listID, DisplayListState state);

	GPUDebugOp DisassembleOp(u32 pc, u32 op);
	std::vector<GPUDebugOp> DisassembleOpRange(u32 startpc, u32 endpc);

	u32 GetRelativeAddress(u32 data);
	u32 GetVertexAddress();
	u32 GetIndexAddress();
	const GEState &GetGState();
	void SetCmdValue(u32 op);

	DisplayList* getList(int listid) {
		return &dls[listid];
	}

	const std::list<int> &GetDisplayListQueue() {
		return dlQueue;
	}
	const DisplayList &GetDisplayList(int index) {
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

	GPURecord::Recorder *GetRecorder() {
		return &recorder_;
	}
	GPUBreakpoints *GetBreakpoints() {
		return &breakpoints_;
	}

	void ClearBreakNext();
	void SetBreakNext(GPUDebug::BreakNext next);
	void SetBreakCount(int c, bool relative = false);
	GPUDebug::BreakNext GetBreakNext() const {
		return breakNext_;
	}
	int GetBreakCount() const {
		return breakAtCount_;
	}
	bool SetRestrictPrims(std::string_view rule);
	std::string_view GetRestrictPrims() {
		return restrictPrimRule_;
	}

	int PrimsThisFrame() const {
		return primsThisFrame_;
	}
	int PrimsLastFrame() const {
		return primsLastFrame_;
	}

	void NotifyFlush();

protected:
	// While debugging is active, these may block.
	void NotifyDisplay(u32 framebuf, u32 stride, int format);

	void UpdateMatrixProducts();

	bool NeedsSlowInterpreter() const;
	GPUDebug::NotifyResult NotifyCommand(u32 pc, GPUBreakpoints *breakpoints);

	virtual void ClearCacheNextFrame() {}

	virtual void CheckRenderResized(const DisplayLayoutConfig &config) {}

	void SetDrawType(DrawType type, GEPrimitiveType prim) {
		if (type != lastDraw_) {
			// We always flush when drawing splines/beziers so no need to do so here
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET | DIRTY_VERTEXSHADER_STATE);
			lastDraw_ = type;
		}
		// Prim == RECTANGLES can cause CanUseHardwareTransform to flip, so we need to dirty.
		// Also, culling may be affected so dirty the raster state.
		if (IsTrianglePrim(prim) != IsTrianglePrim(lastPrim_)) {
			Flush();
			gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE);
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
	Draw::DrawContext *draw_ = nullptr;

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
	// The end of the world of the frame the game last said, the counter it reported with, and what has to be
	// put back once the list has been stopped there, see ReportBeforeUIDraw. The point stands until the game
	// says another one: a report that says nothing about it - a game that cannot say where its world ends for
	// that frame - leaves it as it was, so that a frame without it does not come and go in what is drawn there.
	u32 beforeUIDrawAddr_ = 0;
	u32 beforeUIDrawPos_ = 0;
	u32 beforeUIDrawStall_ = 0;
	bool beforeUIDrawSplit_ = false;
	// Whether the report of the frame has had its point, which is one per report even where more than one
	// display list is run, see ReportBeforeUIDraw.
	bool beforeUIDrawFired_ = false;
	// Where the last report came from: handed over as PPSSPPBeforeUIDrawTarget::reportAddress.
	u32 beforeUIDrawReport_ = 0;
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
