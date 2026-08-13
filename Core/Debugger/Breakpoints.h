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
	BREAK_ACTION_IGNORE = 0x00,
	BREAK_ACTION_LOG = 0x01,
	BREAK_ACTION_PAUSE = 0x02,
};

static inline BreakAction &operator |= (BreakAction &lhs, const BreakAction &rhs) {
	lhs = BreakAction(lhs | rhs);
	return lhs;
}

static inline BreakAction operator | (const BreakAction &lhs, const BreakAction &rhs) {
	return BreakAction((u32)lhs | (u32)rhs);
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
	bool temporary;

	BreakAction result = BREAK_ACTION_IGNORE;
	std::string logFormat;

	bool hasCond = false;
	BreakPointCond cond;

	bool IsEnabled() const {
		return (result & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const BreakPoint &other) const {
		return addr == other.addr;
	}
	bool operator < (const BreakPoint &other) const {
		return addr < other.addr;
	}
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
	BreakAction result = BREAK_ACTION_IGNORE;
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
	BreakAction Action(u32 addr, bool write, int size, u32 pc, const char *reason);

	void Log(u32 addr, bool write, int size, u32 pc, const char *reason) const;

	bool IsEnabled() const {
		return (result & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const MemCheck &other) const {
		return start == other.start && end == other.end;
	}
};

// A breakpoint that trips whenever a general-purpose register is written to by an
// instruction, regardless of address - identified by register index (0-31), not addr/range.
// Interpreter-only for now (see RunUntilDowncountZeroWithChecks in MIPSTables.cpp) - the JITs
// don't check this at all, so it has no effect unless running with the plain interpreter core.
struct GPRBreakpoint {
	int reg = 0;  // 0-31, general-purpose register index (matches OUT_RT/OUT_RD/OUT_RA fields).

	BreakAction result = BREAK_ACTION_IGNORE;
	std::string logFormat;

	bool hasCond = false;
	BreakPointCond cond;

	u32 numHits = 0;

	bool IsEnabled() const {
		return (result & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const GPRBreakpoint &other) const {
		return reg == other.reg;
	}
};

// BreakPoints cannot overlap, only one is allowed per address.
// MemChecks can overlap, as long as their ends are different.
// WARNING: MemChecks are not always tracked in HLE currently.
class BreakpointManager {
public:
	static const size_t INVALID_BREAKPOINT = -1;
	static const size_t INVALID_MEMCHECK = -1;
	static const size_t INVALID_GPR_BREAKPOINT = -1;

	bool IsAddressBreakPoint(u32 addr);
	bool IsAddressBreakPoint(u32 addr, bool* enabled);
	bool IsTempBreakPoint(u32 addr);
	bool RangeContainsBreakPoint(u32 addr, u32 size);
	int AddBreakPoint(u32 addr, bool temp = false);  // Returns the breakpoint index.
	void RemoveBreakPoint(u32 addr);
	void ChangeBreakPoint(u32 addr, bool enable);
	void ChangeBreakPoint(u32 addr, BreakAction result);
	void ClearAllBreakPoints();
	void ClearTemporaryBreakPoints();

	// Makes a copy of the condition.
	void ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond);
	void ChangeBreakPointRemoveCond(u32 addr);
	BreakPointCond *GetBreakPointCondition(u32 addr);

	void ChangeBreakPointLogFormat(u32 addr, const std::string &fmt);

	BreakAction ExecBreakPoint(u32 addr);

	int AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result);
	void RemoveMemCheck(u32 start, u32 end);
	void ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result);
	void ClearAllMemChecks();

	void ChangeMemCheckAddCond(u32 start, u32 end, const BreakPointCond &cond);
	void ChangeMemCheckRemoveCond(u32 start, u32 end);
	BreakPointCond *GetMemCheckCondition(u32 start, u32 end);

	void ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt);

	bool GetMemCheck(u32 start, u32 end, MemCheck *check);
	bool GetMemCheckInRange(u32 address, int size, MemCheck *check);
	BreakAction ExecMemCheck(u32 address, bool write, int size, u32 pc, const char *reason);
	BreakAction ExecOpMemCheck(u32 address, u32 pc);

	// GPR write breakpoints - see GPRBreakpoint above. reg is a 0-31 GPR index.
	int AddGPRBreakpoint(int reg);  // Returns the breakpoint index.
	void RemoveGPRBreakpoint(int reg);
	void ChangeGPRBreakpoint(int reg, bool enable);
	void ChangeGPRBreakpoint(int reg, BreakAction result);
	void ClearAllGPRBreakpoints();

	void ChangeGPRBreakpointAddCond(int reg, const BreakPointCond &cond);
	void ChangeGPRBreakpointRemoveCond(int reg);
	BreakPointCond *GetGPRBreakpointCondition(int reg);

	void ChangeGPRBreakpointLogFormat(int reg, const std::string &fmt);

	bool IsGPRBreakpoint(int reg);
	bool GetGPRBreakpoint(int reg, GPRBreakpoint *bp);
	std::vector<GPRBreakpoint> GetGPRBreakpoints();

	// Called from the interpreter (RunUntilDowncountZeroWithChecks) right before executing an
	// instruction that would write to reg - does not itself execute the instruction.
	BreakAction ExecGPRBreakpoint(int reg, u32 pc);

	void SetSkipFirst(u32 pc);
	u32 CheckSkipFirst();

	// Includes uncached addresses.
	std::vector<MemCheck> GetMemCheckRanges(bool write);

	std::vector<MemCheck> GetMemChecks();
	std::vector<BreakPoint> GetBreakpoints();

	// For editing through the imdebugger.
	// Since it's on the main thread, we don't need to fear threading clashes.
	std::vector<BreakPoint> &GetBreakpointRefs() {
		return breakPoints_;
	}
	std::vector<MemCheck> &GetMemCheckRefs() {
		return memChecks_;
	}

	bool HasBreakPoints() const {
		return anyBreakPoints_;
	}
	bool HasMemChecks() const {
		return anyMemChecks_;
	}
	bool HasGPRBreakpoints() const {
		return gprBreakpointMask_ != 0;
	}
	// Bit i set means register i has an active (non-ignored) GPR breakpoint - a cheap way for
	// the interpreter's hot per-instruction loop to test "would this write trip anything" with
	// a single shift+and, without touching gprBreakpoints_ at all in the common no-match case.
	u32 GetGPRBreakpointMask() const {
		return gprBreakpointMask_;
	}

	void Frame();

	bool ValidateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt);
	bool EvaluateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt, std::string &result);

