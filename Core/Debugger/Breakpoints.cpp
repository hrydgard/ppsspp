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

#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Host.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"
#include <cstdio>

std::vector<BreakPoint> CBreakPoints::breakPoints_;
u32 CBreakPoints::breakSkipFirstAt_ = 0;
u64 CBreakPoints::breakSkipFirstTicks_ = 0;
std::vector<MemCheck> CBreakPoints::memChecks_;

MemCheck::MemCheck(void)
{
	numHits=0;
}

void MemCheck::Action(u32 addr, bool write, int size, u32 pc)
{
	int mask = write ? MEMCHECK_WRITE : MEMCHECK_READ;
	if (cond & mask)
	{
		++numHits;

		if (result & MEMCHECK_LOG)
			NOTICE_LOG(MEMMAP, "CHK %s%i at %08x (%s), PC=%08x (%s)", write ? "Write" : "Read", size * 8, addr, symbolMap.GetDescription(addr), pc, symbolMap.GetDescription(pc));
		if (result & MEMCHECK_BREAK)
		{
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
	}
}

size_t CBreakPoints::FindBreakpoint(u32 addr, bool matchTemp, bool temp)
{
	for (size_t i = 0; i < breakPoints_.size(); ++i)
	{
		if (breakPoints_[i].addr == addr && (!matchTemp || breakPoints_[i].temporary == temp))
			return i;
	}

	return INVALID_BREAKPOINT;
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
	return bp != INVALID_BREAKPOINT && breakPoints_[bp].enabled;
}

bool CBreakPoints::IsAddressBreakPoint(u32 addr, bool* enabled)
{
	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) return false;
	if (enabled != NULL) *enabled = breakPoints_[bp].enabled;
	return true;
}

bool CBreakPoints::IsTempBreakPoint(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, true);
	return bp != INVALID_BREAKPOINT;
}

void CBreakPoints::AddBreakPoint(u32 addr, bool temp)
{
	size_t bp = FindBreakpoint(addr, true, temp);
	if (bp == INVALID_BREAKPOINT)
	{
		BreakPoint pt;
		pt.enabled = true;
		pt.temporary = temp;
		pt.addr = addr;

		breakPoints_.push_back(pt);
		Update(addr);
	}
	else if (!breakPoints_[bp].enabled)
	{
		breakPoints_[bp].enabled = true;
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
		breakPoints_[bp].enabled = status;
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
		Update();
	}
}

void CBreakPoints::ChangeBreakPointRemoveCond(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = false;
		Update();
	}
}

BreakPointCond *CBreakPoints::GetBreakPointCondition(u32 addr)
{
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT && breakPoints_[bp].hasCond)
		return &breakPoints_[bp].cond;
	return NULL;
}

void CBreakPoints::AddMemCheck(u32 start, u32 end, MemCheckCondition cond, MemCheckResult result)
{
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
		memChecks_[mc].result = (MemCheckResult)(memChecks_[mc].result | result);
		Update();
	}
}

void CBreakPoints::RemoveMemCheck(u32 start, u32 end)
{
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_.erase(memChecks_.begin() + mc);
		Update();
	}
}

void CBreakPoints::ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, MemCheckResult result)
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
	if (!memChecks_.empty())
	{
		memChecks_.clear();
		Update();
	}
}

MemCheck *CBreakPoints::GetMemCheck(u32 address, int size)
{
	std::vector<MemCheck>::iterator iter;
	for (iter = memChecks_.begin(); iter != memChecks_.end(); ++iter)
	{
		MemCheck &check = *iter;
		if (check.end != 0)
		{
			if (address + size > check.start && address < check.end)
				return &check;
		}
		else
		{
			if (check.start == address)
				return &check;
		}
	}

	//none found
	return 0;
}

void CBreakPoints::SetSkipFirst(u32 pc)
{
	breakSkipFirstAt_ = pc;
	breakSkipFirstTicks_ = CoreTiming::GetTicks();
}
u32 CBreakPoints::CheckSkipFirst()
{
	u32 pc = breakSkipFirstAt_;
	breakSkipFirstAt_ = 0;
	if (breakSkipFirstTicks_ == CoreTiming::GetTicks())
		return pc;
	return 0;
}

const std::vector<MemCheck> CBreakPoints::GetMemChecks()
{
	return memChecks_;
}

const std::vector<BreakPoint> CBreakPoints::GetBreakpoints()
{
	return breakPoints_;
}

void CBreakPoints::Update(u32 addr)
{
	if (MIPSComp::jit && Core_IsInactive())
	{
		if (addr != 0)
			MIPSComp::jit->ClearCacheAt(addr);
		else
			MIPSComp::jit->ClearCache();
	}

	// Redraw in order to show the breakpoint.
	host->UpdateDisassembly();
}
