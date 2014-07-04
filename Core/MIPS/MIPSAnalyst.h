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

#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

class DebugInterface;

namespace MIPSAnalyst
{
	const int MIPS_NUM_GPRS = 32;

	struct RegisterAnalysisResults {
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

		int TotalReadCount() const { return readCount + readAsAddrCount; }
		int FirstRead() const { return firstReadAsAddr < firstRead ? firstReadAsAddr : firstRead; }
		int LastRead() const { return lastReadAsAddr > lastRead ? lastReadAsAddr : lastRead; }

		void MarkRead(u32 addr) {
			if (firstRead == -1)
				firstRead = addr;
			lastRead = addr;
			readCount++;
			used = true;
		}

		void MarkReadAsAddr(u32 addr) {
			if (firstReadAsAddr == -1)
				firstReadAsAddr = addr;
			lastReadAsAddr = addr;
			readAsAddrCount++;
			used = true;
		}

		void MarkWrite(u32 addr) {
			if (firstWrite == -1)
				firstWrite = addr;
			lastWrite = addr;
			writeCount++;
			used = true;
		}
	};

	struct AnalysisResults {
		RegisterAnalysisResults r[MIPS_NUM_GPRS];
	};

	AnalysisResults Analyze(u32 address);

	bool IsRegisterUsed(MIPSGPReg reg, u32 addr);

	struct AnalyzedFunction {
		u32 start;
		u32 end;
		u64 hash;
		u32 size;
		bool isStraightLeaf;
		bool hasHash;
		bool usesVFPU;
		bool foundInSymbolMap;
		char name[64];
	};

	struct ReplacementTableEntry;

	void Reset();

	bool IsRegisterUsed(u32 reg, u32 addr);
	// This will not only create a database of "AnalyzedFunction" structs, it also
	// will insert all the functions it finds into the symbol map, if insertSymbols is true.

	// If we have loaded symbols from the elf, we'll register functions as they are touched
	// so that we don't just dump them all in the cache.
	void RegisterFunction(u32 startAddr, u32 size, const char *name);
	void ScanForFunctions(u32 startAddr, u32 endAddr, bool insertSymbols);
	void ForgetFunctions(u32 startAddr, u32 endAddr);
	void CompileLeafs();

	void SetHashMapFilename(std::string filename = "");
	void LoadBuiltinHashMap();
	void LoadHashMap(std::string filename);
	void StoreHashMap(std::string filename = "");

	const char *LookupHash(u64 hash, u32 funcSize);
	void ReplaceFunctions();

	void UpdateHashMap();
	void ApplyHashMap();

	std::vector<MIPSGPReg> GetInputRegs(MIPSOpcode op);
	std::vector<MIPSGPReg> GetOutputRegs(MIPSOpcode op);

	MIPSGPReg GetOutGPReg(MIPSOpcode op);
	bool ReadsFromGPReg(MIPSOpcode op, MIPSGPReg reg);
	bool IsDelaySlotNiceReg(MIPSOpcode branchOp, MIPSOpcode op, MIPSGPReg reg1, MIPSGPReg reg2 = MIPS_REG_ZERO);
	bool IsDelaySlotNiceVFPU(MIPSOpcode branchOp, MIPSOpcode op);
	bool IsDelaySlotNiceFPU(MIPSOpcode branchOp, MIPSOpcode op);
	bool IsSyscall(MIPSOpcode op);

	bool OpWouldChangeMemory(u32 pc, u32 addr, u32 size);

	void Shutdown();
	
	typedef struct {
		DebugInterface* cpu;
		u32 opcodeAddress;
		MIPSOpcode encodedOpcode;
		
		// shared between branches and conditional moves
		bool isConditional;
		bool conditionMet;

		// branches
		u32 branchTarget;
		bool isBranch;
		bool isLinkedBranch;
		bool isLikelyBranch;
		bool isBranchToRegister;
		int branchRegisterNum;

		// data access
		bool isDataAccess;
		int dataSize;
		u32 dataAddress;

		bool hasRelevantAddress;
		u32 releventAddress;
	} MipsOpcodeInfo;

	MipsOpcodeInfo GetOpcodeInfo(DebugInterface* cpu, u32 address);

}	// namespace MIPSAnalyst
