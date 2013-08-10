#pragma once

#include "native/base/mutex.h"
#include "GPU/GPUInterface.h"
#include "Core/CoreTiming.h"
#include <deque>

template <typename B, typename Event, typename EventType, EventType EVENT_INVALID, EventType EVENT_SYNC, EventType EVENT_FINISH>
struct ThreadEventQueue : public B {
	void SetThreadEnabled(bool threadEnabled) {
		threadEnabled_ = threadEnabled;
	}

	void ScheduleEvent(Event ev) {
		{
			lock_guard guard(eventsLock_);
			events_.push_back(ev);
			eventsWait_.notify_one();
		}

		if (!threadEnabled_) {
			RunEventsUntil(0);
		}
	}

	bool HasEvents() {
		lock_guard guard(eventsLock_);
		return !events_.empty();
	}

	Event GetNextEvent() {
		lock_guard guard(eventsLock_);
		if (events_.empty()) {
			eventsDrain_.notify_one();
			return EVENT_INVALID;
		}

		Event ev = events_.front();
		events_.pop_front();
		return ev;
	}

	void RunEventsUntil(u64 globalticks) {
		do {
			for (Event ev = GetNextEvent(); EventType(ev) != EVENT_INVALID; ev = GetNextEvent()) {
				switch (EventType(ev)) {
				case EVENT_FINISH:
					// Stop waiting.
					globalticks = 0;
					break;

				case EVENT_SYNC:
					break;

				default:
					ProcessEvent(ev);
				}
			}

			// Quit the loop if the queue is drained and coreState has tripped, or threading is disabled.
			if (coreState != CORE_RUNNING || !threadEnabled_) {
				return;
			}

			// coreState changes won't wake us, so recheck periodically.
			eventsWait_.wait_for(eventsWaitLock_, 1);
		} while (CoreTiming::GetTicks() < globalticks);
	}

	void SyncThread() {
		if (!threadEnabled_) {
			return;
		}

		// While processing the last event, HasEvents() will be false even while not done.
		// So we schedule a nothing event and wait for that to finish.
		ScheduleEvent(EVENT_SYNC);
		while (HasEvents() && coreState == CORE_RUNNING) {
			eventsDrain_.wait_for(eventsDrainLock_, 1);
		}
	}

	void FinishEventLoop() {
		if (threadEnabled_) {
			ScheduleEvent(EVENT_FINISH);
		}
	}

protected:
	virtual void ProcessEvent(Event ev) = 0;

private:
	bool threadEnabled_;
	std::deque<Event> events_;
	recursive_mutex eventsLock_;
	recursive_mutex eventsWaitLock_;
	recursive_mutex eventsDrainLock_;
	condition_variable eventsWait_;
	condition_variable eventsDrain_;
};
typedef ThreadEventQueue<GPUInterface, GPUEvent, GPUEventType, GPU_EVENT_INVALID, GPU_EVENT_SYNC_THREAD, GPU_EVENT_FINISH_EVENT_LOOP> GPUThreadEventQueue;

class GPUCommon : public GPUThreadEventQueue
{
public:
	GPUCommon();
	virtual ~GPUCommon() {}

	virtual void InterruptStart(int listid);
	virtual void InterruptEnd(int listid);
	virtual void SyncEnd(WaitType waitType, int listid, bool wokeThreads);
	virtual void EnableInterrupts(bool enable) {
		interruptsEnabled_ = enable;
	}

	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual bool InterpretList(DisplayList &list);
	virtual bool ProcessDLQueue();
	virtual u32  UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head);
	virtual u32  DequeueList(int listid);
	virtual int  ListSync(int listid, int mode);
	virtual u32  DrawSync(int mode);
	virtual void DoState(PointerWrap &p);
	virtual bool FramebufferDirty() { return true; }
	virtual u32  Continue();
	virtual u32  Break(int mode);
	virtual void ReapplyGfxState();

protected:
	// To avoid virtual calls to PreExecuteOp().
	virtual void FastRunLoop(DisplayList &list) = 0;
	void SlowRunLoop(DisplayList &list);
	void UpdatePC(u32 currentPC, u32 newPC = 0);
	void UpdateState(GPUState state);
	void PopDLQueue();
	void CheckDrawSync();
	int  GetNextListIndex();
	void ProcessDLQueueInternal();
	void ReapplyGfxStateInternal();
	virtual void ProcessEvent(GPUEvent ev);

	// Allows early unlocking with a guard.  Do not double unlock.
	class easy_guard {
	public:
		easy_guard(recursive_mutex &mtx) : mtx_(mtx), locked_(true) { mtx_.lock(); }
		~easy_guard() { if (locked_) mtx_.unlock(); }
		void unlock() { if (locked_) mtx_.unlock(); else Crash(); locked_ = false; }

	private:
		bool locked_;
		recursive_mutex &mtx_;
	};

	typedef std::list<int> DisplayListQueue;

	DisplayList dls[DisplayListMaxCount];
	DisplayList *currentList;
	DisplayListQueue dlQueue;
	recursive_mutex listLock;

	std::deque<GPUEvent> events;
	recursive_mutex eventsLock;
	recursive_mutex eventsWaitLock;
	recursive_mutex eventsDrainLock;
	condition_variable eventsWait;
	condition_variable eventsDrain;

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

public:
	virtual DisplayList* getList(int listid)
	{
		return &dls[listid];
	}

	const std::list<int>& GetDisplayLists()
	{
		return dlQueue;
	}
	DisplayList* GetCurrentDisplayList()
	{
		return currentList;
	}
	virtual bool DecodeTexture(u8* dest, GPUgstate state)
	{
		return false;
	}
	std::vector<FramebufferInfo> GetFramebufferList()
	{
		return std::vector<FramebufferInfo>();
	}

};
