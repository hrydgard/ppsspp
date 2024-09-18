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

struct WebSocketSteppingState : public DebuggerSubscriber {
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
	uint32_t GetNextAddress(DebugInterface *cpuDebug);
	int GetNextInstructionCount(DebugInterface *cpuDebug);
	void PrepareResume();
	void AddThreadCondition(uint32_t breakpointAddress, uint32_t threadID);

	DisassemblyManager disasm_;
};

DebuggerSubscriber *WebSocketSteppingInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketSteppingState();
	map["cpu.stepInto"] = std::bind(&WebSocketSteppingState::Into, p, std::placeholders::_1);
	map["cpu.stepOver"] = std::bind(&WebSocketSteppingState::Over, p, std::placeholders::_1);
	map["cpu.stepOut"] = std::bind(&WebSocketSteppingState::Out, p, std::placeholders::_1);
	map["cpu.runUntil"] = std::bind(&WebSocketSteppingState::RunUntil, p, std::placeholders::_1);
	map["cpu.nextHLE"] = std::bind(&WebSocketSteppingState::HLE, p, std::placeholders::_1);

	return p;
}

static DebugInterface *CPUFromRequest(DebuggerRequest &req, uint32_t *threadID = nullptr) {
	if (!req.HasParam("thread")) {
		if (threadID)
			*threadID = -1;
		return currentDebugMIPS;
	}

	uint32_t uid;
	if (!req.ParamU32("thread", &uid))
		return nullptr;

	DebugInterface *cpuDebug = KernelDebugThread((SceUID)uid);
	if (!cpuDebug)
		req.Fail("Thread could not be found");
	if (threadID)
		*threadID = uid;
	return cpuDebug;
}

// Single step into the next instruction (cpu.stepInto)
//
// Parameters:
//  - thread: optional number indicating the thread id to plan stepping on.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Into(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping()) {
		Core_EnableStepping(true, "cpu.stepInto", 0);
		return;
	}

	uint32_t threadID;
	auto cpuDebug = CPUFromRequest(req, &threadID);
	if (!cpuDebug)
		return;

	if (cpuDebug == currentDebugMIPS) {
		// If the current PC is on a breakpoint, the user doesn't want to do nothing.
		CBreakPoints::SetSkipFirst(currentMIPS->pc);

		int c = GetNextInstructionCount(cpuDebug);
		for (int i = 0; i < c; ++i) {
			Core_DoSingleStep();
		}
	} else {
		uint32_t breakpointAddress = cpuDebug->GetPC();
		PrepareResume();
		// Could have advanced to the breakpoint already in PrepareResume().
		// Note: we need to get cpuDebug again anyway (in case we ran some HLE above.)
		cpuDebug = CPUFromRequest(req);
		if (cpuDebug != currentDebugMIPS) {
			CBreakPoints::AddBreakPoint(breakpointAddress, true);
			AddThreadCondition(breakpointAddress, threadID);
			Core_EnableStepping(false);
		}
	}
}

// Step over the next instruction (cpu.stepOver)
//
// Note: this jumps over function calls, but also delay slots.
//
// Parameters:
//  - thread: optional number indicating the thread id to plan stepping on.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Over(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	uint32_t threadID;
	auto cpuDebug = CPUFromRequest(req, &threadID);
	if (!cpuDebug)
		return;

	MipsOpcodeInfo info = GetOpcodeInfo(cpuDebug, cpuDebug->GetPC());
	uint32_t breakpointAddress = GetNextAddress(cpuDebug);
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
	cpuDebug = CPUFromRequest(req);
	if (cpuDebug->GetPC() != breakpointAddress) {
		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		if (cpuDebug != currentDebugMIPS)
			AddThreadCondition(breakpointAddress, threadID);
		Core_EnableStepping(false);
	}
}

// Step out of a function based on a stack walk (cpu.stepOut)
//
// Parameters:
//  - thread: optional number indicating the thread id to plan stepping on.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Out(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	uint32_t threadID;
	auto cpuDebug = CPUFromRequest(req, &threadID);
	if (!cpuDebug)
		return;

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
	if (frames.size() < 2) {
		return req.Fail("Could not find function call to step out into");
	}

	uint32_t breakpointAddress = frames[1].pc;
	PrepareResume();
	// Could have advanced to the breakpoint already in PrepareResume().
	cpuDebug = CPUFromRequest(req);
	if (cpuDebug->GetPC() != breakpointAddress) {
		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		if (cpuDebug != currentDebugMIPS)
			AddThreadCondition(breakpointAddress, threadID);
		Core_EnableStepping(false);
	}
}

// Run until a certain address (cpu.runUntil)
//
// Parameters:
//  - address: number parameter for destination.
//
// No immediate response.  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::RunUntil(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	uint32_t address = 0;
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

uint32_t WebSocketSteppingState::GetNextAddress(DebugInterface *cpuDebug) {
	uint32_t current = disasm_.getStartAddress(cpuDebug->GetPC());
	return disasm_.getNthNextAddress(current, 1);
}

int WebSocketSteppingState::GetNextInstructionCount(DebugInterface *cpuDebug) {
	return (GetNextAddress(cpuDebug) - cpuDebug->GetPC()) / 4;
}

void WebSocketSteppingState::PrepareResume() {
	if (currentMIPS->inDelaySlot) {
		Core_DoSingleStep();
	} else {
		// If the current PC is on a breakpoint, the user doesn't want to do nothing.
		CBreakPoints::SetSkipFirst(currentMIPS->pc);
	}
}

void WebSocketSteppingState::AddThreadCondition(uint32_t breakpointAddress, uint32_t threadID) {
	BreakPointCond cond;
	cond.debug = currentDebugMIPS;
	cond.expressionString = StringFromFormat("threadid == 0x%08x", threadID);
	if (currentDebugMIPS->initExpression(cond.expressionString.c_str(), cond.expression))
		CBreakPoints::ChangeBreakPointAddCond(breakpointAddress, cond);
}
