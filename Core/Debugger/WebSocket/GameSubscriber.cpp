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

#include "Core/Debugger/WebSocket/GameSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"

DebuggerSubscriber *WebSocketGameInit(DebuggerEventHandlerMap &map) {
	map["game.status"] = &WebSocketGameStatus;
	map["version"] = &WebSocketVersion;

	return nullptr;
}

// Check game status (game.status)
//
// No parameters.
//
// Response (same event name):
//  - game: null or an object with properties:
//     - id: string disc ID (such as ULUS12345.)
//     - version: string disc version.
//     - title: string game title.
//  - paused: boolean, true when gameplay is paused (not the same as stepping.)
void WebSocketGameStatus(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	if (PSP_IsInited()) {
		json.pushDict("game");
		json.writeString("id", g_paramSFO.GetDiscID());
		json.writeString("version", g_paramSFO.GetValueString("DISC_VERSION"));
		json.writeString("title", g_paramSFO.GetValueString("TITLE"));
		json.pop();
	} else {
		json.writeNull("game");
	}
	json.writeBool("paused", GetUIState() == UISTATE_PAUSEMENU);
}

// Notify debugger version info (version)
//
// Parameters:
//  - name: string indicating name of app or tool.
//  - version: string version.
//
// Response (same event name):
//  - name: string, "PPSSPP" unless some special build.
//  - version: string, typically starts with "v" and may have git build info.
void WebSocketVersion(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeString("name", "PPSSPP");
	json.writeString("version", PPSSPP_GIT_VERSION);
}
