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

#include "Core/Debugger/WebSocket/GameBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"

struct GameStatusEvent {
	const char *ev;

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", ev);
		if (PSP_GetBootState() == BootState::Complete) {
			j.pushDict("game");
			j.writeString("id", g_paramSFO.GetDiscID());
			j.writeString("version", g_paramSFO.GetValueString("DISC_VERSION"));
			j.writeString("title", g_paramSFO.GetValueString("TITLE"));
			j.pop();
		} else {
			j.writeNull("game");
		}
		j.end();
		return j.str();
	}
};

// Game started (game.start)
//
// Sent unexpectedly with these properties:
//  - game: null or an object with properties:
//     - id: string disc ID (such as ULUS12345.)
//     - version: string disc version.
//     - title: string game title.

// Game quit / ended (game.quit)
//
// Sent unexpectedly with these properties:
//  - game: null

// Game paused (game.pause)
//
// Note: this is not the same as stepping.  This means the user went to the pause menu.
//
// Sent unexpectedly with these properties:
//  - game: null or an object with properties:
//     - id: string disc ID (such as ULUS12345.)
//     - version: string disc version.
//     - title: string game title.

// Game resumed (game.resume)
//
// Note: this is not the same as stepping.  This means the user resumed from the pause menu.
//
// Sent unexpectedly with these properties:
//  - game: null or an object with properties:
//     - id: string disc ID (such as ULUS12345.)
//     - version: string disc version.
//     - title: string game title.
std::string GameBroadcaster::PollChange() {
	// Tracked globally rather than per connection: this runs on the CPU thread, which owns the state
	// being read, and the resulting event is then handed to every connected debugger.
	static GlobalUIState prevState = GetUIState();

	const GlobalUIState state = GetUIState();
	if (prevState == state)
		return std::string();

	const char *ev = nullptr;
	if (state == UISTATE_PAUSEMENU) {
		ev = "game.pause";
	} else if (state == UISTATE_INGAME && prevState == UISTATE_PAUSEMENU) {
		ev = "game.resume";
	} else if (state == UISTATE_INGAME && PSP_GetBootState() == BootState::Complete) {
		ev = "game.start";
	} else if (state == UISTATE_MENU && PSP_GetBootState() != BootState::Complete) {
		ev = "game.quit";
	}
	if (!ev)
		return std::string();

	prevState = state;
	return GameStatusEvent{ev};
}
