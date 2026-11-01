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

#include <cstdint>
#include <string>
#include <vector>

// Samples the host PC of one thread at a fixed rate, to find where JIT code spends its time.
// Costs nothing until started.
namespace JitProfiler {

struct Sample {
	uintptr_t pc;
	// The return address, if the PC is in a leaf routine (ARM: LR, x86-64: [rsp]). Only meaningful when
	// the PC is in a routine called from a block that saves nothing (like the dot product routine).
	uintptr_t caller;
};

// Whether this host can sample at all.
bool Supported();
// Starts sampling the calling thread, every periodMicros.
bool Start(int periodMicros);
void Stop();
bool IsRunning();
int PeriodMicros();

// Appends the samples taken since the last drain. Call from the sampled thread, or after Stop().
void Drain(std::vector<Sample> &out);
// Samples lost to a full buffer.
uint64_t Dropped();

// The name of the host function containing pc, or "" if unknown. Slow.
std::string DescribeHostAddress(uintptr_t pc, uintptr_t *symbolStart);

}  // namespace JitProfiler
