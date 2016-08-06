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

#include <cstdio>

#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Host.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"

std::vector<BreakPoint> CBreakPoints::breakPoints_;
u32 CBreakPoints::breakSkipFirstAt_ = 0;
u64 CBreakPoints::breakSkipFirstTicks_ = 0;
std::vector<MemCheck> CBreakPoints::memChecks_;
std::vector<MemCheck *> CBreakPoints::cleanupMemChecks_;

MemCheck::MemCheck()
{
	numHits = 0;
}

void MemCheck::Log(u32 addr, bool write, int size, u32 pc) {
	if (result & BREAK_ACTION_LOG) {
		if (logFormat.empty()) {
			NOTICE_LOG(MEMMAP, "CHK %s%i at %08x (%s), PC=%08x (%s)", write ? "Write" : "Read", size * 8, addr, g_symbolMap->GetDescription(addr).c_str(), pc, g_symbolMap->GetDescription(pc).c_str());
		} else {
			std::string formatted;
			CBreakPoints::EvaluateLogFormat(currentDebugMIPS, logFormat, formatted);
			NOTICE_LOG(MEMMAP, "CHK %s%i at %08x: %s", write ? "Write" : "Read", size * 8, addr, formatted.c_str());
		}
	}
}

BreakAction MemCheck::Action(u32 addr, bool write, int size, u32 pc)
{
	int mask = write ? MEMCHECK_WRITE : MEMCHECK_READ;
	if (cond & mask)
	{
		++numHits;

		Log(addr, write, size, pc);
		if (result & BREAK_ACTION_PAUSE)
		{
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}

		return result;
	}

	return BREAK_ACTION_IGNORE;
}

void MemCheck::JitBefore(u32 addr, bool write, int size, u32 pc)
{
	int mask = MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE;
	if (write && (cond & mask) == mask)
	{
		lastAddr = addr;
		lastPC = pc;
		lastSize = size;

		// We have to break to find out if it changed.
		Core_EnableStepping(true);
	}
	else
	{
		lastAddr = 0;
		Action(addr, write, size, pc);
	}
}

void MemCheck::JitCleanup()
{
	if (lastAddr == 0 || lastPC == 0)
		return;

	// Here's the tricky part: would this have changed memory?
	// Note that it did not actually get written.
	bool changed = MIPSAnalyst::OpWouldChangeMemory(lastPC, lastAddr, lastSize);
	if (changed)
	{
		++numHits;
		Log(lastAddr, true, lastSize, lastPC);
	}

	// Resume if it should not have gone to stepping, or if it did not change.
	if ((!(result & BREAK_ACTION_PAUSE) || !changed) && coreState == CORE_STEPPING)
	{
		CBreakPoints::SetSkipFirst(lastPC);
		Core_EnableStepping(false);
	}
	else
		host->SetDebugMode(true);
}

size_t CBreakPoints::FindBreakpoint(u32 addr, bool matchTemp, bool temp)
{
	size_t found = INVALID_BREAKPOINT;
	for (size_t i = 0; i < breakPoints_.size(); ++i)
	{
		const auto &bp = breakPoints_[i];
		if (bp.addr == addr && (!matchTemp || bp.temporary == temp))
		{
			if (bp.IsEnabled())
				return i;
			// Hold out until the first enabled one.
			if (found == INVALID_BREAKPOINT)
				found = i;
		}
	}

	return found;
}

size_t CBreakPoints::FindMemCheck(u32 start, u32 end)
{
	for (size_t i = 0; i < memChecks_.size(); ++i)
	{
		if (memChecks_[i].start == start && memChecks_[i].end == end)
			return i;
	}

	return INVALID_MEMCHECK;
}

bool CBreakPoints::IsAddressBreakPoint(u32 addr)
{
	size_t bp = FindBreakpoint(addr);
	return bp != INVALID_BREAKPOINT && breakPoints_[bp].result != BREAK_ACTION_IGNORE;
}

bool CBreakPoints::IsAddressBreakPoint(u32 addr, bool* enabled)
{
	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) return false;
	if (enabled != nullptr)
		*enabled = breakPoints_[bp].IsEnabled();
	return true;
}

bool CBreakPoints::IsTempBreakPoint(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, true);
	return bp != INVALID_BREAKPOINT;
}

bool CBreakPoints::RangeContainsBreakPoint(u32 addr, u32 size)
{
	const u32 end = addr + size;
	for (const auto &bp : breakPoints_)
	{
		if (bp.addr >= addr && bp.addr < end)
			return true;
	}

	return false;
}

void CBreakPoints::AddBreakPoint(u32 addr, bool temp)
{
	size_t bp = FindBreakpoint(addr, true, temp);
	if (bp == INVALID_BREAKPOINT)
	{
		BreakPoint pt;
		pt.result |= BREAK_ACTION_PAUSE;
		pt.temporary = temp;
		pt.addr = addr;

		breakPoints_.push_back(pt);
		Update(addr);
	}
	else if (!breakPoints_[bp].IsEnabled())
	{
		breakPoints_[bp].result |= BREAK_ACTION_PAUSE;
		breakPoints_[bp].hasCond = false;
		Update(addr);
	}
}

