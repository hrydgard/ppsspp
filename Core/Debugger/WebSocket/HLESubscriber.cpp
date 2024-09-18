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

#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/WebSocket/HLESubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSStackWalk.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"

DebuggerSubscriber *WebSocketHLEInit(DebuggerEventHandlerMap &map) {
	map["hle.thread.list"] = &WebSocketHLEThreadList;
	map["hle.thread.wake"] = &WebSocketHLEThreadWake;
	map["hle.thread.stop"] = &WebSocketHLEThreadStop;
	map["hle.func.list"] = &WebSocketHLEFuncList;
	map["hle.func.add"] = &WebSocketHLEFuncAdd;
	map["hle.func.remove"] = &WebSocketHLEFuncRemove;
	map["hle.func.removeRange"] = &WebSocketHLEFuncRemoveRange;
	map["hle.func.rename"] = &WebSocketHLEFuncRename;
	map["hle.func.scan"] = &WebSocketHLEFuncScan;
	map["hle.module.list"] = &WebSocketHLEModuleList;
	map["hle.backtrace"] = &WebSocketHLEBacktrace;

	return nullptr;
}

// List all current HLE threads (hle.thread.list)
//
// No parameters.
//
// Response (same event name):
//  - threads: array of objects, each with properties:
//     - id: unsigned integer unique id of thread.
//     - name: name given to thread when created.
//     - status: numeric status flags of thread.
//     - statuses: array of string status names, e.g. 'running'.  Typically only one set.
//     - pc: unsigned integer address of next instruction on thread.
//     - entry: unsigned integer address thread execution started at.
//     - initialStackSize: unsigned integer, size of initial stack.
//     - currentStackSize: unsigned integer, size of stack (e.g. if resized.)
//     - priority: numeric priority level, lower values are better priority.
//     - waitType: numeric wait type, if the thread is waiting, or 0 if not waiting.
//     - isCurrent: boolean, true for the currently executing thread.
void WebSocketHLEThreadList(DebuggerRequest &req) {
	// Will just return none of the CPU isn't ready yet.
	auto threads = GetThreadsInfo();

	JsonWriter &json = req.Respond();
	json.pushArray("threads");
	for (const auto &th : threads) {
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
		json.writeInt("waitType", (int)th.waitType);
		json.writeBool("isCurrent", th.isCurrent);
		json.pop();
	}
	json.pop();
}

static bool ThreadInfoForStatus(DebuggerRequest &req, DebugThreadInfo *result) {
	if (!PSP_IsInited()) {
		req.Fail("CPU not active");
		return false;
	}
	if (!Core_IsStepping()) {
		req.Fail("CPU currently running (cpu.stepping first)");
		return false;
	}

	uint32_t threadID;
	if (!req.ParamU32("thread", &threadID))
		return false;

	auto threads = GetThreadsInfo();
	for (const auto &t : threads) {
		if (t.id == threadID) {
			*result = t;
			return true;
		}
	}

	req.Fail("Thread could not be found");
	return false;
}

// Force resume a thread (hle.thread.wake)
//
// Parameters:
//  - thread: number indicating the thread id to resume.
//
// Response (same event name):
//  - thread: id repeated back.
//  - status: string 'ready'.
void WebSocketHLEThreadWake(DebuggerRequest &req) {
	DebugThreadInfo threadInfo{ -1 };
	if (!ThreadInfoForStatus(req, &threadInfo))
		return;

	switch (threadInfo.status) {
	case THREADSTATUS_SUSPEND:
	case THREADSTATUS_WAIT:
	case THREADSTATUS_WAITSUSPEND:
		if (__KernelResumeThreadFromWait(threadInfo.id, 0) != 0)
			return req.Fail("Failed to resume thread");
		break;

	default:
		return req.Fail("Cannot force run thread based on current status");
	}

	Reporting::NotifyDebugger();

	JsonWriter &json = req.Respond();
	json.writeUint("thread", threadInfo.id);
	json.writeString("status", "ready");
}

