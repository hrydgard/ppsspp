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
#include "Common/Math/math_util.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Common/File/FileUtil.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/LineInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/WebSocket/HLESubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSStackWalk.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"

// General note: Function addresses will be snapped appropriately to full instructions
// if they are not divisible by four. Addresses downwards, sizes upwards.
// It's recommended to use correctly aligned addresses instead.

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
	map["hle.module.saveSymbols"] = &WebSocketHLEModuleSaveSymbols;
	map["hle.module.loadSymbols"] = &WebSocketHLEModuleLoadSymbols;
	map["hle.game.saveSymbols"] = &WebSocketHLEGameSaveSymbols;
	map["hle.game.loadSymbols"] = &WebSocketHLEGameLoadSymbols;
	map["hle.backtrace"] = &WebSocketHLEBacktrace;
	map["hle.data.list"] = &WebSocketHLEDataList;
	map["hle.data.add"] = &WebSocketHLEDataAdd;
	map["hle.data.remove"] = &WebSocketHLEDataRemove;
	map["hle.data.rename"] = &WebSocketHLEDataRename;

	return nullptr;
}

static const char *DataTypeToString(DataType type) {
	switch (type) {
	case DATATYPE_BYTE: return "byte";
	case DATATYPE_HALFWORD: return "halfword";
	case DATATYPE_WORD: return "word";
	case DATATYPE_ASCII: return "ascii";
	default: return "unknown";
	}
}

static bool DataTypeFromString(const std::string &s, DataType *out) {
	if (s == "byte") {
		*out = DATATYPE_BYTE;
	} else if (s == "halfword") {
		*out = DATATYPE_HALFWORD;
	} else if (s == "word") {
		*out = DATATYPE_WORD;
	} else if (s == "ascii") {
		*out = DATATYPE_ASCII;
	} else {
		return false;
	}
	return true;
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
//     - initialStack: unsigned integer, address of the base of the thread's stack.
//     - currentStackSize: unsigned integer, size of stack (e.g. if resized.)
//     - priority: numeric priority level, lower values are better priority.
//     - waitType: numeric wait type, if the thread is waiting, or 0 if not waiting.
//     - isCurrent: boolean, true for the currently executing thread.
void WebSocketHLEThreadList(DebuggerRequest &req) {
	// Route the actual kernel thread reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		// Will just return none of the CPU isn't ready yet.
		std::vector<DebugThreadInfo> threads = GetThreadsInfo();

		JsonWriter &json = req.Respond();
		json.pushArray("threads");
		for (const DebugThreadInfo &th : threads) {
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
			// Named after the SceKernelThreadInfo field it comes from - it's the stack base
			// address, not a size, despite what this used to be called.
			json.writeUint("initialStack", th.initialStack);
			json.writeUint("currentStackSize", th.stackSize);
			json.writeInt("priority", th.priority);
			json.writeInt("waitType", (int)th.waitType);
			json.writeBool("isCurrent", th.isCurrent);
			json.pop();
		}
		json.pop();
	});
}

