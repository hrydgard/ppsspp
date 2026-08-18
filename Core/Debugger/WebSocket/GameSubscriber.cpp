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

#include "ppsspp_config.h"

// For the process id in the version response. On desktop Windows this is deliberately the CRT's
// _getpid() rather than Win32 GetCurrentProcessId(), to avoid pulling windows.h into this file - it
// defines an OPTIONAL macro that collides with DebuggerParamType::OPTIONAL. UWP has no _getpid, and
// there windows.h is safe to include because WebSocketUtils.h below #undef's OPTIONAL for it.
#if PPSSPP_PLATFORM(UWP)
#include "Common/CommonWindows.h"
#elif PPSSPP_PLATFORM(WINDOWS) && !defined(__MINGW32__)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/Debugger/WebSocket/GameSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/RetroAchievements.h"
#include "Core/System.h"

// Declared rather than included from Core/HLE/sceNet.h, which reaches winsock and so windows.h
// through proAdhoc.h - and that redefines the OPTIONAL macro that collides with
// DebuggerParamType::OPTIONAL, exactly the problem the note above is avoiding.
bool NetworkAllowSpeedControl();

static uint32_t GetOwnProcessID() {
#if PPSSPP_PLATFORM(UWP)
	return (uint32_t)GetCurrentProcessId();
#elif PPSSPP_PLATFORM(WINDOWS) && !defined(__MINGW32__)
	return (uint32_t)_getpid();
#else
	return (uint32_t)getpid();
#endif
}

DebuggerSubscriber *WebSocketGameInit(DebuggerEventHandlerMap &map) {
	map["game.reset"] = &WebSocketGameReset;
	map["game.status"] = &WebSocketGameStatus;
	map["game.speed.get"] = &WebSocketGameSpeedGet;
	map["game.speed.set"] = &WebSocketGameSpeedSet;
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
	bool needBreak = false;
	if (!req.ParamBool("break", &needBreak, DebuggerParamType::OPTIONAL))
		return;

	// Route the boot-state check and the startBreak write to the CPU thread instead of poking at
	// them directly from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	bool running = false;
	Core_RunOnCPUThread([&] {
		running = PSP_GetBootState() == BootState::Complete;
		if (running && needBreak)
			PSP_CoreParameter().startBreak = true;
	});

	if (!running)
		return req.Fail("Game not running");

	// We can only support async resets here. A lot of the stuff in init must happen on the EmuThread,
	// and we are not on it here.
	System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);

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
	// Route the boot state and param SFO reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		if (PSP_GetBootState() == BootState::Complete) {
			json.pushDict("game");
			json.writeString("id", g_paramSFO.GetDiscID());
			json.writeString("version", g_paramSFO.GetValueString("DISC_VERSION"));
			json.writeString("title", g_paramSFO.GetValueString("TITLE"));
			json.pop();
		} else {
			json.writeNull("game");
		}
		json.writeBool("paused", GetUIState() == UISTATE_PAUSEMENU);
	});
}

// Percentages are relative to 60 FPS, matching how the in-app settings present the alternative
// speeds (see GameSettingsScreen.cpp) - so a client and the UI agree on what "200%" means.
static int PercentToFps(int percent) {
	return (percent * 60) / 100;
}

static int FpsToPercent(int fps) {
	return (fps * 100) / 60;
}

// Fails the request if something else owns the speed right now, rather than accepting it and
// having FrameTimingLimit() quietly ignore it - silently doing nothing is the worst outcome for
// an automation client.
static bool CheckSpeedControlAllowed(DebuggerRequest &req) {
	if (Achievements::HardcoreModeActive()) {
		req.Fail("Speed control is not allowed in RetroAchievements hardcore mode");
		return false;
	}
	if (!NetworkAllowSpeedControl()) {
		req.Fail("Speed control is not allowed while connected to a network game");
		return false;
	}
	return true;
}

static void WriteSpeedState(JsonWriter &json) {
	json.writeBool("fastForward", PSP_CoreParameter().fastForward);
	if (PSP_CoreParameter().fpsLimit == FPSLimit::DEBUGGER)
		json.writeInt("percent", FpsToPercent(PSP_CoreParameter().debuggerFpsLimit));
	else
		json.writeNull("percent");
	json.writeInt("limitFps", __DisplayGetFrameTimingLimit());
}

