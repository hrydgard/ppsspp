#pragma once

#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "Core/ThreadEventQueue.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"

#if defined(ANDROID)
#include <atomic>
#elif defined(_M_SSE)
#include <xmmintrin.h>
#endif

typedef ThreadEventQueue<GPUInterface, GPUEvent, GPUEventType, GPU_EVENT_INVALID, GPU_EVENT_SYNC_THREAD, GPU_EVENT_FINISH_EVENT_LOOP> GPUThreadEventQueue;

class GPUCommon : public GPUThreadEventQueue, public GPUDebugInterface {
public:
	GPUCommon();
	virtual ~GPUCommon();

	void Reinitialize() override;

	void BeginHostFrame() override;
	void EndHostFrame() override;

	void InterruptStart(int listid) override;
	void InterruptEnd(int listid) override;
	void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) override;
	void EnableInterrupts(bool enable) override {
		interruptsEnabled_ = enable;
	}

	void ExecuteOp(u32 op, u32 diff) override;
	void PreExecuteOp(u32 op, u32 diff) override;
	bool InterpretList(DisplayList &list) override;
	virtual bool ProcessDLQueue();
	u32  UpdateStall(int listid, u32 newstall) override;
	u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) override;
	u32  DequeueList(int listid) override;
	int  ListSync(int listid, int mode) override;
	u32  DrawSync(int mode) override;
	int  GetStack(int index, u32 stackPtr) override;
	void DoState(PointerWrap &p) override;
	bool FramebufferDirty() override {
		SyncThread();
		return true;
	}
	bool FramebufferReallyDirty() override {
		SyncThread();
		return true;
	}
	bool BusyDrawing() override;
	u32  Continue() override;
	u32  Break(int mode) override;
	void ReapplyGfxState() override;

	void Execute_OffsetAddr(u32 op, u32 diff);
	void Execute_Origin(u32 op, u32 diff);
	void Execute_Jump(u32 op, u32 diff);
	void Execute_BJump(u32 op, u32 diff);
	void Execute_Call(u32 op, u32 diff);
	void Execute_Ret(u32 op, u32 diff);
	void Execute_End(u32 op, u32 diff);

	u64 GetTickEstimate() override {
#if defined(_M_X64) || defined(ANDROID)
		return curTickEst_;
#elif defined(_M_SSE)
		__m64 result = *(__m64 *)&curTickEst_;
		u64 safeResult = *(u64 *)&result;
		_mm_empty();
		return safeResult;
#else
		lock_guard guard(curTickEstLock_);
		return curTickEst_;
#endif
	}

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

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override {
		return false;
	}

	// From GPUDebugInterface.
	bool GetCurrentDisplayList(DisplayList &list) override;
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
	virtual bool DecodeTexture(u8* dest, const GPUgstate &state) override {
		return false;
	}
	std::vector<FramebufferInfo> GetFramebufferList() override {
		return std::vector<FramebufferInfo>();
	}
	void ClearShaderCache() override {}
	void CleanupBeforeUI() override {}

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override { return std::vector<std::string>(); };
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override {
		return "N/A";
	}

protected:
	// To avoid virtual calls to PreExecuteOp().
	virtual void FastRunLoop(DisplayList &list) = 0;
	void SlowRunLoop(DisplayList &list);
	void UpdatePC(u32 currentPC, u32 newPC);
	void UpdatePC(u32 currentPC) {
		UpdatePC(currentPC, currentPC);
	}
	void UpdateState(GPURunState state);
	void PopDLQueue();
	void CheckDrawSync();
	int  GetNextListIndex();
	void ProcessDLQueueInternal();
	virtual void ReapplyGfxStateInternal();
	virtual void FastLoadBoneMatrix(u32 target);
	void ProcessEvent(GPUEvent ev) override;
	bool ShouldExitEventLoop() override {
		return coreState != CORE_RUNNING;
	}
	virtual void FinishDeferred() {
	}

	void AdvanceVerts(u32 vertType, int count, int bytesRead);

	// Allows early unlocking with a guard.  Do not double unlock.
	class easy_guard {
	public:
		easy_guard(recursive_mutex &mtx) : mtx_(mtx), locked_(true) { mtx_.lock(); }
		~easy_guard() { if (locked_) mtx_.unlock(); }
		void unlock() { if (locked_) mtx_.unlock(); else Crash(); locked_ = false; }

	private:
		recursive_mutex &mtx_;
		bool locked_;
	};

	typedef std::list<int> DisplayListQueue;

	int nextListID;
	DisplayList dls[DisplayListMaxCount];
	DisplayList *currentList;
	DisplayListQueue dlQueue;
	recursive_mutex listLock;

	bool interruptRunning;
	GPURunState gpuState;
	bool isbreak;
	u64 drawCompleteTicks;
	u64 busyTicks;

	int downcount;
	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool dumpNextFrame_;
	bool dumpThisFrame_;
	bool interruptsEnabled_;

private:
	// For CPU/GPU sync.
#ifdef ANDROID
	std::atomic<u64> curTickEst_;
#else
	volatile MEMORY_ALIGNED16(u64) curTickEst_;
	recursive_mutex curTickEstLock_;
#endif

	inline void UpdateTickEstimate(u64 value) {
#if defined(_M_X64) || defined(ANDROID)
		curTickEst_ = value;
#elif defined(_M_SSE)
		__m64 result = *(__m64 *)&value;
		*(__m64 *)&curTickEst_ = result;
		_mm_empty();
#else
		lock_guard guard(curTickEstLock_);
		curTickEst_ = value;
#endif
	}

	// Debug stats.
	double timeSteppingStarted_;
	double timeSpentStepping_;
};
