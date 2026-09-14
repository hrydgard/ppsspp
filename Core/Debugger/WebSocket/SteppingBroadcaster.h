// Copyright (c) 2018- PPSSPP Project.

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

#include <string>

namespace SteppingBroadcaster {

// Notices the CPU entering or leaving stepping and returns the formatted event for one, or an empty
// string if nothing changed. CPU thread only - it reads pc and the tick count, which is exactly what
// connected debuggers must not do from their own threads. Must be called even when no debugger is
// connected, so the transition state doesn't go stale.
std::string PollChange();

// The cpu.stepping event describing the state right now, for a debugger that connected while the
// CPU was already stopped. Empty if it isn't stepping. CPU thread only.
std::string CurrentState();

}  // namespace SteppingBroadcaster
