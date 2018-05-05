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

#include "base/stringutil.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/WebSocket/SteppingSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSStackWalk.h"

using namespace MIPSAnalyst;

struct WebSocketSteppingState {
	WebSocketSteppingState() {
		disasm_.setCpu(currentDebugMIPS);
	}
	~WebSocketSteppingState() {
		disasm_.clear();
	}

	void Into(DebuggerRequest &req);
	void Over(DebuggerRequest &req);
	void Out(DebuggerRequest &req);
	void RunUntil(DebuggerRequest &req);
	void HLE(DebuggerRequest &req);

protected:
	u32 GetNextAddress();
	int GetNextInstructionCount();
	void PrepareResume();

	DisassemblyManager disasm_;
};

void *WebSocketSteppingInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketSteppingState();
	map["cpu.stepInto"] = std::bind(&WebSocketSteppingState::Into, p, std::placeholders::_1);
	map["cpu.stepOver"] = std::bind(&WebSocketSteppingState::Over, p, std::placeholders::_1);
	map["cpu.stepOut"] = std::bind(&WebSocketSteppingState::Out, p, std::placeholders::_1);
	map["cpu.runUntil"] = std::bind(&WebSocketSteppingState::RunUntil, p, std::placeholders::_1);
	map["cpu.nextHLE"] = std::bind(&WebSocketSteppingState::HLE, p, std::placeholders::_1);

	return p;
}

void WebSocketSteppingShutdown(void *p) {
	delete static_cast<WebSocketSteppingState *>(p);
}

// Single step into the next instruction (cpu.stepInto)
//
// No parameters.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::Into(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	if (!Core_IsStepping()) {
		Core_EnableStepping(true);
		return;
	}

	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);

	int c = GetNextInstructionCount();
	for (int i = 0; i < c; ++i) {
		Core_DoSingleStep();
	}
}

// Step over the next instruction (cpu.stepOver)
//
// Note: this jumps over function calls, but also delay slots.
//
// No parameters.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::Over(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	MipsOpcodeInfo info = GetOpcodeInfo(currentDebugMIPS, currentMIPS->pc);
	u32 breakpointAddress = GetNextAddress();
	if (info.isBranch) {
		if (info.isConditional && !info.isLinkedBranch) {
			if (info.conditionMet) {
				breakpointAddress = info.branchTarget;
			} else {
				// Skip over the delay slot.
				breakpointAddress += 4;
			}
		} else {
			if (info.isLinkedBranch) {
				// jal or jalr - a function call.  Skip the delay slot.
				breakpointAddress += 4;
			} else {
				// j - for absolute branches, set the breakpoint at the branch target.
				breakpointAddress = info.branchTarget;
			}
		}
	}

	PrepareResume();
	// Could have advanced to the breakpoint already in PrepareResume().
	if (currentMIPS->pc != breakpointAddress) {
		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		Core_EnableStepping(false);
	}
}

// Step out of a function based on a stack walk (cpu.stepOut)
//
// No parameters.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::Out(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	auto threads = GetThreadsInfo();
	u32 entry = currentMIPS->pc;
	u32 stackTop = 0;
	for (const DebugThreadInfo &th : threads) {
		if (th.isCurrent) {
			entry = th.entrypoint;
			stackTop = th.initialStack;
			break;
		}
	}

	auto frames = MIPSStackWalk::Walk(currentMIPS->pc, currentMIPS->r[MIPS_REG_RA], currentMIPS->r[MIPS_REG_SP], entry, stackTop);
	if (frames.size() < 2) {
		// TODO: Respond in some way?
		return;
	}

	u32 breakpointAddress = frames[1].pc;
	PrepareResume();
	// Could have advanced to the breakpoint already in PrepareResume().
	if (currentMIPS->pc != breakpointAddress) {
		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		Core_EnableStepping(false);
	}
}

// Run until a certain address (cpu.stepOut)
//
// Parameters:
//  - address: number parameter for destination.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::RunUntil(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	u32 address = 0;
	if (!req.ParamU32("address", &address)) {
		// Error already sent.
		return;
	}

	bool wasAtAddress = currentMIPS->pc == address;
	PrepareResume();
	// We may have arrived already if PauseResume() stepped out of a delay slot.
	if (currentMIPS->pc != address || wasAtAddress) {
		CBreakPoints::AddBreakPoint(address, true);
		Core_EnableStepping(false);
	}
}

// Jump after the next HLE call (cpu.nextHLE)
//
// No parameters.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::HLE(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	PrepareResume();
	hleDebugBreak();
	Core_EnableStepping(false);
}

u32 WebSocketSteppingState::GetNextAddress() {
	u32 current = disasm_.getStartAddress(currentMIPS->pc);
	return disasm_.getNthNextAddress(current, 1);
}

int WebSocketSteppingState::GetNextInstructionCount() {
	return (GetNextAddress() - currentMIPS->pc) / 4;
}

void WebSocketSteppingState::PrepareResume() {
	if (currentMIPS->inDelaySlot) {
		Core_DoSingleStep();
	} else {
		// If the current PC is on a breakpoint, the user doesn't want to do nothing.
		CBreakPoints::SetSkipFirst(currentMIPS->pc);
	}
}

