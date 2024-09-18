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

#include <map>
#include <set>
#include <mutex>

#include "Core/ThreadEventQueue.h"

class NoBase {
};

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
	AsyncIOResult() : result(0), finishTicks(0), invalidateAddr(0) {
	}

	explicit AsyncIOResult(s64 r) : result(r), finishTicks(0), invalidateAddr(0) {
	}

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

typedef ThreadEventQueue<NoBase, AsyncIOEvent, AsyncIOEventType, IO_EVENT_INVALID, IO_EVENT_SYNC, IO_EVENT_FINISH> IOThreadEventQueue;
class AsyncIOManager : public IOThreadEventQueue {
public:
	void DoState(PointerWrap &p);

	bool HasOperation(u32 handle);
	void ScheduleOperation(const AsyncIOEvent &ev);
	void Shutdown();

	bool HasResult(u32 handle);
	bool WaitResult(u32 handle, AsyncIOResult &result);
	u64 ResultFinishTicks(u32 handle);

protected:
	void ProcessEvent(AsyncIOEvent ref) override;
	bool ShouldExitEventLoop() override {
		return coreState == CORE_BOOT_ERROR || coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN;
	}

private:
	bool PopResult(u32 handle, AsyncIOResult &result);
	bool ReadResult(u32 handle, AsyncIOResult &result);
	void Read(u32 handle, u8 *buf, size_t bytes, u32 invalidateAddr);
	void Write(u32 handle, const u8 *buf, size_t bytes);

	void EventResult(u32 handle, const AsyncIOResult &result);

	std::mutex resultsLock_;
	std::condition_variable resultsWait_;
	std::set<u32> resultsPending_;
	std::map<u32, AsyncIOResult> results_;
};