static bool ThreadInfoForStatus(DebuggerRequest &req, DebugThreadInfo *result) {
	if (PSP_GetBootState() != BootState::Complete) {
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

	std::vector<DebugThreadInfo> threads = GetThreadsInfo();
	for (const DebugThreadInfo &t : threads) {
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
	// Route the actual kernel thread manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		DebugThreadInfo threadInfo{ -1 };
		if (!ThreadInfoForStatus(req, &threadInfo))
			return;

		switch (threadInfo.status) {
		case THREADSTATUS_SUSPEND:
		case THREADSTATUS_WAIT:
		case THREADSTATUS_WAITSUSPEND:
			if (__KernelResumeThreadFromWait(threadInfo.id, 0) != 0) {
				req.Fail("Failed to resume thread");
				return;
			}
			break;

		default:
			req.Fail("Cannot force run thread based on current status");
			return;
		}

		Reporting::NotifyDebugger();

		JsonWriter &json = req.Respond();
		json.writeUint("thread", threadInfo.id);
		json.writeString("status", "ready");
	});
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
	// Route the actual kernel thread manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
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
			req.Fail("Cannot force stop thread based on current status");
			return;
		}

		// Get it again to verify.
		if (!ThreadInfoForStatus(req, &threadInfo))
			return;
		if ((threadInfo.status & THREADSTATUS_DORMANT) == 0) {
			req.Fail("Failed to stop thread");
			return;
		}

		Reporting::NotifyDebugger();

		JsonWriter &json = req.Respond();
		json.writeUint("thread", threadInfo.id);
		json.writeString("status", "dormant");
	});
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

	// Route the actual symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		std::vector<SymbolEntry> functions = g_symbolMap->GetAllActiveSymbols(ST_FUNCTION);

		JsonWriter &json = req.Respond();
		json.pushArray("functions");
		for (const SymbolEntry &f : functions) {
			json.pushDict();
			json.writeString("name", f.name);
			json.writeUint("address", f.address);
			json.writeUint("size", f.size);
			json.pop();
		}
		json.pop();
	});
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

	addr = RoundDownToMultipleOf(addr, 4);
	size = RoundUpToMultipleOf(size, 4);

	std::string name;
	if (!req.ParamString("name", &name, DebuggerParamType::OPTIONAL))
		return;
	if (name.empty())
		name = StringFromFormat("z_un_%08x", addr);

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 prevBegin = g_symbolMap->GetFunctionStart(addr);
		u32 endBegin = size == -1 ? prevBegin : g_symbolMap->GetFunctionStart(addr + size - 4);
		if (prevBegin == addr) {
			req.Fail("Function already exists at 'address'");
			return;
		} else if (endBegin != prevBegin) {
			req.Fail("Function already exists between 'address' and 'address' + 'size'");
			return;
		} else if (prevBegin != -1) {
			std::string prevName = g_symbolMap->GetLabelString(prevBegin);
			u32 prevSize = g_symbolMap->GetFunctionSize(prevBegin);
			u32 newPrevSize = addr - prevBegin;

			// The new function will be the remainder, unless otherwise specified.
			if (size == -1)
				size = prevSize - newPrevSize;

			// Make sure we register the new length for replacements too.
			MIPSAnalyst::ForgetFunctions(prevBegin, prevBegin + newPrevSize);
			g_symbolMap->SetFunctionSize(prevBegin, newPrevSize);
			MIPSAnalyst::RegisterFunction(prevBegin, newPrevSize, prevName.c_str());
		} else {
			// There was no function there, so hopefully they specified a size.
			if (size == -1)
				size = 4;
		}

		// To ensure we restore replacements.
		MIPSAnalyst::ForgetFunctions(addr, addr + size);
		g_symbolMap->AddFunction(name.c_str(), addr, size);
		g_symbolMap->SortSymbols();
		MIPSAnalyst::RegisterFunction(addr, size, name.c_str());

		MIPSAnalyst::UpdateHashMap();
		MIPSAnalyst::ApplyHashMap();

		if (g_Config.bFuncReplacements) {
			MIPSAnalyst::ReplaceFunctions();
		}

		// Clear cache for branch lines and such.
		g_disassemblyManager.clear();

		JsonWriter &json = req.Respond();
		json.writeUint("address", addr);
		json.writeUint("size", size);
		json.writeString("name", name);
	});
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

	addr = RoundDownToMultipleOf(addr, 4);

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 funcBegin = g_symbolMap->GetFunctionStart(addr);
		if (funcBegin == -1) {
			req.Fail("No function found at 'address'");
			return;
		}
		u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin);

		// Expand the previous function.
		u32 prevBegin = g_symbolMap->GetFunctionStart(funcBegin - 1);
		if (prevBegin != -1) {
			std::string prevName = g_symbolMap->GetLabelString(prevBegin);
			u32 expandedSize = g_symbolMap->GetFunctionSize(prevBegin) + funcSize;
			g_symbolMap->SetFunctionSize(prevBegin, expandedSize);
			MIPSAnalyst::ForgetFunctions(prevBegin, prevBegin + expandedSize);
			MIPSAnalyst::RegisterFunction(prevBegin, expandedSize, prevName.c_str());
		} else {
			MIPSAnalyst::ForgetFunctions(funcBegin, funcBegin + funcSize);
		}

		g_symbolMap->RemoveFunction(funcBegin, true);
		g_symbolMap->SortSymbols();

		MIPSAnalyst::UpdateHashMap();
		MIPSAnalyst::ApplyHashMap();

		if (g_Config.bFuncReplacements) {
			MIPSAnalyst::ReplaceFunctions();
		}

		// Clear cache for branch lines and such.
		g_disassemblyManager.clear();

		JsonWriter &json = req.Respond();
		json.writeUint("address", funcBegin);
		json.writeUint("size", funcSize);
	});
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
		MIPSAnalyst::ForgetFunctions(addr, addr + size);

		// The following was copied from hle.func.remove:
		g_symbolMap->SortSymbols();

		MIPSAnalyst::UpdateHashMap();
		MIPSAnalyst::ApplyHashMap();

		if (g_Config.bFuncReplacements) {
			MIPSAnalyst::ReplaceFunctions();
		}

		// Clear cache for branch lines and such.
		g_disassemblyManager.clear();
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

	addr = RoundDownToMultipleOf(addr, 4);
	size = RoundUpToMultipleOf(size, 4);

	// This only depends on addr/size, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Address or size outside valid memory");

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 count = RemoveFuncSymbolsInRange(addr, size);

		JsonWriter &json = req.Respond();
		json.writeUint("count", count);
	});
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

	addr = RoundDownToMultipleOf(addr, 4);

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 funcBegin = g_symbolMap->GetFunctionStart(addr);
		if (funcBegin == -1) {
			req.Fail("No function found at 'address'");
			return;
		}
		u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin);

		g_symbolMap->SetLabelName(name.c_str(), funcBegin);
		// To ensure we reapply replacements (in case we check name there.)
		MIPSAnalyst::ForgetFunctions(funcBegin, funcBegin + funcSize);
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
	});
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

	addr = RoundDownToMultipleOf(addr, 4);
	size = RoundUpToMultipleOf(size, 4);

	bool remove = false;
	if (!req.ParamBool("remove", &remove, DebuggerParamType::OPTIONAL))
		return;

	// This only depends on addr/size, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Address or size outside valid memory");

	// Route the actual symbol scan/manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	// Note: like memory.search, 'size' has no cap beyond valid memory range, so a very large scan
	// will block the CPU thread's own frame pump for its duration.
	Core_RunOnCPUThread([&] {

		if (remove) {
			RemoveFuncSymbolsInRange(addr, size);
		}

		bool insertSymbols = MIPSAnalyst::ScanForFunctions(addr, addr + size, true);
		MIPSAnalyst::FinalizeScan(insertSymbols);

		req.Respond();
	});
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

	// Route the actual symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		std::vector<LoadedModuleInfo> modules = g_symbolMap->getAllModules();

		JsonWriter &json = req.Respond();
		json.pushArray("modules");
		for (const LoadedModuleInfo &m : modules) {
			json.pushDict();
			json.writeString("name", m.name);
			json.writeUint("address", m.address);
			json.writeUint("size", m.size);
			json.writeBool("isActive", m.active);
			json.pop();
		}
		json.pop();
	});
}

