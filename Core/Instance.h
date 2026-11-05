// Copyright (c) 2020 PPSSPP Project.

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

#include "ppsspp_config.h"

#include <cstdint>
#include <string>

extern uint8_t PPSSPP_ID;

void InitInstanceCounter();
void ShutdownInstanceCounter();
int GetInstancePeerCount();

inline bool IsFirstInstance() {
	return PPSSPP_ID == 1;
}

// Keeps several instances on one machine in step in emulated time, for ad hoc tests: each one stops
// at every emulated millisecond until all the others have reached it too, so they're never more than
// a millisecond apart, however fast each one runs. They share a file of one slot per instance (the
// launcher creates it, zero-filled, at least INSTANCE_SYNC_FILE_SIZE bytes); index is 1-based.
constexpr int INSTANCE_SYNC_FILE_SIZE = 4096;
bool InitInstanceSync(const std::string &path, int index, int count);
void ShutdownInstanceSync();
bool InstanceSyncEnabled();
// Called by CoreTiming at each emulated millisecond: publishes it and waits for the others.
void InstanceSyncReached(int64_t ms);
// Around a wait in real time on the CPU thread for something another instance has to do (the others
// would otherwise sit waiting for this one): meanwhile they run on without it, and wait for it to
// catch up afterwards.
void InstanceSyncStandAside(bool aside);
