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

#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/WebSocket/HLESubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceKernelThread.h"

void *WebSocketHLEInit(DebuggerEventHandlerMap &map) {
	map["hle.thread.list"] = &WebSocketHLEThreadList;
	map["hle.func.list"] = &WebSocketHLEFuncList;
	map["hle.module.list"] = &WebSocketHLEModuleList;

	return nullptr;
}

void WebSocketHLEThreadList(DebuggerRequest &req) {
	// Will just return none of the CPU isn't ready yet.
	auto threads = GetThreadsInfo();

	JsonWriter &json = req.Respond();
	json.pushArray("threads");
	for (auto th : threads) {
		json.pushDict();
		json.writeUint("id", th.id);
		json.writeString("name", th.name);
		json.writeInt("status", th.status);
		json.pushArray("statuses");
		if (th.status & THREADSTATUS_RUNNING)
			json.writeString("running");
		if (th.status & THREADSTATUS_READY)
			json.writeString("ready");
		if (th.status & THREADSTATUS_WAIT)
			json.writeString("wait");
		if (th.status & THREADSTATUS_SUSPEND)
			json.writeString("suspend");
		if (th.status & THREADSTATUS_DORMANT)
			json.writeString("dormant");
		if (th.status & THREADSTATUS_DEAD)
			json.writeString("dead");
		json.pop();
		json.writeUint("pc", th.curPC);
		json.writeUint("entry", th.entrypoint);
		json.writeUint("initialStackSize", th.initialStack);
		json.writeUint("currentStackSize", th.stackSize);
		json.writeInt("priority", th.priority);
		json.writeInt("priority", (int)th.waitType);
		json.writeBool("isCurrent", th.isCurrent);
		json.pop();
	}
	json.pop();
}

void WebSocketHLEFuncList(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	auto functions = g_symbolMap->GetAllSymbols(ST_FUNCTION);

	JsonWriter &json = req.Respond();
	json.pushArray("functions");
	for (auto f : functions) {
		json.pushDict();
		json.writeString("name", f.name);
		json.writeUint("address", f.address);
		json.writeUint("size", f.size);
		json.pop();
	}
	json.pop();
}

void WebSocketHLEModuleList(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	auto modules = g_symbolMap->getAllModules();

	JsonWriter &json = req.Respond();
	json.pushArray("functions");
	for (auto m : modules) {
		json.pushDict();
		json.writeString("name", m.name);
		json.writeUint("address", m.address);
		json.writeUint("size", m.size);
		json.writeBool("isActive", m.active);
		json.pop();
	}
	json.pop();
}
