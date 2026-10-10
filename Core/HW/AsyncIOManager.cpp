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


#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Serialize/SerializeSet.h"
#include "Core/MIPS/MIPS.h"
#include "Common/LogOnce.h"
#include "Core/HW/AsyncIOManager.h"
#include "Core/FileSystems/MetaFileSystem.h"

bool AsyncIOManager::HasOperation(u32 handle) {
	return resultsPending_.find(handle) != resultsPending_.end() || results_.find(handle) != results_.end();
}

void AsyncIOManager::ScheduleOperation(const AsyncIOEvent &ev) {
	if (!resultsPending_.insert(ev.handle).second) {
		ERROR_LOG(Log::sceIo, "Scheduling operation for file %d while one is pending (type %d)", ev.handle, ev.type);
	}
	switch (ev.type) {
	case IO_EVENT_READ:
		Read(ev.handle, ev.buf, ev.bytes, ev.invalidateAddr);
		break;

	case IO_EVENT_WRITE:
		Write(ev.handle, ev.buf, ev.bytes);
		break;

	default:
		ERROR_LOG(Log::sceIo, "Unsupported IO event type");
		resultsPending_.erase(ev.handle);
		break;
	}
}

void AsyncIOManager::Shutdown() {
	resultsPending_.clear();
	results_.clear();
}

bool AsyncIOManager::HasResult(u32 handle) {
	return results_.find(handle) != results_.end();
}

bool AsyncIOManager::PopResult(u32 handle, AsyncIOResult &result) {
	auto iter = results_.find(handle);
	if (iter == results_.end()) {
		return false;
	}
	result = iter->second;
	results_.erase(iter);
	resultsPending_.erase(handle);

	if (result.invalidateAddr && result.result > 0) {
		currentMIPS->InvalidateICacheRangeImmediate(result.invalidateAddr, (int)result.result);
	}
	return true;
}

bool AsyncIOManager::WaitResult(u32 handle, AsyncIOResult &result) {
	// Operations finish as they're scheduled, so there's nothing to wait for.
	return PopResult(handle, result);
}

u64 AsyncIOManager::ResultFinishTicks(u32 handle) {
	auto iter = results_.find(handle);
	return iter != results_.end() ? iter->second.finishTicks : 0;
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
	if (results_.find(handle) != results_.end()) {
		ERROR_LOG(Log::sceIo, "Overwriting previous result for file action on handle %d", handle);
	}
	results_[handle] = result;
}

void AsyncIOManager::DoState(PointerWrap &p) {
	auto s = p.Section("AsyncIoManager", 1, 2);
	if (!s)
		return;

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
