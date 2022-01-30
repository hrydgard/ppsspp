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

#include <unordered_map>
#include "Core/Debugger/WebSocket/InputBroadcaster.h"
#include "Core/Debugger/WebSocket/InputSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HW/Display.h"

// Button press state change (input.buttons)
//
// Sent unexpectedly with these properties:
//  - buttons: an object with button names as keys and bool press state as values.
//  - changed: same as buttons, but only including changed states.
//
// See input.buttons.send in InputSubscriber for button names.

// Analog position change (input.analog)
//
// Sent unexpectedly with these properties:
//  - stick: "left" or "right".
//  - x: number between -1.0 and 1.0, representing horizontal position in a square.
//  - y: number between -1.0 and 1.0, representing vertical position in a square.

std::string InputBroadcaster::Analog::Event(const char *stick) {
	JsonWriter j;
	j.begin();
	j.writeString("event", "input.analog");
	j.writeString("stick", stick);
	j.writeFloat("x", x);
	j.writeFloat("y", y);
	j.end();
	return j.str();
}

static std::string ButtonsEvent(uint32_t lastButtons, uint32_t newButtons) {
	uint32_t pressed = newButtons & ~lastButtons;
	uint32_t released = ~newButtons & lastButtons;

	JsonWriter j;
	j.begin();
	j.writeString("event", "input.buttons");
	j.pushDict("buttons");
	for (auto it : WebSocketInputButtonLookup()) {
		j.writeBool(it.first, (newButtons & it.second) != 0);
	}
	j.pop();
	j.pushDict("changed");
	for (auto it : WebSocketInputButtonLookup()) {
		if (pressed & it.second) {
			j.writeBool(it.first, true);
		} else if (released & it.second) {
			j.writeBool(it.first, false);
		}
	}
	j.pop();
	j.end();
	return j.str();
}

void InputBroadcaster::Broadcast(net::WebSocketServer *ws) {
	int counter = __DisplayGetNumVblanks();
	if (lastCounter_ == counter)
		return;
	lastCounter_ = counter;

	uint32_t newButtons = __CtrlPeekButtons();
	if (newButtons != lastButtons_) {
		ws->Send(ButtonsEvent(lastButtons_, newButtons));
		lastButtons_ = newButtons;
	}

	Analog newAnalog;
	__CtrlPeekAnalog(CTRL_STICK_LEFT, &newAnalog.x, &newAnalog.y);
	if (!lastAnalog_[0].Equals(newAnalog)) {
		ws->Send(newAnalog.Event("left"));
		lastAnalog_[0].x = newAnalog.x;
		lastAnalog_[0].y = newAnalog.y;
	}

	__CtrlPeekAnalog(CTRL_STICK_RIGHT, &newAnalog.x, &newAnalog.y);
	if (!lastAnalog_[1].Equals(newAnalog)) {
		ws->Send(newAnalog.Event("right"));
		lastAnalog_[1].x = newAnalog.x;
		lastAnalog_[1].y = newAnalog.y;
	}
}
