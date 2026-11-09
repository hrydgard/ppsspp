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

struct APerformanceHintSession;

// An ADPF performance hint session (Android 13+) for the thread that runs the frame loop.
// Emulation does a burst of work each frame and then waits, so the scheduler sees a mostly idle
// thread and moves it to a slow core at low clocks, where the next burst then runs. Reporting each
// frame's work against a target tells it what we actually need. Does nothing where unsupported.
class PerformanceHintSession {
public:
	~PerformanceHintSession();

	// Call once per frame, always from the same thread. The session is created on the first call.
	void ReportFrame();

private:
	APerformanceHintSession *session_ = nullptr;
	bool failed_ = false;
	int64_t lastCpuTimeNs_ = -1;
};
