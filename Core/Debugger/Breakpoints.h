// Copyright (c) 2012- PPSSPP Project.

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

#include <vector>
#include <atomic>

#include "Core/MIPS/MIPSDebugInterface.h"
#include "Common/Math/expression_parser.h"

enum BreakAction : u32 {
	BREAK_ACTION_NONE = 0,
	BREAK_ACTION_LOG = (1 << 0),
	BREAK_ACTION_PAUSE = (1 << 1),
};

static inline BreakAction &operator |= (BreakAction &lhs, const BreakAction &rhs) {
	lhs = BreakAction(lhs | rhs);
	return lhs;
}

static inline BreakAction operator | (const BreakAction &lhs, const BreakAction &rhs) {
	return BreakAction((u32)lhs | (u32)rhs);
}

static inline BreakAction operator ~ (const BreakAction &v) {
	return BreakAction(~(u32)v);
}

// For dropping an action that isn't wanted after all, e.g. result &= ~BREAK_ACTION_PAUSE.
static inline BreakAction &operator &= (BreakAction &lhs, const BreakAction &rhs) {
	lhs = BreakAction((u32)lhs & (u32)rhs);
	return lhs;
}

struct BreakPointCond {
	DebugInterface *debug = nullptr;
	PostfixExpression expression;
	std::string expressionString;

	u32 Evaluate() {
		u32 result;
		if (parseExpression(debug, expression, result) == false)
			return 0;
		return result;
	}
};

struct BreakPoint {
	u32	addr;

	BreakAction action = BREAK_ACTION_NONE;
	std::string logFormat;

	bool hasCond = false;
	BreakPointCond cond;

	// Matches MemCheck's numHits below - added after repeatedly needing to tell "is this
	// breakpoint even being reached at all" from "it's reached but the log isn't showing up
	// where I'm looking" during the VSH boot investigation (see docs/VSHBootInvestigation.md).
	u32 numHits = 0;

	bool IsEnabled() const {
		return (action & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const BreakPoint &other) const {
		return addr == other.addr;
	}
	bool operator < (const BreakPoint &other) const {
		return addr < other.addr;
	}
};

// The internal one-shot breakpoint that step-over, step-out and run-until plant at the address they
// want execution to come back to. Deliberately kept out of the user's breakpoint list: it belongs to
// an in-flight step, and when it lived in the same array the two kinds kept colliding - a user
// breakpoint edit could land on the temporary one (address lookups return one entry per address, and
// a log-only user breakpoint isn't "enabled", so the temporary one won the search), and removing
// either deleted both.
//
// One is enough. Step over/out and cross-thread step into all require the CPU to already be stepping
// and resume it immediately, so only one can ever be in flight; run-until is the only caller that
// could stack them, and stacking has no coherent meaning - other debuggers ("run to cursor",
// gdb's until/advance) replace instead, which is what SetTempBreakPoint does.
struct TempBreakPoint {
	bool valid = false;
	u32 addr = 0;

	// Set when planning a step on a thread other than the current one - "threadid == 0x...".
	bool hasCond = false;
	BreakPointCond cond;
};

enum MemCheckCondition {
	MEMCHECK_READ = 0x01,
	MEMCHECK_WRITE = 0x02,
	MEMCHECK_WRITE_ONCHANGE = 0x04,

	MEMCHECK_READWRITE = 0x03,
};

struct MemCheck {
	u32 start;
	u32 end;

	MemCheckCondition cond = MEMCHECK_READ;
	BreakAction action = BREAK_ACTION_NONE;
	std::string logFormat;

	bool hasCondition = false;
	BreakPointCond condition;

	u32 numHits = 0;

	u32 lastPC = 0;
	u32 lastAddr = 0;
	int lastSize = 0;

	// Called on the stored memcheck (affects numHits, etc.)
	BreakAction Apply(u32 addr, bool write, int size, u32 pc);
	// Called on a copy.
	// Logs the hit and returns the action. Does not pause - that's the caller's call, since it's
	// the one that knows whether we're just resuming off this instruction.
	BreakAction Action(u32 addr, bool write, int size, u32 pc, const char *reason);

	void Log(u32 addr, bool write, int size, u32 pc, const char *reason) const;

	bool IsEnabled() const {
		return (action & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const MemCheck &other) const {
		return start == other.start && end == other.end;
	}
};

// A breakpoint that trips whenever a register is written to by an instruction, regardless of
// address - identified by register index (0-31), not addr/range. Currently only GPRs are
// supported (reg is a GPR index), but the naming is kept general since this is expected to grow
// to cover other register files too (e.g. FPU registers like $f10).
// Interpreter-only for now (see RunUntilDowncountZeroWithChecks in MIPSTables.cpp) - the JITs
// don't check this at all, so it has no effect unless running with the plain interpreter core.
struct RegBreakpoint {
	int reg = 0;  // 0-31, general-purpose register index (matches OUT_RT/OUT_RD/OUT_RA fields).

	BreakAction result = BREAK_ACTION_NONE;
	std::string logFormat;

