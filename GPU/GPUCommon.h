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

class GPUCommon : public GPUThreadEventQueue, public GPUDebugInterface
{
public:
	GPUCommon();
	virtual ~GPUCommon() {}
	virtual void Reinitialize();

	virtual void InterruptStart(int listid);
	virtual void InterruptEnd(int listid);
	virtual void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads);
	virtual void EnableInterrupts(bool enable) {
		interruptsEnabled_ = enable;
	}

	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual bool InterpretList(DisplayList &list);
	virtual bool ProcessDLQueue();
	virtual u32  UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head);
	virtual u32  DequeueList(int listid);
	virtual int  ListSync(int listid, int mode);
	virtual u32  DrawSync(int mode);
	virtual int  GetStack(int index, u32 stackPtr);
	virtual void DoState(PointerWrap &p);
	virtual bool FramebufferDirty() {
		SyncThread();
		return true;
	}
	virtual bool FramebufferReallyDirty() {
		SyncThread();
		return true;
	}
	virtual bool BusyDrawing();
	virtual u32  Continue();
	virtual u32  Break(int mode);
	virtual void ReapplyGfxState();

	void Execute_OffsetAddr(u32 op, u32 diff);
	void Execute_Origin(u32 op, u32 diff);
	void Execute_Jump(u32 op, u32 diff);
	void Execute_BJump(u32 op, u32 diff);
	void Execute_Call(u32 op, u32 diff);
	void Execute_Ret(u32 op, u32 diff);
	void Execute_End(u32 op, u32 diff);

	virtual u64 GetTickEstimate() {
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

	void *operator new(size_t s) {
		return AllocateAlignedMemory(s, 16);
	}
	void operator delete(void *p) {
		FreeAlignedMemory(p);
	}

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name) {
		return false;
	}

	// From GPUDebugInterface.
	virtual bool GetCurrentDisplayList(DisplayList &list);
	virtual std::vector<DisplayList> ActiveDisplayLists();
	virtual void ResetListPC(int listID, u32 pc);
	virtual void ResetListStall(int listID, u32 stall);
	virtual void ResetListState(int listID, DisplayListState state);

	virtual GPUDebugOp DissassembleOp(u32 pc, u32 op);
	virtual std::vector<GPUDebugOp> DissassembleOpRange(u32 startpc, u32 endpc);

	virtual void NotifySteppingEnter();
	virtual void NotifySteppingExit();

	virtual u32 GetRelativeAddress(u32 data);
	virtual u32 GetVertexAddress();
	virtual u32 GetIndexAddress();
	virtual GPUgstate GetGState();
	virtual void SetCmdValue(u32 op);

	virtual DisplayList* getList(int listid) {
		return &dls[listid];
	}

	const std::list<int>& GetDisplayLists() {
		return dlQueue;
	}
	virtual bool DecodeTexture(u8* dest, GPUgstate state) {
		return false;
	}
	std::vector<FramebufferInfo> GetFramebufferList() {
		return std::vector<FramebufferInfo>();
	}
	virtual void ClearShaderCache() {}
	virtual void CleanupBeforeUI() {}

protected:
	// To avoid virtual calls to PreExecuteOp().
	virtual void FastRunLoop(DisplayList &list) = 0;
	void SlowRunLoop(DisplayList &list);
	void UpdatePC(u32 currentPC, u32 newPC);
	void UpdatePC(u32 currentPC) {
		UpdatePC(currentPC, currentPC);
	}
	void UpdateState(GPUState state);
	void PopDLQueue();
	void CheckDrawSync();
	int  GetNextListIndex();
	void ProcessDLQueueInternal();
	void ReapplyGfxStateInternal();
	virtual void FastLoadBoneMatrix(u32 target);
	virtual void ProcessEvent(GPUEvent ev);
	virtual bool ShouldExitEventLoop() {
		return coreState != CORE_RUNNING;
	}

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
	GPUState gpuState;
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
