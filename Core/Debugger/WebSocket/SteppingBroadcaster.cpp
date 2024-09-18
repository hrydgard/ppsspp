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
#include "Core/Debugger/WebSocket/SteppingBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"

struct CPUSteppingEvent {
	CPUSteppingEvent(const SteppingReason &reason) : reason_(reason) {
	}

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "cpu.stepping");
		j.writeUint("pc", currentMIPS->pc);
		// A double ought to be good enough for a 156 day debug session.
		j.writeFloat("ticks", CoreTiming::GetTicks());
		j.writeString("reason", reason_.reason);
		j.writeUint("relatedAddress", reason_.relatedAddress);
		j.end();
		return j.str();
	}

private:
	const SteppingReason &reason_;
};

// CPU has begun stepping (cpu.stepping)
//
// Sent unexpectedly with these properties:
//  - pc: number value of PC register (inaccurate unless stepping.)
//  - ticks: number of CPU cycles into emulation.
//  - reason: a value submitted to Core_EnableStepping ("jit.branchdebug", "savestate.load", "ui.lost_focus", etc.)
//  - relatedAddress: an address (often zero, but it can be a value of PC saved at some point, a related memory address, etc.)

// CPU has resumed from stepping (cpu.resume)
//
// Sent unexpectedly with no other properties.
void SteppingBroadcaster::Broadcast(net::WebSocketServer *ws) {
	if (PSP_IsInited()) {
		int steppingCounter = Core_GetSteppingCounter();
		// We ignore CORE_POWERDOWN as a stepping state.
		if (coreState == CORE_STEPPING && steppingCounter != lastCounter_) {
			ws->Send(CPUSteppingEvent(Core_GetSteppingReason()));
		} else if (prevState_ == CORE_STEPPING && coreState != CORE_STEPPING && Core_IsActive()) {
			ws->Send(R"({"event":"cpu.resume"})");
		}
		lastCounter_ = steppingCounter;
		prevState_ = coreState;
	} else {
		lastCounter_ = -1;
		prevState_ = CORE_POWERDOWN;
	}
}