	bool hasCond = false;
	BreakPointCond cond;

	u32 numHits = 0;

	bool IsEnabled() const {
		return (result & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const RegBreakpoint &other) const {
		return reg == other.reg;
	}
};

// BreakPoints cannot overlap, only one is allowed per address.
// MemChecks can overlap, as long as their ends are different.
// WARNING: MemChecks are not always tracked in HLE currently (some functions write to memory without
// notifying the breakpoint manager).
class BreakpointManager {
public:
	static const size_t INVALID_BREAKPOINT = -1;
	static const size_t INVALID_MEMCHECK = -1;
	static const size_t INVALID_REG_BREAKPOINT = -1;

	// User-facing: "does the user have a breakpoint here" - for the breakpoint lists and for drawing
	// markers in the disassembly views. Deliberately does not see the temporary breakpoint.
	bool IsAddressBreakPoint(u32 addr);
	bool IsAddressBreakPoint(u32 addr, bool* enabled);

	// Codegen/hot-path facing: "does anything at all need checking here", temporary breakpoint
	// included. Use these from the JIT frontends and the interpreter, never to decide what to show
	// the user.
	bool NeedsBreakCheckAt(u32 addr);
	bool RangeContainsBreakPoint(u32 addr, u32 size);

	int AddBreakPoint(u32 addr);  // Returns the breakpoint index.
	void RemoveBreakPoint(u32 addr);
	void ChangeBreakPoint(u32 addr, bool enable);
	void ChangeBreakPoint(u32 addr, BreakAction action);
	// Moves an existing breakpoint, keeping its action, condition and log format. Prefer this over
	// assigning to BreakPoint::addr through GetBreakpointRefs() - there's cache invalidation and a
	// duplicate check to get right, see the implementation.
	bool ChangeBreakPointAddress(u32 oldAddr, u32 newAddr);
	void ClearAllBreakPoints();

	// Makes a copy of the condition.
	void ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond);
	void ChangeBreakPointRemoveCond(u32 addr);
	BreakPointCond *GetBreakPointCondition(u32 addr);

	void ChangeBreakPointLogFormat(u32 addr, const std::string &fmt);

	BreakAction ExecBreakPoint(u32 addr);

	// The one-shot breakpoint behind step-over/step-out/run-until - see TempBreakPoint above.
	// Setting one replaces any previous one. It's cleared automatically whenever execution stops for
	// any reason (see Core_Break), matching how other debuggers abandon a pending step when
	// something else stops you first.
	void SetTempBreakPoint(u32 addr);
	// Applies to the temp breakpoint set above; used to restrict a planned step to one thread.
	void SetTempBreakPointCond(const BreakPointCond &cond);
	void ClearTempBreakPoint();
	bool HasTempBreakPoint() const { return tempBreakPoint_.valid; }

	int AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction action);
	void RemoveMemCheck(u32 start, u32 end);
	void ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction action);
	void ClearAllMemChecks();

	void ChangeMemCheckAddCond(u32 start, u32 end, const BreakPointCond &cond);
	void ChangeMemCheckRemoveCond(u32 start, u32 end);
	BreakPointCond *GetMemCheckCondition(u32 start, u32 end);

	void ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt);

	bool GetMemCheck(u32 start, u32 end, MemCheck *check);
	bool GetMemCheckInRange(u32 address, int size, MemCheck *check);
	BreakAction ExecMemCheck(u32 address, bool write, int size, u32 pc, const char *reason);
	BreakAction ExecOpMemCheck(u32 address, u32 pc);

	// Register write breakpoints - see RegBreakpoint above. reg is a 0-31 GPR index.
	int AddRegBreakpoint(int reg);  // Returns the breakpoint index.
	void RemoveRegBreakpoint(int reg);
	void ChangeRegBreakpoint(int reg, bool enable);
	void ChangeRegBreakpoint(int reg, BreakAction action);
	void ClearAllRegBreakpoints();

	void ChangeRegBreakpointAddCond(int reg, const BreakPointCond &cond);
	void ChangeRegBreakpointRemoveCond(int reg);
	BreakPointCond *GetRegBreakpointCondition(int reg);

	void ChangeRegBreakpointLogFormat(int reg, const std::string &fmt);

	bool IsRegBreakpoint(int reg);
	bool GetRegBreakpoint(int reg, RegBreakpoint *bp);
	std::vector<RegBreakpoint> GetRegBreakpoints();

	// Called from the interpreter (RunUntilDowncountZeroWithChecks) right before executing an
	// instruction that would write to reg - does not itself execute the instruction.
	BreakAction ExecRegBreakpoint(int reg, u32 pc);

	// Sitting on a breakpoint and resuming or stepping needs two different things suppressed, and
	// conflating them is a bug in both directions - see PendingExec and the two markers below.
	// Call this when starting to run or step, with the address execution starts from.
	void NotifyResumingFrom(u32 addr);
	// The run or step that armed the above is over (we stopped for some reason).
	void ClearResumeMarker();
	// Both markers are (address, tick count) pairs, so a reset or a savestate load can leave one
	// that happens to match again. Call this when execution discontinuously jumps like that.
	void ResetExecutionMarkers();

	// Includes uncached addresses.
	std::vector<MemCheck> GetMemCheckRanges(bool write);

	std::vector<MemCheck> GetMemChecks();
	std::vector<BreakPoint> GetBreakpoints();

	// For editing through the imdebugger.
	// Since it's on the main thread, we don't need to fear threading clashes.
	std::vector<BreakPoint> &GetBreakpointRefs() { return breakPoints_; }

	// NOTE: If you edit this array directly, you need to call NotifyChangedMemchecks().
	std::vector<MemCheck> &GetMemCheckRefs() { return memChecks_; }

	bool HasBreakPoints() const { return anyBreakPoints_; }
	bool HasMemChecks() const { return anyMemChecks_; }

	void NotifyChangedMemchecks() { updateMemChecks_ = true; }

	// Bit i set means register i has an active (non-ignored) register breakpoint - a cheap way
	// for the interpreter's hot per-instruction loop to test "would this write trip anything".
	u32 GetRegBreakpointMask() const { return regBreakpointMask_; }
	bool HasRegBreakpoints() const { return regBreakpointMask_ != 0; }

	void Frame();

	bool ValidateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt);
	bool EvaluateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt, std::string &result);

