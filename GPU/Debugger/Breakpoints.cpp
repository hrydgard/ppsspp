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

#include <vector>
#include <set>
#include "base/mutex.h"
#include "GPU/Debugger/Breakpoints.h"

namespace GPUBreakpoints {

static recursive_mutex breaksLock;
static std::vector<bool> breakCmds;
static std::set<u32> breakPCs;
// Small optimization to avoid a lock/lookup for the common case.
static size_t breakPCsCount = 0;

// If these are set, the above are also, but they should be temporary.
static std::vector<bool> breakCmdsTemp;
static std::set<u32> breakPCsTemp;

void Init() {
	ClearAllBreakpoints();
}

bool IsAddressBreakpoint(u32 addr, bool &temp) {
	if (breakPCsCount == 0) {
		temp = false;
		return false;
	}

	lock_guard guard(breaksLock);
	temp = breakPCsTemp.find(addr) != breakPCsTemp.end();
	return breakPCs.find(addr) != breakPCs.end();
}

bool IsAddressBreakpoint(u32 addr) {
	if (breakPCsCount == 0) {
		return false;
	}

	lock_guard guard(breaksLock);
	return breakPCs.find(addr) != breakPCs.end();
}

bool IsCmdBreakpoint(u8 cmd, bool &temp) {
	temp = breakCmdsTemp[cmd];
	return breakCmds[cmd];
}

bool IsCmdBreakpoint(u8 cmd) {
	return breakCmds[cmd];
}

void AddAddressBreakpoint(u32 addr, bool temp) {
	lock_guard guard(breaksLock);

	if (temp) {
		if (breakPCs.find(addr) == breakPCs.end()) {
			breakPCsTemp.insert(addr);
			breakPCs.insert(addr);
		}
		// Already normal breakpoint, let's not make it temporary.
	} else {
		// Remove the temporary marking.
		breakPCsTemp.erase(addr);
		breakPCs.insert(addr);
	}

	breakPCsCount = breakPCs.size();
}

void AddCmdBreakpoint(u8 cmd, bool temp) {
	if (temp) {
		if (!breakCmds[cmd]) {
			breakCmdsTemp[cmd] = true;
			breakCmds[cmd] = true;
		}
		// Ignore adding a temp breakpoint when a normal one exists.
	} else {
		// This is no longer temporary.
		breakCmdsTemp[cmd] = false;
		breakCmds[cmd] = true;
	}
}

void RemoveAddressBreakpoint(u32 addr) {
	lock_guard guard(breaksLock);

	breakPCsTemp.erase(addr);
	breakPCs.erase(addr);

	breakPCsCount = breakPCs.size();
}

void RemoveCmdBreakpoint(u8 cmd) {
	breakCmdsTemp[cmd] = false;
	breakCmds[cmd] = false;
}

void ClearAllBreakpoints() {
	lock_guard guard(breaksLock);

	breakCmds.clear();
	breakCmds.resize(256, false);
	breakPCs.clear();

	breakCmdsTemp.clear();
	breakCmdsTemp.resize(256, false);
	breakPCsTemp.clear();

	breakPCsCount = breakPCs.size();
}

void ClearTempBreakpoints() {
	lock_guard guard(breaksLock);

	// Reset ones that were temporary back to non-breakpoints in the primary arrays.
	for (int i = 0; i < 256; ++i) {
		if (breakCmdsTemp[i]) {
			breakCmds[i] = false;
			breakCmdsTemp[i] = false;
		}
	}

	for (auto it = breakPCsTemp.begin(), end = breakPCsTemp.end(); it != end; ++it) {
		breakPCs.erase(*it);
	}
	breakPCsTemp.clear();

	breakPCsCount = breakPCs.size();
}

};
