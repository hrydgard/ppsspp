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

#include <condition_variable>
#include <mutex>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Serialize/SerializeSet.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/HW/AsyncIOManager.h"
#include "Core/FileSystems/MetaFileSystem.h"

bool AsyncIOManager::HasOperation(u32 handle) {
	std::lock_guard<std::mutex> guard(resultsLock_);
	if (resultsPending_.find(handle) != resultsPending_.end()) {
		return true;
	}
	if (results_.find(handle) != results_.end()) {
		return true;
	}
	return false;
}

void AsyncIOManager::ScheduleOperation(const AsyncIOEvent &ev) {
	{
		std::lock_guard<std::mutex> guard(resultsLock_);
		if (!resultsPending_.insert(ev.handle).second) {
			ERROR_LOG_REPORT(Log::sceIo, "Scheduling operation for file %d while one is pending (type %d)", ev.handle, ev.type);
		}
	}
	ScheduleEvent(ev);
}

void AsyncIOManager::Shutdown() {
	std::lock_guard<std::mutex> guard(resultsLock_);
	resultsPending_.clear();
	results_.clear();
}

bool AsyncIOManager::HasResult(u32 handle) {
	std::lock_guard<std::mutex> guard(resultsLock_);
	return results_.find(handle) != results_.end();
}

bool AsyncIOManager::PopResult(u32 handle, AsyncIOResult &result) {
	// This is called under lock from WaitResult, no need to lock again.
	if (results_.find(handle) != results_.end()) {
		result = results_[handle];
		results_.erase(handle);
		resultsPending_.erase(handle);

		if (result.invalidateAddr && result.result > 0) {
			currentMIPS->InvalidateICache(result.invalidateAddr, (int)result.result);
		}
		return true;
	} else {
		return false;
	}
}

bool AsyncIOManager::ReadResult(u32 handle, AsyncIOResult &result) {
	// This is called under lock from WaitResult, no need to lock again.
	if (results_.find(handle) != results_.end()) {
		result = results_[handle];
		return true;
	} else {
		return false;
	}
}

bool AsyncIOManager::WaitResult(u32 handle, AsyncIOResult &result) {
	std::unique_lock<std::mutex> guard(resultsLock_);
	ScheduleEvent(IO_EVENT_SYNC);
	while (HasEvents() && ThreadEnabled() && resultsPending_.find(handle) != resultsPending_.end()) {
		if (PopResult(handle, result)) {
			return true;
		}
		resultsWait_.wait_for(guard, std::chrono::milliseconds(16));
	}
	return PopResult(handle, result);
}

u64 AsyncIOManager::ResultFinishTicks(u32 handle) {
	AsyncIOResult result;

	std::unique_lock<std::mutex> guard(resultsLock_);
	ScheduleEvent(IO_EVENT_SYNC);
	while (HasEvents() && ThreadEnabled() && resultsPending_.find(handle) != resultsPending_.end()) {
		if (ReadResult(handle, result)) {
			return result.finishTicks;
		}
		resultsWait_.wait_for(guard, std::chrono::milliseconds(16));
	}
	if (ReadResult(handle, result)) {
		return result.finishTicks;
	}

	return 0;
}

void AsyncIOManager::ProcessEvent(AsyncIOEvent ev) {
	switch (ev.type) {
	case IO_EVENT_READ:
		Read(ev.handle, ev.buf, ev.bytes, ev.invalidateAddr);
		break;

	case IO_EVENT_WRITE:
		Write(ev.handle, ev.buf, ev.bytes);
		break;

	default:
		ERROR_LOG_REPORT(Log::sceIo, "Unsupported IO event type");
	}
}

void AsyncIOManager::Read(u32 handle, u8 *buf, size_t bytes, u32 invalidateAddr) {
	int usec = 0;
	s64 result = pspFileSystem.ReadFile(handle, buf, bytes, usec);
	EventResult(handle, AsyncIOResult(result, usec, invalidateAddr));
}

void AsyncIOManager::Write(u32 handle, const u8 *buf, size_t bytes) {
	int usec = 0;
	s64 result = pspFileSystem.WriteFile(handle, buf, bytes, usec);
	EventResult(handle, AsyncIOResult(result, usec));
}

void AsyncIOManager::EventResult(u32 handle, const AsyncIOResult &result) {
	std::lock_guard<std::mutex> guard(resultsLock_);
	if (results_.find(handle) != results_.end()) {
		ERROR_LOG_REPORT(Log::sceIo, "Overwriting previous result for file action on handle %d", handle);
	}
	results_[handle] = result;
	resultsWait_.notify_one();
}

void AsyncIOManager::DoState(PointerWrap &p) {
	auto s = p.Section("AsyncIoManager", 1, 2);
	if (!s)
		return;

	SyncThread();
	std::lock_guard<std::mutex> guard(resultsLock_);
	Do(p, resultsPending_);
	if (s >= 2) {
		Do(p, results_);
	} else {
		std::map<u32, size_t> oldResults;
		Do(p, oldResults);
		for (auto it = oldResults.begin(), end = oldResults.end(); it != end; ++it) {
			results_[it->first] = AsyncIOResult(it->second);
		}
	}
}