// Save one module's symbols to its standard per-module file (hle.module.saveSymbols)
//
// Saves to <memstick>/PSP/SYSTEM/SYMBOLS/<moduleName>_<crc>.ppsym - see
// SymbolMap::GetModuleSymbolsPath. Keyed by module name+crc rather than the current game, so
// the file is shared by every game/homebrew that happens to load the exact same module. This is
// the same file LoadModuleSymbols/hle.module.loadSymbols reads, and (if
// g_Config.bAutoSaveLoadSymbols is on - see Core/HLE/sceKernelModule.cpp) the same file
// auto-save-on-unload writes and auto-load-on-module-load reads.
//
// Parameters:
//  - name: string, name of the module to save (as returned by hle.module.list.)
//
// Response (same event name):
//  - path: string, the file path that was written.
void WebSocketHLEModuleSaveSymbols(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	std::string name;
	if (!req.ParamString("name", &name))
		return;

	// Route the actual symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		int moduleIndex = g_symbolMap->GetModuleIndexByName(name);
		if (moduleIndex <= 0) {
			req.Fail("No module found with that name");
			return;
		}

		Path path = SymbolMap::GetModuleSymbolsPath(name, g_symbolMap->GetModuleCrc(moduleIndex));
		if (!g_symbolMap->SaveModuleSymbols(moduleIndex, path, g_paramSFO.GetDiscID(), g_paramSFO.GetValueString("TITLE"))) {
			req.Fail("Failed to save symbols file");
			return;
		}

		JsonWriter &json = req.Respond();
		json.writeString("path", path.ToString());
	});
}