private:
	size_t FindBreakpoint(u32 addr);
	// anyBreakPoints_ gates the interpreter's checked run loop and whether the JIT emits checks at
	// all, so it has to account for the temporary breakpoint too - otherwise a step-over with no
	// user breakpoints set (the common case) would never come back.
	void UpdateAnyBreakPoints();
	// Finds exactly, not using a range check.
	size_t FindMemCheck(u32 start, u32 end);
	// Finds a memcheck covering (part of) a range, unlike FindMemCheck() above.
	MemCheck *FindMemCheckInRange(u32 address, int size);
	void UpdateCachedMemCheckRanges();
	size_t FindRegBreakpoint(int reg);
	void RecomputeRegBreakpointMask();

	std::atomic<bool> anyBreakPoints_;
	std::atomic<bool> anyMemChecks_;
	std::atomic<u32> regBreakpointMask_;

	// Identifies one pending execution of one instruction: the address, plus the tick count at the
	// moment it is about to run. CoreTiming::GetTicks() is continuous across CoreTiming::Advance()
	// (it's globalTimer + slicelength - downcount, and Advance moves both by the same amount), so
	// it only changes when the CPU actually retires an instruction. That makes (addr, ticks) stop
	// matching as soon as the instruction runs - come around a loop to the same address and it's a
	// different pending execution, which is what makes a breakpoint in a loop keep firing.
	// Has an explicit valid flag rather than treating address 0 as "none", or a breakpoint at 0
	// would be permanently suppressed.
	struct PendingExec {
		bool valid = false;
		u32 addr = 0;
		u64 ticks = 0;

		void Arm(u32 a, u64 t) { valid = true; addr = a; ticks = t; }
		void Clear() { valid = false; }
		bool Matches(u32 a, u64 t) const { return valid && addr == a && ticks == t; }
	};

	// Helpers over the markers below. addr is the instruction being checked - for a memcheck
	// that's the instruction performing the access, not the address being accessed.
	bool ShouldSuppressPauseAt(u32 addr) const;
	bool AlreadyReportedAt(u32 addr) const;
	void NoteStoppedOnReported(u32 addr);

	std::vector<BreakPoint> breakPoints_;
	TempBreakPoint tempBreakPoint_;
	// Where the current run or step started. A breakpoint there must not *pause*, or you could
	// never get off a breakpoint you're stopped on. It must still log and count though: arriving
	// at an address by stepping is not the same as having reported the breakpoint there, and
	// treating it as such is what used to silently swallow a breakpoint you stepped onto.
	PendingExec resumedFrom_;
	// The breakpoint we already reported (logged and counted). Reporting stops the CPU before the
	// instruction runs, so the resume or step that follows arrives at the same pending execution
	// and must not report it a second time.
	PendingExec reported_;
	// How reported_ gets armed. It can't be armed where the report happens: under a JIT that's
	// inside a compiled block, whose cycles are already accounted for, so the tick count there
	// isn't the settled one we'll see on the way back in. So the report just records the address,
	// and NotifyResumingFrom() turns it into a real (addr, ticks) marker once the CPU has stopped
	// and the tick count means something again. The address is what distinguishes "stopped here
	// because this breakpoint reported" from "a step happened to land here", which must still
	// report when execution moves on.
	struct {
		bool valid = false;
		u32 addr = 0;
	} stoppedOnReported_;

	std::vector<MemCheck> memChecks_;
	std::vector<MemCheck> memCheckRangesRead_;
	std::vector<MemCheck> memCheckRangesWrite_;

	std::vector<RegBreakpoint> regBreakpoints_;

	bool updateMemChecks_ = false;

	enum {
		INVALID_ADDRESS = -1
	};
};

extern BreakpointManager g_breakpoints;

