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

#pragma once

#include "Core/Debugger/WebSocket/WebSocketUtils.h"

DebuggerSubscriber *WebSocketBreakpointInit(DebuggerEventHandlerMap &map);

void WebSocketCPUBreakpointAdd(DebuggerRequest &req);
void WebSocketCPUBreakpointUpdate(DebuggerRequest &req);
void WebSocketCPUBreakpointRemove(DebuggerRequest &req);
void WebSocketCPUBreakpointList(DebuggerRequest &req);

void WebSocketMemoryBreakpointAdd(DebuggerRequest &req);
void WebSocketMemoryBreakpointUpdate(DebuggerRequest &req);
void WebSocketMemoryBreakpointRemove(DebuggerRequest &req);
void WebSocketMemoryBreakpointList(DebuggerRequest &req);

void WebSocketRegBreakpointAdd(DebuggerRequest &req);
void WebSocketRegBreakpointUpdate(DebuggerRequest &req);
void WebSocketRegBreakpointRemove(DebuggerRequest &req);
void WebSocketRegBreakpointList(DebuggerRequest &req);

struct BreakpointHit;

// Writes the "hit" object describing what tripped a breakpoint. Shared by the cpu.breakpoint.hit
// broadcast and by cpu.stepping, so a client can parse the two the same way and the field set
// can't drift between them.
void WriteBreakpointHit(JsonWriter &json, const BreakpointHit &hit);