// Force stop a thread (hle.thread.stop)
//
// Parameters:
//  - thread: number indicating the thread id to stop.
//
// Response (same event name):
//  - thread: id repeated back.
//  - status: string 'dormant'.
void WebSocketHLEThreadStop(DebuggerRequest &req) {
	DebugThreadInfo threadInfo{ -1 };
	if (!ThreadInfoForStatus(req, &threadInfo))
		return;

	switch (threadInfo.status) {
	case THREADSTATUS_SUSPEND:
	case THREADSTATUS_WAIT:
	case THREADSTATUS_WAITSUSPEND:
	case THREADSTATUS_READY:
		__KernelStopThread(threadInfo.id, 0, "stopped from debugger");
		break;

	default:
		return req.Fail("Cannot force run thread based on current status");
	}

	// Get it again to verify.
	if (!ThreadInfoForStatus(req, &threadInfo))
		return;
	if ((threadInfo.status & THREADSTATUS_DORMANT) == 0)
		return req.Fail("Failed to stop thread");

	Reporting::NotifyDebugger();

	JsonWriter &json = req.Respond();
	json.writeUint("thread", threadInfo.id);
	json.writeString("status", "dormant");
}

// List all current known function symbols (hle.func.list)
//
// No parameters.
//
// Response (same event name):
//  - functions: array of objects, each with properties:
//     - name: current name of function.
//     - address: unsigned integer start address of function.
//     - size: unsigned integer size in bytes.
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

// Add a new function symbols (hle.func.add)
//
// Parameters:
//  - address: unsigned integer address for the start of the function.
//  - size: unsigned integer size in bytes, optional.  If 'address' is inside a function,
//    defaults to that function's end, otherwise 4 bytes.
//  - name: string to name the function, optional and defaults to an auto-generated name.
//
// Response (same event name):
//  - address: the start address, repeated back.
//  - size: the size of the function, whether autodetected or not.
//  - name: name of the new function.
//
// Note: will fail if a function starts at that location already, or if size spans multiple
// existing functions.  Remove those functions first if necessary.
void WebSocketHLEFuncAdd(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;
	u32 size = -1;
	if (!req.ParamU32("size", &size, false, DebuggerParamType::OPTIONAL))
		return;
	if (size == 0)
		size = -1;

	std::string name;
	if (!req.ParamString("name", &name, DebuggerParamType::OPTIONAL))
		return;
	if (name.empty())
		name = StringFromFormat("z_un_%08x", addr);

	u32 prevBegin = g_symbolMap->GetFunctionStart(addr);
	u32 endBegin = size == -1 ? prevBegin : g_symbolMap->GetFunctionStart(addr + size - 1);
	if (prevBegin == addr) {
		return req.Fail("Function already exists at 'address'");
	} else if (endBegin != prevBegin) {
		return req.Fail("Function already exists between 'address' and 'address' + 'size'");
	} else if (prevBegin != -1) {
		std::string prevName = g_symbolMap->GetLabelString(prevBegin);
		u32 prevSize = g_symbolMap->GetFunctionSize(prevBegin);
		u32 newPrevSize = addr - prevBegin;

		// The new function will be the remainder, unless otherwise specified.
		if (size == -1)
			size = prevSize - newPrevSize;

		// Make sure we register the new length for replacements too.
		MIPSAnalyst::ForgetFunctions(prevBegin, prevBegin + newPrevSize - 1);
		g_symbolMap->SetFunctionSize(prevBegin, newPrevSize);
		MIPSAnalyst::RegisterFunction(prevBegin, newPrevSize, prevName.c_str());
	} else {
		// There was no function there, so hopefully they specified a size.
		if (size == -1)
			size = 4;
	}

	// To ensure we restore replacements.
	MIPSAnalyst::ForgetFunctions(addr, addr + size - 1);
	g_symbolMap->AddFunction(name.c_str(), addr, size);
	g_symbolMap->SortSymbols();
	MIPSAnalyst::RegisterFunction(addr, size, name.c_str());

	MIPSAnalyst::UpdateHashMap();
	MIPSAnalyst::ApplyHashMap();

	if (g_Config.bFuncReplacements) {
		MIPSAnalyst::ReplaceFunctions();
	}

	// Clear cache for branch lines and such.
	DisassemblyManager manager;
	manager.clear();

	JsonWriter &json = req.Respond();
	json.writeUint("address", addr);
	json.writeUint("size", size);
	json.writeString("name", name);
}