// Load a module's previously saved symbols (hle.module.loadSymbols)
//
// Reads from the same standard path hle.module.saveSymbols writes - see its docs above.
// Existing symbol names for this module are overwritten by what's in the file.
//
// Parameters:
//  - name: string, name of the module to load into (must currently be loaded - see
//    hle.module.list.)
//
// Response (same event name):
//  - path: string, the file path that was read.
void WebSocketHLEModuleLoadSymbols(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	std::string name;
	if (!req.ParamString("name", &name))
		return;

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		int moduleIndex = g_symbolMap->GetModuleIndexByName(name);
		if (moduleIndex <= 0 || !g_symbolMap->IsModuleActive(moduleIndex)) {
			req.Fail("No active module found with that name");
			return;
		}

		Path path = SymbolMap::GetModuleSymbolsPath(name, g_symbolMap->GetModuleCrc(moduleIndex));
		if (!g_symbolMap->LoadModuleSymbols(moduleIndex, path)) {
			req.Fail("Failed to load symbols file (does it exist?)");
			return;
		}

		// Clear cache so the disassembly view picks up the newly loaded names.
		g_disassemblyManager.clear();

		JsonWriter &json = req.Respond();
		json.writeString("path", path.ToString());
	});
}

// Save the symbols that aren't inside any module (hle.game.saveSymbols)
//
// The counterpart to hle.module.saveSymbols for everything the user labelled outside a module -
// heap, stack, scratchpad, hardware registers. Those describe this game's own memory layout and
// mean nothing to another game, so they go to a per-game file rather than a shared per-module one:
// <memstick>/PSP/SYSTEM/SYMBOLS/<gameID>_syms.ppsym. See SymbolMap::GetGameSymbolsPath.
//
// Parameters: none.
//
// Response (same event name):
//  - path: string, the file path that was written.
//  - saved: boolean, false if there were no such symbols to save (any previous file is removed.)
void WebSocketHLEGameSaveSymbols(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	// Route the actual symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		const Path path = SymbolMap::GetGameSymbolsPath(g_paramSFO.GetDiscID());
		if (!g_symbolMap->SaveModuleSymbols(0, path, g_paramSFO.GetDiscID(), g_paramSFO.GetValueString("TITLE"))) {
			req.Fail("Failed to save symbols file");
			return;
		}

		JsonWriter &json = req.Respond();
		json.writeString("path", path.ToString());
		// False when there was nothing to save - any previous file has been removed.
		json.writeBool("saved", File::Exists(path));
	});
}

// Load the symbols that aren't inside any module (hle.game.loadSymbols)
//
// Reads back what hle.game.saveSymbols wrote - see its docs above. Existing names at those
// addresses are overwritten by what's in the file.
//
// Parameters: none.
//
// Response (same event name):
//  - path: string, the file path that was read.
void WebSocketHLEGameLoadSymbols(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		const Path path = SymbolMap::GetGameSymbolsPath(g_paramSFO.GetDiscID());
		if (!g_symbolMap->LoadModuleSymbols(0, path)) {
			req.Fail("Failed to load symbols file (does it exist?)");
			return;
		}

		// Clear cache so the disassembly view picks up the newly loaded names.
		g_disassemblyManager.clear();

		JsonWriter &json = req.Respond();
		json.writeString("path", path.ToString());
	});
}