private:
	// 0 means to clear the whole jit cache, to apply some change that has been made.
	void Update(u32 addr) {
		needsUpdate_ = true;
		updateAddr_ = addr;
	}
	size_t FindBreakpoint(u32 addr, bool matchTemp = false, bool temp = false);
	// Finds exactly, not using a range check.
	size_t FindMemCheck(u32 start, u32 end);
	// Finds a memcheck covering (part of) a range, unlike FindMemCheck() above.
	MemCheck *FindMemCheckInRange(u32 address, int size);
	void UpdateCachedMemCheckRanges();
	size_t FindGPRBreakpoint(int reg);
	void RecomputeGPRBreakpointMask();

	std::atomic<bool> anyBreakPoints_;
	std::atomic<bool> anyMemChecks_;
	std::atomic<u32> gprBreakpointMask_;

	std::vector<BreakPoint> breakPoints_;
	u32 breakSkipFirstAt_ = 0;
	u64 breakSkipFirstTicks_ = 0;

	std::vector<MemCheck> memChecks_;
	std::vector<MemCheck> memCheckRangesRead_;
	std::vector<MemCheck> memCheckRangesWrite_;

	std::vector<GPRBreakpoint> gprBreakpoints_;

	bool needsUpdate_ = true;
	u32 updateAddr_ = 0;

	enum {
		INVALID_ADDRESS = -1
	};
};

extern BreakpointManager g_breakpoints;

