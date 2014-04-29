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

#include "Common/ChunkFile.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/HW/AsyncIOManager.h"
#include "Core/FileSystems/MetaFileSystem.h"

bool AsyncIOManager::HasOperation(u32 handle) {
	if (resultsPending_.find(handle) != resultsPending_.end()) {
		return true;
	}
	if (results_.find(handle) != results_.end()) {
		return true;
	}
	return false;
}

void AsyncIOManager::ScheduleOperation(AsyncIOEvent ev) {
	{
		lock_guard guard(resultsLock_);
		if (!resultsPending_.insert(ev.handle).second) {
			ERROR_LOG_REPORT(SCEIO, "Scheduling operation for file %d while one is pending (type %d)", ev.handle, ev.type);
		}
	}
	ScheduleEvent(ev);
}

void AsyncIOManager::Shutdown() {
	lock_guard guard(resultsLock_);
	resultsPending_.clear();
	results_.clear();
}

bool AsyncIOManager::PopResult(u32 handle, AsyncIOResult &result) {
	lock_guard guard(resultsLock_);
	if (results_.find(handle) != results_.end()) {
		result = results_[handle];
		results_.erase(handle);
		resultsPending_.erase(handle);
		return true;
	} else {
		return false;
	}
}

bool AsyncIOManager::WaitResult(u32 handle, AsyncIOResult &result) {
	lock_guard guard(resultsLock_);
	ScheduleEvent(IO_EVENT_SYNC);
	while (HasEvents() && ThreadEnabled() && resultsPending_.find(handle) != resultsPending_.end()) {
		if (PopResult(handle, result)) {
			return true;
		}
		resultsWait_.wait_for(resultsLock_, 16);
	}
	if (PopResult(handle, result)) {
		return true;
	}

	return false;
}

void AsyncIOManager::ProcessEvent(AsyncIOEvent ev) {
	switch (ev.type) {
	case IO_EVENT_READ:
		Read(ev.handle, ev.buf, ev.bytes);
		break;

	case IO_EVENT_WRITE:
		Write(ev.handle, ev.buf, ev.bytes);
		break;

	default:
		ERROR_LOG_REPORT(SCEIO, "Unsupported IO event type");
	}
}

void AsyncIOManager::Read(u32 handle, u8 *buf, size_t bytes) {
	size_t result = pspFileSystem.ReadFile(handle, buf, bytes);
	EventResult(handle, result);
}

void AsyncIOManager::Write(u32 handle, u8 *buf, size_t bytes) {
	size_t result = pspFileSystem.WriteFile(handle, buf, bytes);
	EventResult(handle, result);
}

void AsyncIOManager::EventResult(u32 handle, AsyncIOResult result) {
	lock_guard guard(resultsLock_);
	if (results_.find(handle) != results_.end()) {
		ERROR_LOG_REPORT(SCEIO, "Overwriting previous result for file action on handle %d", handle);
	}
	results_[handle] = result;
	resultsWait_.notify_one();
}

void AsyncIOManager::DoState(PointerWrap &p) {
	auto s = p.Section("AsyncIoManager", 1);
	if (!s)
		return;

	SyncThread();
	lock_guard guard(resultsLock_);
	p.Do(resultsPending_);
	p.Do(results_);
}
