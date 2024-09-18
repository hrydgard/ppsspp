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

#include "Core/Debugger/DebugInterface.h"

enum BreakAction {
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
		if (debug->parseExpression(expression, result) == false)
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

	void Log(u32 addr, bool write, int size, u32 pc, const char *reason);

	bool IsEnabled() const {
		return (result & BREAK_ACTION_PAUSE) != 0;
	}

	bool operator == (const MemCheck &other) const {
		return start == other.start && end == other.end;
	}
};

// BreakPoints cannot overlap, only one is allowed per address.
// MemChecks can overlap, as long as their ends are different.
// WARNING: MemChecks are not always tracked in HLE currently.
class CBreakPoints
{
public:
	static const size_t INVALID_BREAKPOINT = -1;
	static const size_t INVALID_MEMCHECK = -1;

	static bool IsAddressBreakPoint(u32 addr);
	static bool IsAddressBreakPoint(u32 addr, bool* enabled);
	static bool IsTempBreakPoint(u32 addr);
	static bool RangeContainsBreakPoint(u32 addr, u32 size);
	static void AddBreakPoint(u32 addr, bool temp = false);
	static void RemoveBreakPoint(u32 addr);
	static void ChangeBreakPoint(u32 addr, bool enable);
	static void ChangeBreakPoint(u32 addr, BreakAction result);
	static void ClearAllBreakPoints();
	static void ClearTemporaryBreakPoints();

	// Makes a copy of the condition.
	static void ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond);
	static void ChangeBreakPointRemoveCond(u32 addr);
	static BreakPointCond *GetBreakPointCondition(u32 addr);

	static void ChangeBreakPointLogFormat(u32 addr, const std::string &fmt);

	static BreakAction ExecBreakPoint(u32 addr);

	static void AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result);
	static void RemoveMemCheck(u32 start, u32 end);
	static void ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result);
	static void ClearAllMemChecks();

	static void ChangeMemCheckAddCond(u32 start, u32 end, const BreakPointCond &cond);
	static void ChangeMemCheckRemoveCond(u32 start, u32 end);
	static BreakPointCond *GetMemCheckCondition(u32 start, u32 end);

	static void ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt);

	static bool GetMemCheck(u32 start, u32 end, MemCheck *check);
	static bool GetMemCheckInRange(u32 address, int size, MemCheck *check);
	static BreakAction ExecMemCheck(u32 address, bool write, int size, u32 pc, const char *reason);
	static BreakAction ExecOpMemCheck(u32 address, u32 pc);

	static void SetSkipFirst(u32 pc);
	static u32 CheckSkipFirst();

	// Includes uncached addresses.
	static std::vector<MemCheck> GetMemCheckRanges(bool write);

	static std::vector<MemCheck> GetMemChecks();
	static std::vector<BreakPoint> GetBreakpoints();

	static bool HasBreakPoints();
	static bool HasMemChecks();

	static void Update(u32 addr = 0);

	static bool ValidateLogFormat(DebugInterface *cpu, const std::string &fmt);
	static bool EvaluateLogFormat(DebugInterface *cpu, const std::string &fmt, std::string &result);

private:
	static size_t FindBreakpoint(u32 addr, bool matchTemp = false, bool temp = false);
	// Finds exactly, not using a range check.
	static size_t FindMemCheck(u32 start, u32 end);
	static MemCheck *GetMemCheckLocked(u32 address, int size);
	static void UpdateCachedMemCheckRanges();

	static std::vector<BreakPoint> breakPoints_;
	static u32 breakSkipFirstAt_;
	static u64 breakSkipFirstTicks_;

	static std::vector<MemCheck> memChecks_;
	static std::vector<MemCheck> memCheckRangesRead_;
	static std::vector<MemCheck> memCheckRangesWrite_;
};


