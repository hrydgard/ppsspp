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

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/WebSocket/BreakpointSubscriber.h"
#include "Core/Debugger/WebSocket/SteppingBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"

struct CPUSteppingEvent {
	// By value: the SteppingReason this is built from is a temporary at every call site, and it
	// carries strings now, so binding a reference to it is asking for trouble later.
	CPUSteppingEvent(const SteppingReason &reason) : reason_(reason) {
	}

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "cpu.stepping");
		j.writeUint("pc", currentMIPS->pc);
		// A double ought to be good enough for a 156 day debug session.
		j.writeFloat("ticks", CoreTiming::GetTicks(currentMIPS));
		if (reason_.reason != BreakReason::None) {
			j.writeString("reason", BreakReasonToString(reason_.reason));
			j.writeUint("relatedAddress", reason_.relatedAddress);
		}
		// Present only when a breakpoint was what stopped us, so its absence is the test rather
		// than some "kind": "none" the client would have to check for.
		if (reason_.hit.kind != BreakpointKind::None) {
			WriteBreakpointHit(j, reason_.hit);
		}
		j.end();
		return j.str();
	}

private:
	const SteppingReason reason_;
};

// CPU has begun stepping (cpu.stepping)
//
// Sent unexpectedly with these properties:
//  - pc: number value of PC register (inaccurate unless stepping.)
//  - ticks: number of CPU cycles into emulation.
//  - reason: an optional property, if present, it's equal to the value submitted to Core_EnableStepping ("jit.branchdebug", "savestate.load", "ui.lost_focus", etc.)
//  - relatedAddress: an optional address (often zero, but it can be a value of PC saved at some point, a related memory address, etc.), always present if 'reason' is present

// CPU has resumed from stepping (cpu.resume)
//
// Sent unexpectedly with no other properties.
// Tracked globally rather than per connection: this runs on the CPU thread, which owns the state
// being read, and the resulting event is then handed to every connected debugger.
static CoreState g_prevState = CORE_POWERDOWN;
static int g_lastCounter = 0;

std::string SteppingBroadcaster::PollChange() {
	if (PSP_GetBootState() != BootState::Complete) {
		g_lastCounter = -1;
		g_prevState = CORE_POWERDOWN;
		return std::string();
	}

	std::string result;
	const int steppingCounter = Core_GetSteppingCounter();
	// We ignore CORE_POWERDOWN as a stepping state.
	if (coreState == CORE_STEPPING_CPU && steppingCounter != g_lastCounter) {
		result = CPUSteppingEvent(Core_GetSteppingReason());
	} else if (g_prevState == CORE_STEPPING_CPU && coreState != CORE_STEPPING_CPU && Core_IsActive()) {
		result = R"({"event":"cpu.resume"})";
	}
	g_lastCounter = steppingCounter;
	g_prevState = coreState;
	return result;
}

std::string SteppingBroadcaster::CurrentState() {
	if (PSP_GetBootState() != BootState::Complete || coreState != CORE_STEPPING_CPU)
		return std::string();
	return CPUSteppingEvent(Core_GetSteppingReason());
}