void CBreakPoints::RemoveBreakPoint(u32 addr)
{
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_.erase(breakPoints_.begin() + bp);

		// Check again, there might've been an overlapping temp breakpoint.
		bp = FindBreakpoint(addr);
		if (bp != INVALID_BREAKPOINT)
			breakPoints_.erase(breakPoints_.begin() + bp);

		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPoint(u32 addr, bool status)
{
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		if (status)
			breakPoints_[bp].result |= BREAK_ACTION_PAUSE;
		else
			breakPoints_[bp].result = BreakAction(breakPoints_[bp].result & ~BREAK_ACTION_PAUSE);
		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPoint(u32 addr, BreakAction result)
{
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].result = result;
		Update(addr);
	}
}

void CBreakPoints::ClearAllBreakPoints()
{
	if (!breakPoints_.empty())
	{
		breakPoints_.clear();
		Update();
	}
}

void CBreakPoints::ClearTemporaryBreakPoints()
{
	if (breakPoints_.empty())
		return;

	bool update = false;
	for (int i = (int)breakPoints_.size()-1; i >= 0; --i)
	{
		if (breakPoints_[i].temporary)
		{
			breakPoints_.erase(breakPoints_.begin() + i);
			update = true;
		}
	}
	
	if (update)
		Update();
}

void CBreakPoints::ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond)
{
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = true;
		breakPoints_[bp].cond = cond;
		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPointRemoveCond(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = false;
		Update(addr);
	}
}

BreakPointCond *CBreakPoints::GetBreakPointCondition(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT && breakPoints_[bp].hasCond)
		return &breakPoints_[bp].cond;
	return NULL;
}

void CBreakPoints::ChangeBreakPointLogFormat(u32 addr, const std::string &fmt) {
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_[bp].logFormat = fmt;
		Update(addr);
	}
}

BreakAction CBreakPoints::ExecBreakPoint(u32 addr) {
	size_t bp = FindBreakpoint(addr, false);
	if (bp != INVALID_BREAKPOINT) {
		if (breakPoints_[bp].hasCond) {
			// Evaluate the breakpoint and abort if necessary.
			auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
			if (cond && !cond->Evaluate())
				return BREAK_ACTION_IGNORE;
		}

		if (breakPoints_[bp].result & BREAK_ACTION_LOG) {
			if (breakPoints_[bp].logFormat.empty()) {
				NOTICE_LOG(JIT, "BKP PC=%08x (%s)", addr, g_symbolMap->GetDescription(addr).c_str());
			} else {
				std::string formatted;
				CBreakPoints::EvaluateLogFormat(currentDebugMIPS, breakPoints_[bp].logFormat, formatted);
				NOTICE_LOG(JIT, "BKP PC=%08x: %s", addr, formatted.c_str());
			}
		}
		if (breakPoints_[bp].result & BREAK_ACTION_PAUSE) {
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}

		return breakPoints_[bp].result;
	}

	return BREAK_ACTION_IGNORE;
}

void CBreakPoints::AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result)
{
	// This will ruin any pending memchecks.
	cleanupMemChecks_.clear();

	size_t mc = FindMemCheck(start, end);
	if (mc == INVALID_MEMCHECK)
	{
		MemCheck check;
		check.start = start;
		check.end = end;
		check.cond = cond;
		check.result = result;

		memChecks_.push_back(check);
		Update();
	}
	else
	{
		memChecks_[mc].cond = (MemCheckCondition)(memChecks_[mc].cond | cond);
		memChecks_[mc].result = (BreakAction)(memChecks_[mc].result | result);
		Update();
	}
}

void CBreakPoints::RemoveMemCheck(u32 start, u32 end)
{
	// This will ruin any pending memchecks.
	cleanupMemChecks_.clear();

	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_.erase(memChecks_.begin() + mc);
		Update();
	}
}

void CBreakPoints::ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result)
{
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_[mc].cond = cond;
		memChecks_[mc].result = result;
		Update();
	}
}

void CBreakPoints::ClearAllMemChecks()
{
	// This will ruin any pending memchecks.
	cleanupMemChecks_.clear();

	if (!memChecks_.empty())
	{
		memChecks_.clear();
		Update();
	}
}

void CBreakPoints::ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].logFormat = fmt;
		Update();
	}
}

static inline u32 NotCached(u32 val)
{
	// Remove the cached part of the address.
	return val & ~0x40000000;
}

MemCheck *CBreakPoints::GetMemCheck(u32 address, int size)
{
	std::vector<MemCheck>::iterator iter;
	for (iter = memChecks_.begin(); iter != memChecks_.end(); ++iter)
	{
		MemCheck &check = *iter;
		if (check.end != 0)
		{
			if (NotCached(address + size) > NotCached(check.start) && NotCached(address) < NotCached(check.end))
				return &check;
		}
		else
		{
			if (NotCached(check.start) == NotCached(address))
				return &check;
		}
	}

	//none found
	return 0;
}

