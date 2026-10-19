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

#include <algorithm>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Formatted events from the CPU thread, drained by one connection thread.
// A hot log-only breakpoint can outpace the consumer. Only breakpoint hits are
// discardable; losing a game or stepping transition requires closing the connection.
class DebuggerEventQueue {
public:
	using Event = std::pair<const char *, std::string>;
	static constexpr size_t MAX_PENDING_EVENTS = 4096;

	// Category strings must outlive the queued events (the producers use literals).
	void Push(const char *category, std::string json) {
		std::lock_guard<std::mutex> guard(lock_);
		if (overflow_) {
			return;
		}
		if (pending_.size() >= MAX_PENDING_EVENTS) {
			if (std::strcmp(category, "breakpoint") == 0) {
				return;
			}
			// Retain the earlier hits and keep all surviving events in production order.
			auto breakpoint = std::find_if(pending_.rbegin(), pending_.rend(), [](const Event &event) {
				return std::strcmp(event.first, "breakpoint") == 0;
			});
			if (breakpoint == pending_.rend()) {
				overflow_ = true;
				return;
			}
			pending_.erase(std::prev(breakpoint.base()));
		}
		pending_.emplace_back(category, std::move(json));
	}

	// False means a control event was lost. The connection thread must close the
	// socket; this remains false even after draining or receiving further events.
	bool Take(std::vector<Event> *out) {
		std::lock_guard<std::mutex> guard(lock_);
		out->clear();
		if (overflow_) {
			pending_.clear();
			return false;
		}
		out->swap(pending_);
		return true;
	}

private:
	std::mutex lock_;
	std::vector<Event> pending_;
	bool overflow_ = false;
};
