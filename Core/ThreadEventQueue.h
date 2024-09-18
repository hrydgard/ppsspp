// Copyright (c) 2013- PPSSPP Project.

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

#include "Core/System.h"
#include "Core/CoreTiming.h"

template <typename B, typename Event, typename EventType, EventType EVENT_INVALID, EventType EVENT_SYNC, EventType EVENT_FINISH>
struct ThreadEventQueue : public B {
	ThreadEventQueue() : threadEnabled_(false), eventsRunning_(false), eventsHaveRun_(false) {
	}
	virtual ~ThreadEventQueue() {
	}

	void SetThreadEnabled(bool threadEnabled) {
		threadEnabled_ = threadEnabled;
	}

	bool ThreadEnabled() {
		return threadEnabled_;
	}

	void ScheduleEvent(Event ev) {
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

	Event GetNextEvent() {
		if (threadEnabled_) {
			std::lock_guard<std::recursive_mutex> guard(eventsLock_);
			if (events_.empty()) {
				NotifyDrain();
				return EVENT_INVALID;
			}

			Event ev = events_.front();
			events_.pop_front();
			return ev;
		} else {
			if (events_.empty()) {
				return EVENT_INVALID;
			}
			Event ev = events_.front();
			events_.pop_front();
			return ev;
		}
	}

	void RunEventsUntil(u64 globalticks) {
		if (!threadEnabled_) {
			do {
				for (Event ev = GetNextEvent(); EventType(ev) != EVENT_INVALID; ev = GetNextEvent()) {
					ProcessEventIfApplicable(ev, globalticks);
				}
			} while (CoreTiming::GetTicks() < globalticks);
			return;
		}

		std::unique_lock<std::recursive_mutex> guard(eventsLock_);
		eventsRunning_ = true;
		eventsHaveRun_ = true;
		do {
			while (events_.empty() && !ShouldExitEventLoop()) {
				eventsWait_.wait(guard);
			}
			// Quit the loop if the queue is drained and coreState has tripped, or threading is disabled.
			if (events_.empty()) {
				break;
			}

			for (Event ev = GetNextEvent(); EventType(ev) != EVENT_INVALID; ev = GetNextEvent()) {
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
		if (coreState != CORE_RUNNING && !force)
			return false;

		// Don't run if it's not running, but wait for startup.
		if (!eventsRunning_) {
			if (eventsHaveRun_ || coreState == CORE_BOOT_ERROR || coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN) {
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
		ScheduleEvent(EVENT_SYNC);
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
			ScheduleEvent(EVENT_FINISH);
		}
	}

protected:
	virtual void ProcessEvent(Event ev) = 0;
	virtual bool ShouldExitEventLoop() = 0;

	inline void ProcessEventIfApplicable(Event &ev, u64 &globalticks) {
		switch (EventType(ev)) {
		case EVENT_FINISH:
			// Stop waiting.
			globalticks = 0;
			break;

		case EVENT_SYNC:
			// Nothing special to do, this event it just to wait on, see SyncThread.
			break;

		default:
			ProcessEvent(ev);
		}
	}

private:
	bool threadEnabled_;
	bool eventsRunning_;
	bool eventsHaveRun_;
	std::deque<Event> events_;
	std::recursive_mutex eventsLock_;  // TODO: Should really make this non-recursive - condition_variable_any is dangerous
	std::condition_variable_any eventsWait_;
	std::condition_variable_any eventsDrain_;
};
