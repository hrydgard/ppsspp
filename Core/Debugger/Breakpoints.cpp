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

#include "../Core.h"
#include "Breakpoints.h"
#include "SymbolMap.h"
#include "FixedSizeUnorderedSet.h"
#include "Core/Host.h"
#include "../MIPS/JitCommon/JitCommon.h"
#include <cstdio>

#define MAX_BREAKPOINTS 16

static FixedSizeUnorderedSet<BreakPoint, MAX_BREAKPOINTS> m_iBreakPoints;

std::vector<MemCheck>		CBreakPoints::MemChecks;
u32 CBreakPoints::m_iBreakOnCount = 0;

MemCheck::MemCheck(void)
{
	numHits=0;
}

void MemCheck::Action(u32 addr, bool write, int size, u32 pc)
{
	if ((write && bOnWrite) || (!write && bOnRead))
	{
		++numHits;

		if (bLog)
			NOTICE_LOG(MEMMAP, "CHK %s%i at %08x (%s), PC=%08x (%s)", write ? "Write" : "Read", size * 8, addr, symbolMap.GetDescription(addr), pc, symbolMap.GetDescription(pc));
		if (bBreak)
		{
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
	}
}

bool CBreakPoints::IsAddressBreakPoint(u32 _iAddress)
{
	for (size_t i = 0; i < m_iBreakPoints.size(); i++)
		if (m_iBreakPoints[i].iAddress == _iAddress)
			return true;

	return false;
}

bool CBreakPoints::IsTempBreakPoint(u32 _iAddress)
{
	for (size_t i = 0; i < m_iBreakPoints.size(); i++)
		if (m_iBreakPoints[i].iAddress == _iAddress && m_iBreakPoints[i].bTemporary)
			return true;

	return false;
}

void CBreakPoints::RemoveBreakPoint(u32 _iAddress)
{
	for (size_t i = 0; i < m_iBreakPoints.size(); i++)
	{
		if (m_iBreakPoints[i].iAddress == _iAddress)
		{
			m_iBreakPoints.remove(m_iBreakPoints[i]);
			InvalidateJit(_iAddress);
			break;
		}
	}
}

void CBreakPoints::ClearAllBreakPoints()
{
	m_iBreakPoints.clear();
	InvalidateJit();
}

MemCheck *CBreakPoints::GetMemCheck(u32 address, int size)
{
	std::vector<MemCheck>::iterator iter;
	for (iter = MemChecks.begin(); iter != MemChecks.end(); ++iter)
	{
		MemCheck &check = *iter;
		if (check.bRange)
		{
			if (address >= check.iStartAddress && address + size < check.iEndAddress)
				return &check;
		}
		else
		{
			if (check.iStartAddress==address)
				return &check;
		}
	}

	//none found
	return 0;
}

bool CBreakPoints::IsBreakOnCount(u32 _iCount)
{
	if ((_iCount == m_iBreakOnCount) && 
		(m_iBreakOnCount != 0))
		return true;

	return false;
}

void CBreakPoints::AddBreakPoint(u32 _iAddress, bool temp)
{
	if (!IsAddressBreakPoint(_iAddress))
	{
		BreakPoint pt;
		pt.bOn=true;
		pt.bTemporary=temp;
		pt.iAddress = _iAddress;

		m_iBreakPoints.insert(pt);
		InvalidateJit(_iAddress);
	}
}

void CBreakPoints::InvalidateJit(u32 _iAddress)
{
	// Don't want to clear cache while running, I think?
	if (MIPSComp::jit && Core_IsInactive())
		MIPSComp::jit->ClearCacheAt(_iAddress);
}

void CBreakPoints::InvalidateJit()
{
	// Don't want to clear cache while running, I think?
	if (MIPSComp::jit && Core_IsInactive())
		MIPSComp::jit->ClearCache();
}

int CBreakPoints::GetNumBreakpoints()
{
	return (int)m_iBreakPoints.size();
}

int CBreakPoints::GetBreakpointAddress(int i)
{
	return m_iBreakPoints[i].iAddress;
}