// Walk the stack and list stack frames (hle.backtrace)
//
// Parameters:
//  - thread: optional number indicating the thread id to backtrace, default current.
//
// Response (same event name):
//  - walked: boolean, false if the stack walk failed and the frames below are the fallback
//    described under 'frames'.
//  - frames: array of objects, each with properties:
//     - entry: unsigned integer address of function start (may be estimated.)
//     - pc: unsigned integer next execution address.
//     - sp: unsigned integer stack address in this func (beware of alloca().)
//     - stackSize: integer size of stack frame.
//     - code: string disassembly of pc, empty if pc isn't readable.
//     - file: string source file name, or null when there's no line info for this address.
//     - line: integer source line number, or null. Only ever available when the game shipped an
//       unstripped ELF - PRX conversion drops DWARF, so this is a homebrew-only luxury.
//
// A real stack walk needs to recognize the function it starts in, so it comes back empty exactly
// when execution has gone somewhere unexpected - a jump through a bad pointer, say - which is
// when a backtrace is most wanted. In that case this falls back to the two things that are still
// known: the current pc, and ra, which for a botched call still holds the return address and so
// points at the instruction after the call site. Those frames have "walked": false, and there is
// no guarantee ra hasn't already been overwritten - it's a lead, not a stack walk.
void WebSocketHLEBacktrace(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	// Route the actual CPU/symbol/disassembly reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		uint32_t threadID = -1;
		DebugInterface *cpuDebug = currentDebugMIPS;
		if (req.HasParam("thread")) {
			if (!req.ParamU32("thread", &threadID))
				return;

			cpuDebug = KernelDebugThread((SceUID)threadID);
			if (!cpuDebug) {
				req.Fail("Thread could not be found");
				return;
			}
		}

		std::vector<DebugThreadInfo> threads = GetThreadsInfo();
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
		std::vector<MIPSStackWalk::StackFrame> frames = MIPSStackWalk::Walk(cpuDebug->GetPC(), ra, sp, entry, stackTop);

		// See the note above the function: a failed walk is the case that matters most, so rather
		// than an empty array, hand back what can still be salvaged.
		const bool walked = !frames.empty();
		if (!walked) {
			auto addFrame = [&](uint32_t pc) {
				MIPSStackWalk::StackFrame f{};
				f.pc = pc;
				f.sp = sp;
				const uint32_t start = g_symbolMap->GetFunctionStart(pc);
				f.entry = start == SymbolMap::INVALID_ADDRESS ? pc : start;
				frames.push_back(f);
			};
			addFrame(cpuDebug->GetPC());
			if (ra != cpuDebug->GetPC() && Memory::IsValidAddress(ra))
				addFrame(ra);
		}

		JsonWriter &json = req.Respond();
		json.writeBool("walked", walked);
		json.pushArray("frames");
		for (const MIPSStackWalk::StackFrame &f : frames) {
			json.pushDict();
			json.writeUint("entry", f.entry);
			json.writeUint("pc", f.pc);
			json.writeUint("sp", f.sp);
			json.writeUint("stackSize", f.stackSize);

			// The fallback frames can point at unmapped memory - that's the whole reason we're
			// here - so don't ask the disassembler to read it.
			if (Memory::IsValidAddress(f.pc)) {
				DisassemblyLineInfo line;
				g_disassemblyManager.getLine(g_disassemblyManager.getStartAddress(f.pc), true, line, cpuDebug);
				json.writeString("code", line.name + " " + line.params);
			} else {
				json.writeString("code", "");
			}

			// Only present when the game shipped an unstripped ELF to read DWARF out of, which in
			// practice means homebrew - see Core/Debugger/LineInfo.h. A backtrace is where this
			// pays off most: four addresses versus four source locations.
			std::string file;
			int line = 0;
			if (g_lineInfo.Lookup(f.pc, &file, &line)) {
				json.writeString("file", file);
				json.writeInt("line", line);
			} else {
				json.writeNull("file");
				json.writeNull("line");
			}

			json.pop();
		}
		json.pop();
	});
}

// List all current known data symbols (hle.data.list)
//
// No parameters.
//
// Response (same event name):
//  - data: array of objects, each with properties:
//     - name: current name of the data symbol.
//     - address: unsigned integer start address.
//     - size: unsigned integer size in bytes.
//     - type: string, one of 'byte', 'halfword', 'word', 'ascii', or 'unknown'.
void WebSocketHLEDataList(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");

	// Route the actual symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		std::vector<SymbolEntry> entries = g_symbolMap->GetAllActiveSymbols(ST_DATA);

		JsonWriter &json = req.Respond();
		json.pushArray("data");
		for (const SymbolEntry &d : entries) {
			json.pushDict();
			json.writeString("name", d.name);
			json.writeUint("address", d.address);
			json.writeUint("size", d.size);
			json.writeString("type", DataTypeToString(g_symbolMap->GetDataType(d.address)));
			json.pop();
		}
		json.pop();
	});
}

