// Copyright (c) 2021- PPSSPP Project.

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

#include <cstdint>
#include "Common/Data/Encoding/Base64.h"
#include "Common/Swap.h"
#include "Core/HLE/sceRtc.h"
#include "Core/Replay.h"
#include "Core/System.h"
#include "Core/Debugger/WebSocket/ReplaySubscriber.h"

DebuggerSubscriber *WebSocketReplayInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["replay.begin"] = &WebSocketReplayBegin;
	map["replay.abort"] = &WebSocketReplayAbort;
	map["replay.flush"] = &WebSocketReplayFlush;
	map["replay.execute"] = &WebSocketReplayExecute;
	map["replay.status"] = &WebSocketReplayStatus;
	map["replay.time.get"] = &WebSocketReplayTimeGet;
	map["replay.time.set"] = &WebSocketReplayTimeSet;

	return nullptr;
}

// Begin or resume recording of replay data (replay.begin)
//
// If a replay was previously being played back, this will keep any executed replay data up to
// this point for the next flush.  To discard, break the CPU, abort, and then begin.
//
// No parameters.
//
// Response (same event name) with no extra data.
void WebSocketReplayBegin(DebuggerRequest &req) {
	ReplayBeginSave();
	req.Respond();
}

// Abort any replay execution or recording (replay.abort)
//
// This stops executing any replay and discards any in progress recording.
//
// No parameters.
//
// Response (same event name) with no extra data.
void WebSocketReplayAbort(DebuggerRequest &req) {
	ReplayAbort();
	req.Respond();
}

// Flush current recording data (replay.flush)
//
// Flushes event data and returns it.  Note when combining, you must decode first.
//
// No parameters.
//
// Response (same event name):
//  - version: unsigned integer, version number of data.
//  - base64: base64 encode of binary data.
void WebSocketReplayFlush(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("Game not running");

	std::vector<uint8_t> data;
	ReplayFlushBlob(&data);

	JsonWriter &json = req.Respond();
	json.writeInt("version", ReplayVersion());
	json.writeString("base64", Base64Encode(data.data(), data.size()));
}

// Begin executing a replay (replay.execute)
//
// Parameters:
//  - version: unsigned integer, same version from replay.flush.
//  - base64: base64 encoded replay data.
//
// Response (same event name) with no extra data.
void WebSocketReplayExecute(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("Game not running");

	uint32_t version = -1;
	if (!req.ParamU32("version", &version))
		return;
	std::string encoded;
	if (!req.ParamString("base64", &encoded))
		return;

	std::vector<uint8_t> data = Base64Decode(encoded.data(), encoded.size());
	if (!ReplayExecuteBlob(version, data))
		return req.Fail("Invalid replay data or version");

	req.Respond();
}

// Get replay status (replay.status)
//
// No parameters.
//
// Response (same event name):
//  - executing: boolean if a replay is being executed.
//  - saving: boolean if a replay is being recorded.
void WebSocketReplayStatus(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeBool("executing", ReplayIsExecuting());
	json.writeBool("saving", ReplayIsSaving());
}

// Get the base RTC (real time clock) time for replay data (replay.time.get)
//
// The base time is constant during a game session, and represents the "power on" time of the
// emulated PSP.
//
// No parameters.
//
// Response (same event name):
//  - value: unsigned integer, may have more than 32 integer bits.
void WebSocketReplayTimeGet(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("Game not running");

	JsonWriter &json = req.Respond();
	json.writeUint("value", RtcBaseTime());
}

// Overwrite the base RTC time (replay.time.set)
//
// Parameters:
//  - value: unsigned integer.
//
// Response (same event name) with no extra data.
void WebSocketReplayTimeSet(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("Game not running");

	uint32_t value;
	if (!req.ParamU32("value", &value, false)) {
		return;
	}

	RtcSetBaseTime((int32_t)value);
	req.Respond();
}