// Remove a function symbol (hle.func.remove)
//
// Parameters:
//  - address: unsigned integer address within function to remove.
//
// Response (same event name):
//  - address: the start address of the removed function.
//  - size: the size in bytes of the removed function.
//
// Note: will expand any previous function automatically.
void WebSocketHLEFuncRemove(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;

	u32 funcBegin = g_symbolMap->GetFunctionStart(addr);
	if (funcBegin == -1)
		return req.Fail("No function found at 'address'");
	u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin);

	// Expand the previous function.
	u32 prevBegin = g_symbolMap->GetFunctionStart(funcBegin - 1);
	if (prevBegin != -1) {
		std::string prevName = g_symbolMap->GetLabelString(prevBegin);
		u32 expandedSize = g_symbolMap->GetFunctionSize(prevBegin) + funcSize;
		g_symbolMap->SetFunctionSize(prevBegin, expandedSize);
		MIPSAnalyst::ForgetFunctions(prevBegin, prevBegin + expandedSize - 1);
		MIPSAnalyst::RegisterFunction(prevBegin, expandedSize, prevName.c_str());
	} else {
		MIPSAnalyst::ForgetFunctions(funcBegin, funcBegin + funcSize - 1);
	}

	g_symbolMap->RemoveFunction(funcBegin, true);
	g_symbolMap->SortSymbols();

	MIPSAnalyst::UpdateHashMap();
	MIPSAnalyst::ApplyHashMap();

	if (g_Config.bFuncReplacements) {
		MIPSAnalyst::ReplaceFunctions();
	}

	// Clear cache for branch lines and such.
	DisassemblyManager manager;
	manager.clear();

	JsonWriter &json = req.Respond();
	json.writeUint("address", funcBegin);
	json.writeUint("size", funcSize);
}

// This function removes function symbols that intersect or lie inside the range
// (Note: this makes no checks whether the range is valid)
// Returns the number of removed functions
static u32 RemoveFuncSymbolsInRange(u32 addr, u32 size) {
	u32 func_address = g_symbolMap->GetFunctionStart(addr);
	if (func_address == SymbolMap::INVALID_ADDRESS) {
		func_address = g_symbolMap->GetNextSymbolAddress(addr, SymbolType::ST_FUNCTION);
	}

	u32 counter = 0;
	while (func_address < addr + size && func_address != SymbolMap::INVALID_ADDRESS) {
		g_symbolMap->RemoveFunction(func_address, true);
		++counter;
		func_address = g_symbolMap->GetNextSymbolAddress(addr, SymbolType::ST_FUNCTION);
	}

	if (counter) {
		MIPSAnalyst::ForgetFunctions(addr, addr + size - 1);

		// The following was copied from hle.func.remove:
		g_symbolMap->SortSymbols();

		MIPSAnalyst::UpdateHashMap();
		MIPSAnalyst::ApplyHashMap();

		if (g_Config.bFuncReplacements) {
			MIPSAnalyst::ReplaceFunctions();
		}

		// Clear cache for branch lines and such.
		DisassemblyManager manager;
		manager.clear();
	}
	return counter;
}

// Remove function symbols in range (hle.func.removeRange)
//
// Parameters:
//  - address: unsigned integer address for the start of the range.
//  - size: unsigned integer size in bytes for removal
//
// Response (same event name):
//  - count: number of removed functions
void WebSocketHLEFuncRemoveRange(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;
	u32 size;
	if (!req.ParamU32("size", &size))
		return;

	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Address or size outside valid memory");

	u32 count = RemoveFuncSymbolsInRange(addr, size);

	JsonWriter &json = req.Respond();
	json.writeUint("count", count);
}

