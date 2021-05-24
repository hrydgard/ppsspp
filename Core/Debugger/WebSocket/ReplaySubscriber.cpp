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

#include "Core/Replay.h"
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

void WebSocketReplayBegin(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayAbort(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayFlush(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayExecute(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayStatus(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayTimeGet(DebuggerRequest &req) {
	// TODO
}

void WebSocketReplayTimeSet(DebuggerRequest &req) {
	// TODO
}
