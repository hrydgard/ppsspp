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

namespace MIPSAnalyst
{
	void Analyze(u32 address);
	struct RegisterAnalysisResults
	{
		bool used;
		int firstRead;
		int lastRead;
		int firstWrite;
		int lastWrite;
		int firstReadAsAddr;
		int lastReadAsAddr;

		int readCount;
		int writeCount;
		int readAsAddrCount;
		bool usesVFPU;

		int TotalReadCount() {return readCount + readAsAddrCount;}
		int FirstRead() {return firstReadAsAddr < firstRead ? firstReadAsAddr : firstRead;}
		int LastRead() {return lastReadAsAddr > lastRead ? lastReadAsAddr : lastRead;}
	};

	struct AnalysisResults
	{
		int x;
	};

	bool IsRegisterUsed(u32 reg, u32 addr);
	void ScanForFunctions(u32 startAddr, u32 endAddr);
	void CompileLeafs();

	std::vector<int> GetInputRegs(u32 op);
	std::vector<int> GetOutputRegs(u32 op);

	int GetOutReg(u32 op);
	bool ReadsFromReg(u32 op, u32 reg);
	bool IsDelaySlotNice(u32 branch, u32 delayslot);
	bool IsDelaySlotNiceReg(u32 branchOp, u32 op, int reg1, int reg2 = 0);
	bool IsDelaySlotNiceVFPU(u32 branchOp, u32 op);
	bool IsDelaySlotNiceFPU(u32 branchOp, u32 op);
	bool IsSyscall(u32 op);

	void Shutdown();

}	// namespace MIPSAnalyst
