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

#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/Debugger/WebSocket/MIPSTracerSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTracer.h"
#include "Core/System.h"

DebuggerSubscriber *WebSocketMIPSTracerInit(DebuggerEventHandlerMap &map) {
	map["cpu.tracer.status"] = &WebSocketMIPSTracerStatus;
	map["cpu.tracer.start"] = &WebSocketMIPSTracerStart;
	map["cpu.tracer.stop"] = &WebSocketMIPSTracerStop;
	map["cpu.tracer.flush"] = &WebSocketMIPSTracerFlush;
	map["cpu.tracer.clear"] = &WebSocketMIPSTracerClear;

	return nullptr;
}

// The tracer records blocks from the IR frontend (IRFrontend::DoJit inserts the LogIRBlock
// instruction), so only the two IR-based cores feed it. Under the plain interpreter or the
// native JIT it stays silent, which from a client's side is indistinguishable from "nothing
// executed" - so say so up front instead.
static bool TracerCoreIsIR() {
	const CPUCore core = (CPUCore)g_Config.iCpuCore;
	return core == CPUCore::IR_INTERPRETER || core == CPUCore::JIT_IR;
}

static const char *CPUCoreName(CPUCore core) {
	switch (core) {
	case CPUCore::INTERPRETER: return "interpreter";
	case CPUCore::JIT: return "jit";
	case CPUCore::IR_INTERPRETER: return "ir";
	case CPUCore::JIT_IR: return "jit-ir";
	default: return "unknown";
	}
}

static void WriteStatus(JsonWriter &json) {
	json.writeBool("tracing", mipsTracer.tracing_enabled);
	json.writeString("path", mipsTracer.get_logging_path());
	json.writeUint("storageCapacity", (u32)mipsTracer.in_storage_capacity);
	json.writeUint("maxTraceSize", (u32)mipsTracer.in_max_trace_size);
	// How much of each ring has been used, so a client can tell "the trace covers everything
	// since I started" from "the oldest entries have already been overwritten".
	json.writeUint("blocksRecorded", mipsTracer.executed_blocks.current_index);
	json.writeBool("traceOverflowed", mipsTracer.executed_blocks.overflow);
	json.writeUint("distinctBlocks", (u32)mipsTracer.trace_info.size());
	json.writeUint("storageUsed", mipsTracer.storage.cur_index);
	json.writeBool("supported", TracerCoreIsIR());
	json.writeString("cpuCore", CPUCoreName((CPUCore)g_Config.iCpuCore));
}

// Report tracer state (cpu.tracer.status)
//
// No parameters.
//
// Response (same event name):
//  - tracing: boolean, whether recording is currently on.
//  - path: string, file cpu.tracer.flush writes to ("" if never set).
//  - storageCapacity: unsigned integer, instruction words the block storage holds.
//  - maxTraceSize: unsigned integer, block entries the trace ring holds.
//  - blocksRecorded: unsigned integer, entries written to the trace ring.
//  - traceOverflowed: boolean, true once the ring wrapped and the oldest entries were lost.
//  - distinctBlocks: unsigned integer, distinct basic blocks seen.
//  - storageUsed: unsigned integer, instruction words of block storage used.
//  - supported: boolean, whether the current CPU core feeds the tracer (IR cores only.)
//  - cpuCore: string, one of 'interpreter', 'jit', 'ir', 'jit-ir'.
void WebSocketMIPSTracerStatus(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	WriteStatus(json);
}

