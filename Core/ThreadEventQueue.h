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

#include "native/base/mutex.h"
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