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

#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/Debugger/WebSocket/GameSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"

DebuggerSubscriber *WebSocketGameInit(DebuggerEventHandlerMap &map) {
	map["game.reset"] = &WebSocketGameReset;
	map["game.status"] = &WebSocketGameStatus;
	map["version"] = &WebSocketVersion;

	return nullptr;
}

// Reset emulation (game.reset)
//
// Use this if you need to break on start and do something before the game starts.
//
// Parameters:
//  - break: optional boolean, true to break CPU on start.  Use cpu.resume afterward.
//
// Response (same event name) with no extra data or error.
void WebSocketGameReset(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("Game not running");

	bool needBreak = false;
	if (!req.ParamBool("break", &needBreak, DebuggerParamType::OPTIONAL))
		return;

	if (needBreak)
		PSP_CoreParameter().startBreak = true;

	std::string resetError;
	if (!PSP_Reboot(&resetError)) {
		ERROR_LOG(Log::Boot, "Error resetting: %s", resetError.c_str());
		return req.Fail("Could not reset");
	}

	System_Notify(SystemNotification::BOOT_DONE);
	System_Notify(SystemNotification::DISASSEMBLY);

	req.Respond();
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

	std::string version = req.client->version;
	if (!req.ParamString("version", &version, DebuggerParamType::OPTIONAL_LOOSE))
		return;
	std::string name = req.client->name;
	if (!req.ParamString("name", &name, DebuggerParamType::OPTIONAL_LOOSE))
		return;

	req.client->version = version;
	req.client->name = name;

	json.writeString("name", "PPSSPP");
	json.writeString("version", PPSSPP_GIT_VERSION);
}