BreakAction CBreakPoints::ExecMemCheck(u32 address, bool write, int size, u32 pc)
{
	auto check = GetMemCheck(address, size);
	if (check)
		return check->Action(address, write, size, pc);
	return BREAK_ACTION_IGNORE;
}

BreakAction CBreakPoints::ExecOpMemCheck(u32 address, u32 pc)
{
	// Note: currently, we don't check "on changed" for HLE (ExecMemCheck.)
	// We'd need to more carefully specify memory changes in HLE for that.
	int size = MIPSAnalyst::OpMemoryAccessSize(pc);
	if (size == 0 && MIPSAnalyst::OpHasDelaySlot(pc)) {
		// This means that the delay slot is what tripped us.
		pc += 4;
		size = MIPSAnalyst::OpMemoryAccessSize(pc);
	}

	bool write = MIPSAnalyst::IsOpMemoryWrite(pc);
	auto check = GetMemCheck(address, size);
	if (check) {
		int mask = MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE;
		if (write && (check->cond & mask) == mask) {
			if (MIPSAnalyst::OpWouldChangeMemory(pc, address, size)) {
				return check->Action(address, write, size, pc);
			}
		} else {
			return check->Action(address, write, size, pc);
		}
	}
	return BREAK_ACTION_IGNORE;
}

void CBreakPoints::ExecMemCheckJitBefore(u32 address, bool write, int size, u32 pc)
{
	auto check = GetMemCheck(address, size);
	if (check) {
		check->JitBefore(address, write, size, pc);
		cleanupMemChecks_.push_back(check);
	}
}

void CBreakPoints::ExecMemCheckJitCleanup()
{
	for (auto it = cleanupMemChecks_.begin(), end = cleanupMemChecks_.end(); it != end; ++it) {
		auto check = *it;
		check->JitCleanup();
	}
	cleanupMemChecks_.clear();
}

void CBreakPoints::SetSkipFirst(u32 pc)
{
	breakSkipFirstAt_ = pc;
	breakSkipFirstTicks_ = CoreTiming::GetTicks();
}
u32 CBreakPoints::CheckSkipFirst()
{
	u32 pc = breakSkipFirstAt_;
	if (breakSkipFirstTicks_ == CoreTiming::GetTicks())
		return pc;
	return 0;
}

const std::vector<MemCheck> CBreakPoints::GetMemCheckRanges()
{
	std::vector<MemCheck> ranges = memChecks_;
	for (auto it = memChecks_.begin(), end = memChecks_.end(); it != end; ++it)
	{
		MemCheck check = *it;
		// Toggle the cached part of the address.
		check.start ^= 0x40000000;
		if (check.end != 0)
			check.end ^= 0x40000000;
		ranges.push_back(check);
	}

	return ranges;
}

const std::vector<MemCheck> CBreakPoints::GetMemChecks()
{
	return memChecks_;
}

const std::vector<BreakPoint> CBreakPoints::GetBreakpoints()
{
	return breakPoints_;
}

bool CBreakPoints::HasMemChecks()
{
	return !memChecks_.empty();
}

void CBreakPoints::Update(u32 addr)
{
	if (MIPSComp::jit)
	{
		bool resume = false;
		if (Core_IsStepping() == false)
		{
			Core_EnableStepping(true);
			Core_WaitInactive(200);
			resume = true;
		}
		
		// In case this is a delay slot, clear the previous instruction too.
		if (addr != 0)
			MIPSComp::jit->InvalidateCacheAt(addr - 4, 8);
		else
			MIPSComp::jit->InvalidateCache();

		if (resume)
			Core_EnableStepping(false);
	}

	// Redraw in order to show the breakpoint.
	host->UpdateDisassembly();
}

bool CBreakPoints::ValidateLogFormat(DebugInterface *cpu, const std::string &fmt) {
	std::string ignore;
	return EvaluateLogFormat(cpu, fmt, ignore);
}

bool CBreakPoints::EvaluateLogFormat(DebugInterface *cpu, const std::string &fmt, std::string &result) {
	PostfixExpression exp;
	result.clear();

	size_t pos = 0;
	while (pos < fmt.size()) {
		size_t next = fmt.find_first_of("{", pos);
		if (next == fmt.npos) {
			// End of the string.
			result += fmt.substr(pos);
			break;
		}
		if (next != pos) {
			result += fmt.substr(pos, next - pos);
			pos = next;
		}

		size_t end = fmt.find_first_of("}", next + 1);
		if (end == fmt.npos) {
			// Invalid: every expression needs a { and a }.
			return false;
		}

		std::string expression = fmt.substr(next + 1, end - next - 1);
		if (expression.empty()) {
			result += "{}";
		} else {
			if (!cpu->initExpression(expression.c_str(), exp)) {
				return false;
			}

			u32 expResult;
			char resultString[32];
			if (!cpu->parseExpression(exp, expResult)) {
				return false;
			}

			snprintf(resultString, 32, "%08x", expResult);
			result += resultString;
		}

		// Skip the }.
		pos = end + 1;
	}

	return true;
}
