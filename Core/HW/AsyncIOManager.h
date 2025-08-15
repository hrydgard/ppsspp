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

#pragma once

#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>

#include "Core/Core.h"

#include "Core/System.h"
#include "Core/CoreTiming.h"

enum AsyncIOEventType {
	IO_EVENT_INVALID,
	IO_EVENT_SYNC,
	IO_EVENT_FINISH,
	IO_EVENT_READ,
	IO_EVENT_WRITE,
};

struct AsyncIOEvent {
	AsyncIOEvent(AsyncIOEventType t) : type(t) {}
	AsyncIOEventType type;
	u32 handle;
	u8 *buf;
	size_t bytes;
	u32 invalidateAddr;

	operator AsyncIOEventType() const {
		return type;
	}
};

struct AsyncIOResult {
	AsyncIOResult() : result(0), finishTicks(0), invalidateAddr(0) {}

	explicit AsyncIOResult(s64 r) : result(r), finishTicks(0), invalidateAddr(0) {}

	AsyncIOResult(s64 r, int usec, u32 addr = 0) : result(r), invalidateAddr(addr) {
		finishTicks = CoreTiming::GetTicks() + usToCycles(usec);
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("AsyncIOResult", 1, 2);
		if (!s)
			return;

		Do(p, result);
		Do(p, finishTicks);
		if (s >= 2) {
			Do(p, invalidateAddr);
		} else {
			invalidateAddr = 0;
		}
	}

	s64 result;
	u64 finishTicks;
	u32 invalidateAddr;
};

class AsyncIOManager {
public:
	void DoState(PointerWrap &p);

	bool HasOperation(u32 handle);
	void ScheduleOperation(const AsyncIOEvent &ev);
	void Shutdown();

	bool HasResult(u32 handle);
	bool WaitResult(u32 handle, AsyncIOResult &result);
	u64 ResultFinishTicks(u32 handle);

	void SetThreadEnabled(bool threadEnabled) {
		threadEnabled_ = threadEnabled;
	}

	bool ThreadEnabled() {
		return threadEnabled_;
	}

	void ScheduleEvent(AsyncIOEvent ev) {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			events_.push_back(ev);
			eventsWait_.notify_one();
		} else {
			events_.push_back(ev);
		}

		if (!threadEnabled_) {
			RunEventsUntil(0);
		}
	}

	bool HasEvents() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			return !events_.empty();
		} else {
			return !events_.empty();
		}
	}

	void NotifyDrain() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			eventsDrain_.notify_one();
		}
	}

	AsyncIOEvent GetNextEvent() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			if (events_.empty()) {
				NotifyDrain();
				return IO_EVENT_INVALID;
			}

			AsyncIOEvent ev = events_.front();
			events_.pop_front();
			return ev;
		} else {
			if (events_.empty()) {
				return IO_EVENT_INVALID;
			}
			AsyncIOEvent ev = events_.front();
			events_.pop_front();
			return ev;
		}
	}

	// This is the threadfunc, really. Although it can also run on the main thread if threadEnabled_ is set.
	// TODO: Remove threadEnabled_, always be on a thread.
	void RunEventsUntil(u64 globalticks) {
		if (!threadEnabled_) {
			do {
				for (AsyncIOEvent ev = GetNextEvent(); AsyncIOEventType(ev) != IO_EVENT_INVALID; ev = GetNextEvent()) {
					ProcessEventIfApplicable(ev, globalticks);
				}
			} while (CoreTiming::GetTicks() < globalticks);
			return;
		}

		std::unique_lock<std::recursive_mutex> guard(eventsLock_);
		eventsRunning_ = true;
		eventsHaveRun_ = true;
		do {
			while (events_.empty()) {
				eventsWait_.wait(guard);
			}
			// Quit the loop if the queue is drained and coreState has tripped, or threading is disabled.
			if (events_.empty()) {
				break;
			}

			for (AsyncIOEvent ev = GetNextEvent(); AsyncIOEventType(ev) != IO_EVENT_INVALID; ev = GetNextEvent()) {
				guard.unlock();
				ProcessEventIfApplicable(ev, globalticks);
				guard.lock();
			}
		} while (CoreTiming::GetTicks() < globalticks);

		// This will force the waiter to check coreState, even if we didn't actually drain.
		NotifyDrain();
		eventsRunning_ = false;
	}

	void SyncBeginFrame() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			eventsHaveRun_ = false;
		} else {
			eventsHaveRun_ = false;
		}
	}

	inline bool ShouldSyncThread(bool force) {
		if (!HasEvents())
			return false;
		if (coreState != CORE_RUNNING_CPU && !force)
			return false;

		// Don't run if it's not running, but wait for startup.
		if (!eventsRunning_) {
			if (eventsHaveRun_ || coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN) {
				return false;
			}
		}

		return true;
	}

	// Force ignores coreState.
	void SyncThread(bool force = false) {
		if (!threadEnabled_) {
			return;
		}

		std::unique_lock<std::recursive_mutex> guard(eventsLock_);
		// While processing the last event, HasEvents() will be false even while not done.
		// So we schedule a nothing event and wait for that to finish.
		ScheduleEvent(IO_EVENT_SYNC);
		while (ShouldSyncThread(force)) {
			eventsDrain_.wait(guard);
		}
	}

	void FinishEventLoop() {
		if (!threadEnabled_) {
			return;
		}

		std::lock_guard<std::recursive_mutex> guard(eventsLock_);
		// Don't schedule a finish if it's not even running.
		if (eventsRunning_) {
			ScheduleEvent(IO_EVENT_FINISH);
		}
	}

protected:
	void ProcessEvent(AsyncIOEvent ref);
	
	inline void ProcessEventIfApplicable(AsyncIOEvent &ev, u64 &globalticks) {
		switch (AsyncIOEventType(ev)) {
		case IO_EVENT_FINISH:
			// Stop waiting.
			globalticks = 0;
			break;

		case IO_EVENT_SYNC:
			// Nothing special to do, this event it just to wait on, see SyncThread.
			break;

		default:
			ProcessEvent(ev);
		}
	}

private:
	bool PopResult(u32 handle, AsyncIOResult &result);
	bool ReadResult(u32 handle, AsyncIOResult &result);
	void Read(u32 handle, u8 *buf, size_t bytes, u32 invalidateAddr);
	void Write(u32 handle, const u8 *buf, size_t bytes);

	void EventResult(u32 handle, const AsyncIOResult &result);

	bool threadEnabled_ = false;
	bool eventsRunning_ = false;
	bool eventsHaveRun_ = false;
	std::deque<AsyncIOEvent> events_;
	std::recursive_mutex eventsLock_;  // TODO: Should really make this non-recursive - condition_variable_any is dangerous
	std::condition_variable_any eventsWait_;
	std::condition_variable_any eventsDrain_;

	std::mutex resultsLock_;
	std::condition_variable resultsWait_;
	std::set<u32> resultsPending_;
	std::map<u32, AsyncIOResult> results_;
};