// Request the current emulation speed (game.speed.get)
//
// No parameters.
//
// Response (same event name):
//  - fastForward: boolean, whether unlimited fast-forward is on.
//  - percent: null, or the debugger's speed override as a percentage of 60 FPS.
//  - limitFps: the frame rate throttling is actually aiming for, after fastForward, percent, the
//    user's own alternative-speed hotkeys and any restrictions have all been applied. 0 means
//    unlimited. This is the ground truth - prefer it over inferring from the other two.
void WebSocketGameSpeedGet(DebuggerRequest &req) {
	// Route the reads to the CPU thread - these are what the frame timing consumes there.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		WriteSpeedState(json);
	});
}

// Change the emulation speed (game.speed.set)
//
// Parameters (at least one required):
//  - fastForward: optional boolean, run unlimited. Wins over 'percent' while on, same as the
//    in-app fast-forward key.
//  - percent: optional integer percentage of 60 FPS (100 = normal, 200 = double speed, 50 = half),
//    or null to drop the override and go back to the game's normal rate. Must be at least 1 -
//    use fastForward for unlimited rather than 0, so there's one way to say it.
//
// Response (same event name): the same fields as game.speed.get, after applying the change.
//
// Fails if RetroAchievements hardcore mode is active, or if connected to a network game without
// "allow speed control while connected" - in either case the setting would be ignored.
//
// Note this is a separate channel from the user's own alternative speeds (the CUSTOM1/CUSTOM2
// hotkeys and their settings): it deliberately doesn't touch g_Config, which is persisted per
// game, so a debugger session can't permanently change what the user configured.
void WebSocketGameSpeedSet(DebuggerRequest &req) {
	const bool hasFastForward = req.HasParam("fastForward");
	const bool hasPercent = req.HasParam("percent");
	if (!hasFastForward && !hasPercent)
		return req.Fail("Need at least one of 'fastForward' or 'percent'");

	bool fastForward = false;
	if (hasFastForward && !req.ParamBool("fastForward", &fastForward))
		return;

	// An explicit null clears the override; the parameter being absent leaves it alone.
	const bool clearPercent = hasPercent && !req.HasParam("percent", true);
	uint32_t percent = 0;
	if (hasPercent && !clearPercent) {
		if (!req.ParamU32("percent", &percent))
			return;
		if (percent < 1 || percent > 10000)
			return req.Fail("Parameter 'percent' must be between 1 and 10000");
		if (PercentToFps((int)percent) < 1)
			return req.Fail("Parameter 'percent' is too small to produce a frame rate");
	}

	if (!CheckSpeedControlAllowed(req))
		return;

	// Route the writes to the CPU thread instead of poking at them directly from this WebSocket
	// handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		if (hasFastForward)
			PSP_CoreParameter().fastForward = fastForward;

		if (clearPercent) {
			// Only stand down from a limit we set ourselves - the user may have an alternative
			// speed of their own active, and that isn't ours to cancel.
			if (PSP_CoreParameter().fpsLimit == FPSLimit::DEBUGGER)
				PSP_CoreParameter().fpsLimit = FPSLimit::NORMAL;
			PSP_CoreParameter().debuggerFpsLimit = 0;
		} else if (hasPercent) {
			PSP_CoreParameter().debuggerFpsLimit = PercentToFps((int)percent);
			PSP_CoreParameter().fpsLimit = FPSLimit::DEBUGGER;
		}

		JsonWriter &json = req.Respond();
		WriteSpeedState(json);
	});
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
//  - pid: unsigned integer, OS process id of this PPSSPP instance.
//  - path: null, or string path of the executable/disc image currently loaded.
//
// pid and path are here so an automation client can confirm it's attached to the instance it
// meant to attach to. Ports aren't enough on their own: a leftover process may still be holding
// the one you asked for, and you'd never know you were driving the wrong emulator.
void WebSocketVersion(DebuggerRequest &req) {
	std::string version = req.client->version;
	if (!req.ParamString("version", &version, DebuggerParamType::OPTIONAL_LOOSE))
		return;
	std::string name = req.client->name;
	if (!req.ParamString("name", &name, DebuggerParamType::OPTIONAL_LOOSE))
		return;

	req.client->version = version;
	req.client->name = name;

	// fileToStart is CPU-thread-owned (PSP_Shutdown clears it), so read it over there - see
	// Core_RunOnCPUThread() in Core.h. The rest is constant and safe to read here.
	std::string path;
	Core_RunOnCPUThread([&] {
		path = PSP_CoreParameter().fileToStart.ToString();
	});

	JsonWriter &json = req.Respond();
	json.writeString("name", "PPSSPP");
	json.writeString("version", PPSSPP_GIT_VERSION);
	json.writeUint("pid", GetOwnProcessID());
	if (path.empty())
		json.writeNull("path");
	else
		json.writeString("path", path);
}
