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
#include "Core/System.h"

void GameBroadcaster::Broadcast(net::WebSocketServer *ws) {
	// TODO: This is ugly.  Implement proper information instead.
	// TODO: Should probably include info about which game, etc.
	GlobalUIState state = GetUIState();
	if (prevState_ != state) {
		if (state == UISTATE_PAUSEMENU) {
			ws->Send(R"({"event":"game.pause"})");
		} else if (state == UISTATE_INGAME && prevState_ == UISTATE_PAUSEMENU) {
			ws->Send(R"({"event":"game.resume"})");
		} else if (state == UISTATE_INGAME) {
			ws->Send(R"({"event":"game.start"})");
		} else if (state == UISTATE_MENU) {
			ws->Send(R"({"event":"game.quit"})");
		}
		prevState_ = state;
	}
}
