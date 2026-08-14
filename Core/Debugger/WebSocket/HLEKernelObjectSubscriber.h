// Copyright (c) 2026- PPSSPP Project.

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

DebuggerSubscriber *WebSocketHLEKernelObjectInit(DebuggerEventHandlerMap &map);

void WebSocketHLEObjectList(DebuggerRequest &req);

void WebSocketHLEEventFlagList(DebuggerRequest &req);
void WebSocketHLEEventFlagInfo(DebuggerRequest &req);
void WebSocketHLEMutexList(DebuggerRequest &req);
void WebSocketHLEMutexInfo(DebuggerRequest &req);
void WebSocketHLESemaphoreList(DebuggerRequest &req);
void WebSocketHLESemaphoreInfo(DebuggerRequest &req);
void WebSocketHLEMsgPipeList(DebuggerRequest &req);
void WebSocketHLEMsgPipeInfo(DebuggerRequest &req);
void WebSocketHLECallbackList(DebuggerRequest &req);
void WebSocketHLECallbackInfo(DebuggerRequest &req);
