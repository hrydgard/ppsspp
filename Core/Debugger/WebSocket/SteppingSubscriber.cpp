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
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSStackWalk.h"

using namespace MIPSAnalyst;

struct WebSocketSteppingState : public DebuggerSubscriber {
	WebSocketSteppingState() {
		g_disassemblyManager.setCpu(currentDebugMIPS);
	}
	~WebSocketSteppingState() {
		g_disassemblyManager.clear();
	}

	void Into(DebuggerRequest &req);
	void Over(DebuggerRequest &req);
	void Out(DebuggerRequest &req);
	void RunUntil(DebuggerRequest &req);
	void RunUntilTime(DebuggerRequest &req);
	void HLE(DebuggerRequest &req);

protected:
	uint32_t GetNextAddress(DebugInterface *cpuDebug);
	void PrepareResume();
	void AddThreadCondition(uint32_t threadID);
};

DebuggerSubscriber *WebSocketSteppingInit(DebuggerEventHandlerMap &map) {
	WebSocketSteppingState *p = new WebSocketSteppingState();
	map["cpu.stepInto"] = [p](DebuggerRequest &req) { p->Into(req); };
	map["cpu.stepOver"] = [p](DebuggerRequest &req) { p->Over(req); };
	map["cpu.stepOut"]  = [p](DebuggerRequest &req) { p->Out(req); };
	map["cpu.runUntil"] = [p](DebuggerRequest &req) { p->RunUntil(req); };
	map["cpu.runUntilTime"] = [p](DebuggerRequest &req) { p->RunUntilTime(req); };
	map["cpu.nextHLE"]  = [p](DebuggerRequest &req) { p->HLE(req); };
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
// No immediate response on success (only a "deferred" event, if the client asked for those via
// client.config.set).  A cpu.stepping event will be sent once complete.
// May fail (same-thread case only) if too many steps are already queued, which means a client is
// firing them faster than they can possibly be carried out.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Into(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping()) {
		// Core_Break() is explicitly free-threaded (see Core.cpp), so no need to bounce this to the CPU
		// thread - and we can't anyway, since queuing to it only makes sense once the CPU actually *is*
		// stepping, which this call is what triggers in the first place.
		Core_Break(BreakReason::DebugStepInto, 0);
		return;
	}

	// Route the actual breakpoint/stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		uint32_t threadID;
		DebugInterface *cpuDebug = CPUFromRequest(req, &threadID);
		if (!cpuDebug)
			return;

		if (cpuDebug == currentDebugMIPS) {
			// If the current PC is on a breakpoint, the user doesn't want to do nothing.
			g_breakpoints.SetSkipFirst(currentMIPS->pc);

			// Steps queue up now, so this only fails once the queue is full - a client stepping
			// far faster than frames go by. No step happens and no cpu.stepping event fires in
			// that case, which would otherwise be indistinguishable from one still in flight, so
			// surface it as an error rather than leaving the caller waiting.
			if (!Core_RequestCPUStep(CPUStepType::Into)) {
				req.Fail("Could not step: a step or run request is already pending");
				return;
			}
		} else {
			uint32_t breakpointAddress = cpuDebug->GetPC();
			PrepareResume();
			// Could have advanced to the breakpoint already in PrepareResume().
			// Note: we need to get cpuDebug again anyway (in case we ran some HLE above.)
			cpuDebug = CPUFromRequest(req);
			if (cpuDebug != currentDebugMIPS) {
				g_breakpoints.SetTempBreakPoint(breakpointAddress);
				AddThreadCondition(threadID);
				Core_Resume();
			}
		}
	});
}

// Step over the next instruction (cpu.stepOver)
//
// Note: this jumps over function calls, but also delay slots.
//
// Parameters:
//  - thread: optional number indicating the thread id to plan stepping on.
//
// No immediate response (only a "deferred" event, if the client asked for those via
// client.config.set).  A cpu.stepping event will be sent once complete.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Over(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	// Route the actual breakpoint/stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		uint32_t threadID;
		DebugInterface *cpuDebug = CPUFromRequest(req, &threadID);
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
			g_breakpoints.SetTempBreakPoint(breakpointAddress);
			if (cpuDebug != currentDebugMIPS)
				AddThreadCondition(threadID);
			Core_Resume();
		}
	});
}

// Step out of a function based on a stack walk (cpu.stepOut)
//
// Parameters:
//  - thread: optional number indicating the thread id to plan stepping on.
//
// No immediate response (only a "deferred" event, if the client asked for those via
// client.config.set).  A cpu.stepping event will be sent once complete.
//
// Note: any thread can wake the cpu when it hits the next instruction currently.
void WebSocketSteppingState::Out(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive())
		return req.Fail("CPU not started");
	if (!Core_IsStepping())
		return req.Fail("CPU currently running (cpu.stepping first)");

	// Route the actual breakpoint/stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		uint32_t threadID;
		DebugInterface *cpuDebug = CPUFromRequest(req, &threadID);
		if (!cpuDebug)
			return;

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
		if (frames.size() < 2) {
			return req.Fail("Could not find function call to step out into");
		}

		uint32_t breakpointAddress = frames[1].pc;
		PrepareResume();
		// Could have advanced to the breakpoint already in PrepareResume().
		cpuDebug = CPUFromRequest(req);
		if (cpuDebug->GetPC() != breakpointAddress) {
			g_breakpoints.SetTempBreakPoint(breakpointAddress);
			if (cpuDebug != currentDebugMIPS)
				AddThreadCondition(threadID);
			Core_Resume();
		}
	});
}

