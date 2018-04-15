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
#include "Core/Debugger/WebSocket/SteppingBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/System.h"

void SteppingBroadcaster::Broadcast(net::WebSocketServer *ws) {
	// TODO: This is somewhat primitive.  It'd be nice to register a callback with Core instead?
	if (coreState != prevState_ && PSP_IsInited()) {
		// We ignore CORE_POWERDOWN.
		if (coreState == CORE_STEPPING) {
			// TODO: Should send more data proactively.
			ws->Send(R"({"event":"cpu.stepping"})");
		} else if (prevState_ == CORE_STEPPING && Core_IsActive()) {
			ws->Send(R"({"event":"cpu.resume"})");
		}
	}
	prevState_ = coreState;
}