// Rename a function symbol (hle.func.rename)
//
// Parameters:
//  - address: unsigned integer address within function to rename.
//  - name: string, new name for the function.
//
// Response (same event name):
//  - address: the start address of the renamed function.
//  - size: the size in bytes of the renamed function.
//  - name: string, new name repeated back.
void WebSocketHLEFuncRename(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;
	std::string name;
	if (!req.ParamString("name", &name))
		return;

	u32 funcBegin = g_symbolMap->GetFunctionStart(addr);
	if (funcBegin == -1)
		return req.Fail("No function found at 'address'");
	u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin);

	g_symbolMap->SetLabelName(name.c_str(), funcBegin);
	// To ensure we reapply replacements (in case we check name there.)
	MIPSAnalyst::ForgetFunctions(funcBegin, funcBegin + funcSize - 1);
	MIPSAnalyst::RegisterFunction(funcBegin, funcSize, name.c_str());
	MIPSAnalyst::UpdateHashMap();
	MIPSAnalyst::ApplyHashMap();
	if (g_Config.bFuncReplacements) {
		MIPSAnalyst::ReplaceFunctions();
	}

	JsonWriter &json = req.Respond();
	json.writeUint("address", funcBegin);
	json.writeUint("size", funcSize);
	json.writeString("name", name);
}

// Auto-detect functions in a memory range (hle.func.scan)
//
// Parameters:
//  - address: unsigned integer address for the start of the range.
//  - size: unsigned integer size in bytes for scan.
//  - remove: optional bool indicating whether functions that intersect or inside lie inside the range must be removed before scanning
//
// Response (same event name) with no extra data.
void WebSocketHLEFuncScan(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;
	u32 size;
	if (!req.ParamU32("size", &size))
		return;

	bool remove = false;
	if (!req.ParamBool("remove", &remove, DebuggerParamType::OPTIONAL))
		return;

	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Address or size outside valid memory");

	if (remove) {
		RemoveFuncSymbolsInRange(addr, size);
	}

	bool insertSymbols = MIPSAnalyst::ScanForFunctions(addr, addr + size - 1, true);
	MIPSAnalyst::FinalizeScan(insertSymbols);

	req.Respond();
}

// List all known user modules (hle.module.list)
//
// No parameters.
//
// Response (same event name):
//  - modules: array of objects, each with properties:
//     - name: name of module when loaded.
//     - address: unsigned integer start address.
//     - size: unsigned integer size in bytes.
//     - isActive: boolean, true if this module is active.
void WebSocketHLEModuleList(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	auto modules = g_symbolMap->getAllModules();

	JsonWriter &json = req.Respond();
	json.pushArray("modules");
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

// Walk the stack and list stack frames (hle.backtrace)
//
// Parameters:
//  - thread: optional number indicating the thread id to backtrace, default current.
//
// Response (same event name):
//  - frames: array of objects, each with properties:
//     - entry: unsigned integer address of function start (may be estimated.)
//     - pc: unsigned integer next execution address.
//     - sp: unsigned integer stack address in this func (beware of alloca().)
//     - stackSize: integer size of stack frame.
//     - code: string disassembly of pc.
void WebSocketHLEBacktrace(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	uint32_t threadID = -1;
	DebugInterface *cpuDebug = currentDebugMIPS;
	if (req.HasParam("thread")) {
		if (!req.ParamU32("thread", &threadID))
			return;

		cpuDebug = KernelDebugThread((SceUID)threadID);
		if (!cpuDebug)
			return req.Fail("Thread could not be found");
	}

	auto threads = GetThreadsInfo();
	uint32_t entry = cpuDebug->GetPC();
	uint32_t stackTop = 0;
	for (const DebugThreadInfo &th : threads) {
		if ((threadID == -1 && th.isCurrent) || th.id == threadID) {
			entry = th.entrypoint;
			stackTop = th.initialStack;
			break;
		}
	}

	uint32_t ra = cpuDebug->GetRegValue(0, MIPS_REG_RA);
	uint32_t sp = cpuDebug->GetRegValue(0, MIPS_REG_SP);
	auto frames = MIPSStackWalk::Walk(cpuDebug->GetPC(), ra, sp, entry, stackTop);

	JsonWriter &json = req.Respond();
	json.pushArray("frames");
	for (auto f : frames) {
		json.pushDict();
		json.writeUint("entry", f.entry);
		json.writeUint("pc", f.pc);
		json.writeUint("sp", f.sp);
		json.writeUint("stackSize", f.stackSize);

		DisassemblyManager manager;
		DisassemblyLineInfo line;
		manager.getLine(manager.getStartAddress(f.pc), true, line, cpuDebug);
		json.writeString("code", line.name + " " + line.params);

		json.pop();
	}
	json.pop();
}
