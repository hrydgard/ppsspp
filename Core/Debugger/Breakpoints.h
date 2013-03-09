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


#include "../../Globals.h"

struct BreakPoint
{
	u32	iAddress;
	bool bOn;
	bool bTemporary;

	bool operator == (const BreakPoint &other) const	{
		return iAddress == other.iAddress && bOn == other.bOn && bTemporary == other.bTemporary;
	}
};

struct MemCheck
{
	MemCheck();
	u32 iStartAddress;
	u32 iEndAddress;

	bool	bRange;

	bool	bOnRead;
	bool	bOnWrite;

	bool	bLog;
	bool	bBreak;

	u32 numHits;

	void Action(u32 addr, bool write, int size, u32 pc);
};

class CBreakPoints
{
private:

	enum { MAX_NUMBER_OF_CALLSTACK_ENTRIES = 16384};
	enum { MAX_NUMBER_OF_BREAKPOINTS = 16};

	static u32	m_iBreakOnCount;

public:
	// WARNING: Not used in interpreter or HLE, only jit CPU memory access.
	static std::vector<MemCheck> MemChecks;

	// is address breakpoint
	static bool IsAddressBreakPoint(u32 _iAddress);

	// WARNING: Not used in interpreter or HLE, only jit CPU memory access.
	static MemCheck *GetMemCheck(u32 address, int size);

	// is break on count
	static bool IsBreakOnCount(u32 _iAddress);

	static bool IsTempBreakPoint(u32 _iAddress);

	// AddBreakPoint
	static void AddBreakPoint(u32 _iAddress, bool temp=false);

	// Remove Breakpoint
	static void RemoveBreakPoint(u32 _iAddress);

	static void ClearAllBreakPoints();

	static void InvalidateJit(u32 _iAddress);
	static void InvalidateJit();

	static int GetNumBreakpoints();
	static int GetBreakpointAddress(int i);
};