// Start recording a trace (cpu.tracer.start)
//
// Parameters:
//  - storageCapacity: optional unsigned integer, instruction words to reserve for block storage.
//  - maxTraceSize: optional unsigned integer, how many block entries the trace ring holds. The
//    ring is cyclic, so once it fills the oldest entries are dropped and the trace covers the
//    most recent maxTraceSize blocks - which is usually what you want when tracing up to a crash.
//  - clearJit: optional boolean, default true. Blocks already compiled don't carry the tracer's
//    LogIRBlock instruction, so without dropping them a hot loop that was compiled before this
//    call never shows up. Pass false only if you know the code you care about isn't compiled yet.
//
// Response (same event name): the same fields as cpu.tracer.status.
//
// Note: starting resizes and clears the buffers, so any trace not yet flushed is lost.
void WebSocketMIPSTracerStart(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("CPU not started");
	if (!TracerCoreIsIR()) {
		return req.Fail(StringFromFormat(
			"The MIPS tracer only records under an IR core, but the current core is '%s' - "
			"start PPSSPP with --cpu=ir (or --cpu=jit-ir) to use it",
			CPUCoreName((CPUCore)g_Config.iCpuCore)));
	}

	uint32_t storageCapacity = (uint32_t)mipsTracer.in_storage_capacity;
	uint32_t maxTraceSize = (uint32_t)mipsTracer.in_max_trace_size;
	if (!req.ParamU32("storageCapacity", &storageCapacity, false, DebuggerParamType::OPTIONAL))
		return;
	if (!req.ParamU32("maxTraceSize", &maxTraceSize, false, DebuggerParamType::OPTIONAL))
		return;
	if (storageCapacity == 0 || maxTraceSize == 0)
		return req.Fail("Parameters 'storageCapacity' and 'maxTraceSize' must be non-zero");

	bool clearJit = true;
	if (!req.ParamBool("clearJit", &clearJit, DebuggerParamType::OPTIONAL))
		return;

	// Resizing the buffers and flipping the flag both race the CPU thread otherwise - see
	// docs/DebuggerThreading.md.
	Core_RunOnCPUThread([&] {
		mipsTracer.in_storage_capacity = (int)storageCapacity;
		mipsTracer.in_max_trace_size = (int)maxTraceSize;
		mipsTracer.initialize(storageCapacity, maxTraceSize);
		mipsTracer.start_tracing();
		if (clearJit) {
			currentMIPS->ClearJitCacheDeferred();
		}

		JsonWriter &json = req.Respond();
		WriteStatus(json);
	});
}

// Stop recording (cpu.tracer.stop)
//
// No parameters. Stopping keeps whatever has been recorded, so cpu.tracer.flush still works
// afterwards.
//
// Response (same event name): the same fields as cpu.tracer.status.
void WebSocketMIPSTracerStop(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		mipsTracer.stop_tracing();

		JsonWriter &json = req.Respond();
		WriteStatus(json);
	});
}

// Write the recorded trace to a file (cpu.tracer.flush)
//
// Parameters:
//  - path: optional string, file to write to. Remembered for later flushes; required the first
//    time, since the tracer has no default.
//
// Response (same event name):
//  - path: string, the file written.
//  - plus the same fields as cpu.tracer.status, as they stand after the flush.
//
// Note: a successful flush clears the tracer (MIPSTracer::flush_to_file does), so the next trace
// starts empty. Recording is not stopped - call cpu.tracer.stop first for a stable snapshot.
void WebSocketMIPSTracerFlush(DebuggerRequest &req) {
	std::string path;
	if (req.HasParam("path")) {
		if (!req.ParamString("path", &path, DebuggerParamType::OPTIONAL))
			return;
		if (path.empty())
			return req.Fail("Parameter 'path' must not be empty");
	} else if (mipsTracer.get_logging_path().empty()) {
		return req.Fail("No trace path set - pass 'path' at least once");
	}

	std::string error;
	Core_RunOnCPUThread([&] {
		if (!path.empty())
			mipsTracer.set_logging_path(path);
		if (!mipsTracer.flush_to_file()) {
			// flush_to_file logs the specific reason (bad path, couldn't open) to Log::JIT.
			error = "Couldn't write the trace to '" + mipsTracer.get_logging_path() + "'";
			return;
		}

		JsonWriter &json = req.Respond();
		WriteStatus(json);
	});

	if (!error.empty())
		req.Fail(error);
}

// Discard the recorded trace (cpu.tracer.clear)
//
// No parameters. Leaves recording on if it was on, so this is how to drop everything up to now
// and keep tracing from here.
//
// Response (same event name): the same fields as cpu.tracer.status.
void WebSocketMIPSTracerClear(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		mipsTracer.clear();

		JsonWriter &json = req.Respond();
		WriteStatus(json);
	});
}