// Add a new data symbol (hle.data.add)
//
// Useful for labeling structures, tables, or buffers found while reverse engineering
// (e.g. after locating something with memory.search.)
//
// Parameters:
//  - address: unsigned integer address for the start of the data.
//  - size: unsigned integer size in bytes.
//  - type: string, one of 'byte', 'halfword', 'word', or 'ascii'.
//  - name: string to name the data, optional and defaults to an auto-generated name.
//
// Response (same event name):
//  - address: the start address, repeated back.
//  - size: the size, repeated back.
//  - type: the type, repeated back.
//  - name: name the data symbol actually ended up with.  Normally the requested one, but a
//    function starting at this address keeps its own name (labels are shared between the two),
//    so check this rather than assuming the request was applied verbatim.
void WebSocketHLEDataAdd(DebuggerRequest &req) {
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
	if (size == 0)
		return req.Fail("'size' must not be zero");

	std::string typeStr;
	if (!req.ParamString("type", &typeStr))
		return;
	DataType type;
	if (!DataTypeFromString(typeStr, &type))
		return req.Fail("Invalid 'type', must be byte, halfword, word, or ascii");

	// This only depends on addr/size, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Address or size outside valid memory");

	std::string name;
	if (!req.ParamString("name", &name, DebuggerParamType::OPTIONAL))
		return;
	if (name.empty())
		name = StringFromFormat("data_%08x", addr);

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		// -1 lets SymbolMap work the module out from the address, landing on module 0 ("no
		// module, absolute address") for a label the user put on the heap, the stack or
		// scratchpad after e.g. a memory.search.
		const int moduleIndex = -1;

		// Labels are intentionally a single namespace shared by function and data symbols, and AddLabel() won't
		// overwrite an existing one - a real ELF symbol name shouldn't lose to the analyzer's later z_un_*.
		// However it's wrong here, where someone is explicitly naming this address.
		// So force the requested name in afterwards, unless a function starts here and owns the label:
		// silently renaming that function isn't what "label this data" should do, and it would undo the same care taken in hle.data.remove.
		const bool functionOwnsLabel = g_symbolMap->GetFunctionStart(addr) == addr;

		g_symbolMap->AddData(addr, size, type, moduleIndex);
		g_symbolMap->AddLabel(name.c_str(), addr, moduleIndex);
		if (!functionOwnsLabel)
			g_symbolMap->SetLabelName(name.c_str(), addr);
		g_symbolMap->SortSymbols();

		// Clear cache so the disassembly view picks up the new annotation.
		g_disassemblyManager.clear();

		// Report the name that actually ended up in the map, not the one that was asked for.
		const std::string actualName = g_symbolMap->GetLabelString(addr);

		JsonWriter &json = req.Respond();
		json.writeUint("address", addr);
		json.writeUint("size", size);
		json.writeString("type", typeStr);
		json.writeString("name", actualName.empty() ? name : actualName);
	});
}

// Remove a data symbol (hle.data.remove)
//
// Parameters:
//  - address: unsigned integer address within the data symbol to remove.
//
// Response (same event name):
//  - address: the start address of the removed data symbol.
//  - size: the size in bytes of the removed data symbol.
void WebSocketHLEDataRemove(DebuggerRequest &req) {
	if (!g_symbolMap)
		return req.Fail("CPU not active");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	u32 addr;
	if (!req.ParamU32("address", &addr))
		return;

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 dataBegin = g_symbolMap->GetDataStart(addr);
		if (dataBegin == -1) {
			req.Fail("No data symbol found at 'address'");
			return;
		}
		u32 dataSize = g_symbolMap->GetDataSize(dataBegin);

		// Labels are shared between data and function symbols, so dropping the label along with the
		// data would also wipe the name of a function starting at the same address (leaving it
		// showing up in hle.func.list with an empty name). Only take the label with us if no
		// function is using it too.
		const bool functionOwnsLabel = g_symbolMap->GetFunctionStart(dataBegin) == dataBegin;
		g_symbolMap->RemoveData(dataBegin, !functionOwnsLabel);
		g_symbolMap->SortSymbols();
		g_disassemblyManager.clear();

		JsonWriter &json = req.Respond();
		json.writeUint("address", dataBegin);
		json.writeUint("size", dataSize);
	});
}

// Rename a data symbol (hle.data.rename)
//
// Parameters:
//  - address: unsigned integer address within the data symbol to rename.
//  - name: string, new name for the data symbol.
//
// Response (same event name):
//  - address: the start address of the renamed data symbol.
//  - size: the size in bytes of the renamed data symbol.
//  - name: string, new name repeated back.
void WebSocketHLEDataRename(DebuggerRequest &req) {
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

	// Route the actual symbol manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		u32 dataBegin = g_symbolMap->GetDataStart(addr);
		if (dataBegin == -1) {
			req.Fail("No data symbol found at 'address'");
			return;
		}
		u32 dataSize = g_symbolMap->GetDataSize(dataBegin);

		g_symbolMap->SetLabelName(name.c_str(), dataBegin);

		JsonWriter &json = req.Respond();
		json.writeUint("address", dataBegin);
		json.writeUint("size", dataSize);
		json.writeString("name", name);
	});
}