// Run until a certain address (cpu.runUntil)
//
// Parameters:
//  - address: number parameter for destination.
//
// No immediate response (only a "deferred" event, if the client asked for those via
// client.config.set).  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::RunUntil(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	uint32_t address = 0;
	if (!req.ParamU32("address", &address)) {
		// Error already sent.
		return;
	}

	// Route the actual breakpoint/stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		bool wasAtAddress = currentMIPS->pc == address;
		PrepareResume();
		// We may have arrived already if PauseResume() stepped out of a delay slot.
		if (currentMIPS->pc != address || wasAtAddress) {
			g_breakpoints.SetTempBreakPoint(address);
			Core_Resume();
		}
	});
}

// Run until a point in emulated time (cpu.runUntilTime)
//
// The counterpart to cpu.runUntil for "let the game get N seconds in", which is what lining a
// scripted repro up with a wall-clock description of a bug needs. Polling cpu.status in a loop
// does the same job far more slowly and lands somewhere different every run; this stops on the
// requested tick, so the same script reaches the same place every time.
//
// Parameters (exactly one of):
//  - us: absolute emulated microseconds to run until, as reported by cpu.status.
//  - relativeUs: microseconds to run for, measured from now.
//
// Response (same event name):
//  - targetUs: the absolute emulated time it will stop at.
//  - us: emulated time right now.
// A cpu.stepping event follows once it gets there. Note that anything else that stops the CPU
// first - a breakpoint, an exception - cancels the deadline, same as it cancels a step.
void WebSocketSteppingState::RunUntilTime(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	const bool absolute = req.HasParam("us");
	if (absolute == req.HasParam("relativeUs")) {
		return req.Fail("Pass exactly one of 'us' or 'relativeUs'");
	}

	double requested = 0.0;
	if (!req.ParamF64(absolute ? "us" : "relativeUs", &requested)) {
		// Error already sent.
		return;
	}
	if (requested < 0.0) {
		return req.Fail("Time must not be negative");
	}

	// Route the actual stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		const u64 nowUs = CoreTiming::GetGlobalTimeUs();
		const u64 targetUs = absolute ? (u64)requested : nowUs + (u64)requested;
		if (targetUs <= nowUs) {
			req.Fail("Target time has already passed");
			return;
		}

		CoreTiming::SetBreakDeadlineUs(targetUs);

		PrepareResume();
		Core_Resume();

		JsonWriter &json = req.Respond();
		json.writeFloat("targetUs", (double)targetUs);
		json.writeFloat("us", (double)nowUs);
	});
}

// Jump after the next HLE call (cpu.nextHLE)
//
// No parameters.
//
// No immediate response (only a "deferred" event, if the client asked for those via
// client.config.set).  A cpu.stepping event will be sent once complete.
void WebSocketSteppingState::HLE(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	// Route the actual breakpoint/stepping manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		PrepareResume();
		hleDebugBreak();
		Core_Resume();
	});
}

uint32_t WebSocketSteppingState::GetNextAddress(DebugInterface *cpuDebug) {
	uint32_t current = g_disassemblyManager.getStartAddress(cpuDebug->GetPC());
	return g_disassemblyManager.getNthNextAddress(current, 1);
}

void WebSocketSteppingState::PrepareResume() {
	if (currentMIPS->inDelaySlot) {
		// Delay slot instructions are never joined, so we pass 1.
		//
		// This must happen synchronously, not via Core_RequestCPUStep(): that only queues the
		// step for Core_ProcessStepping() to perform later (on the next iteration of the normal
		// stepping-mode loop), while every caller of PrepareResume() immediately inspects
		// currentMIPS->pc/inDelaySlot right after this returns to decide whether to add a
		// breakpoint and call Core_Resume(). Core_Resume() itself sets coreState back to
		// CORE_RUNNING_CPU, which makes Core_ProcessStepping() skip its pending-step check
		// entirely - so the queued step was not just late, it was silently dropped, leaving
		// g_cpuStepCommand permanently set until the next Core_Break() reset it. Any stepping
		// request issued by the debugger client in that window (e.g. a script or fast-clicking
		// UI immediately re-stepping instead of waiting for a fresh cpu.stepping event) hit
		// Core_RequestCPUStep()'s "Can't submit two steps in one host frame" guard and got
		// silently ignored - the "step-out sometimes just doesn't do anything" flakiness this
		// was found while tracking down. PrepareResume() is only ever called from within a
		// Core_RunOnCPUThread() callback (Into/Over/Out/RunUntil/HLE below), so it's always
		// already running on the CPU thread - safe to single-step directly instead of queuing.
		currentMIPS->SingleStep();
	} else {
		// If the current PC is on a breakpoint, the user doesn't want to do nothing.
		g_breakpoints.SetSkipFirst(currentMIPS->pc);
	}
}

// Restricts the temporary breakpoint a step just planted to the thread the step was requested
// for, so an unrelated thread running through the same address doesn't complete someone else's
// step. Must be called right after SetTempBreakPoint().
void WebSocketSteppingState::AddThreadCondition(uint32_t threadID) {
	BreakPointCond cond;
	cond.debug = currentDebugMIPS;
	cond.expressionString = StringFromFormat("threadid == 0x%08x", threadID);
	if (initExpression(currentDebugMIPS, cond.expressionString.c_str(), cond.expression))
		g_breakpoints.SetTempBreakPointCond(cond);
}
