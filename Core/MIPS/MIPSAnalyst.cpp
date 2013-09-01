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

#include <map>
#include "Globals.h"

#include "Common/FileUtil.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DebugInterface.h"

using namespace MIPSCodeUtils;
using namespace std;

namespace MIPSAnalyst
{
	// Only can ever output a single reg.
	MIPSGPReg GetOutGPReg(MIPSOpcode op) {
		MIPSInfo opinfo = MIPSGetInfo(op);
		if ((opinfo & IS_VFPU) == 0) {
			if (opinfo & OUT_RT) {
				return MIPS_GET_RT(op);
			}
			if (opinfo & OUT_RD) {
				return MIPS_GET_RD(op);
			}
			if (opinfo & OUT_RA) {
				return MIPS_REG_RA;
			}
		}
		return MIPS_REG_INVALID;
	}

	bool ReadsFromGPReg(MIPSOpcode op, MIPSGPReg reg) {
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & IS_VFPU) == 0) {
			if ((info & IN_RS) != 0 && MIPS_GET_RS(op) == reg) {
				return true;
			}
			if ((info & IN_RT) != 0 && MIPS_GET_RT(op) == reg) {
				return true;
			}
		}
		return false;
	}

	bool IsDelaySlotNiceReg(MIPSOpcode branchOp, MIPSOpcode op, MIPSGPReg reg1, MIPSGPReg reg2) {
		// $0 is never an out reg, it's always 0.
		if (reg1 != MIPS_REG_ZERO && GetOutGPReg(op) == reg1) {
			return false;
		}
		if (reg2 != MIPS_REG_ZERO && GetOutGPReg(op) == reg2) {
			return false;
		}

		return true;
	}

	bool IsDelaySlotNiceVFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		// TODO: There may be IS_VFPU cases which are safe...
		return (MIPSGetInfo(op) & IS_VFPU) == 0;
	}

	bool IsDelaySlotNiceFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		return (MIPSGetInfo(op) & OUT_FPUFLAG) == 0;
	}

	bool IsSyscall(MIPSOpcode op) {
		// Syscalls look like this: 0000 00-- ---- ---- ---- --00 1100
		return (op >> 26) == 0 && (op & 0x3f) == 12;
	}

	AnalysisResults Analyze(u32 address) {
		const int MAX_ANALYZE = 10000;

		AnalysisResults results;

		//set everything to -1 (FF)
		memset(&results, 255, sizeof(AnalysisResults));
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			results.r[i].used = false;
			results.r[i].readCount = 0;
			results.r[i].writeCount = 0;
			results.r[i].readAsAddrCount = 0;
		}

		for (u32 addr = address, endAddr = address + MAX_ANALYZE; addr <= endAddr; addr += 4) {
			MIPSOpcode op = Memory::Read_Instruction(addr);
			MIPSInfo info = MIPSGetInfo(op);

			MIPSGPReg rs = MIPS_GET_RS(op);
			MIPSGPReg rt = MIPS_GET_RT(op);

			if (info & IN_RS) {
				if ((info & IN_RS_ADDR) == IN_RS_ADDR) {
					results.r[rs].MarkReadAsAddr(addr);
				} else {
					results.r[rs].MarkRead(addr);
				}
			}

			if (info & IN_RT) {
				results.r[rt].MarkRead(addr);
			}

			MIPSGPReg outReg = GetOutGPReg(op);
			if (outReg != MIPS_REG_INVALID) {
				results.r[outReg].MarkWrite(addr);
			}

			if (info & DELAYSLOT)
			{
				// Let's just finish the delay slot before bailing.
				endAddr = addr + 4;
			}
		}

		int numUsedRegs=0;
		static int totalUsedRegs=0;
		static int numAnalyzings=0;
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			if (results.r[i].used) {
				numUsedRegs++;
			}
		}
		totalUsedRegs+=numUsedRegs;
		numAnalyzings++;
		DEBUG_LOG(CPU,"[ %08x ] Used regs: %i	 Average: %f",address,numUsedRegs,(float)totalUsedRegs/(float)numAnalyzings);

		return results;
	}


	struct Function
	{
		u32 start;
		u32 end;
		u32 hash;
		u32 size;
		bool isStraightLeaf;
		bool hasHash;
		bool usesVFPU;
		char name[64];
	};

	vector<Function> functions;

	map<u32, Function*> hashToFunction;

	void Shutdown()
	{
		functions.clear();
		hashToFunction.clear();
	}

	// hm pointless :P
	void UpdateHashToFunctionMap()
	{
		hashToFunction.clear();
		for (vector<Function>::iterator iter = functions.begin(); iter != functions.end(); iter++)
		{
			Function &f = *iter;
			if (f.hasHash)
			{
				hashToFunction[f.hash] = &f;
			}
		}
	}


	bool IsRegisterUsed(MIPSGPReg reg, u32 addr)
	{
		while (true)
		{
			MIPSOpcode op = Memory::Read_Instruction(addr);
			MIPSInfo info = MIPSGetInfo(op);

			if ((info & IN_RS) && (MIPS_GET_RS(op) == reg))
				return true;
			if ((info & IN_RT) && (MIPS_GET_RT(op) == reg))
				return true;
			if ((info & IS_CONDBRANCH))
				return true; // could also follow both paths
			if ((info & IS_JUMP))
				return true; // could also follow the path
			if ((info & OUT_RT) && (MIPS_GET_RT(op) == reg))
				return false; //the reg got clobbed! yay!
			if ((info & OUT_RD) && (MIPS_GET_RD(op) == reg))
				return false; //the reg got clobbed! yay!
			if ((info & OUT_RA) && (reg == MIPS_REG_RA))
				return false; //the reg got clobbed! yay!
			addr+=4;
		}
		return true;
	}

	void HashFunctions()
	{
		for (vector<Function>::iterator iter = functions.begin(); iter!=functions.end(); iter++)
		{
			Function &f=*iter;
			u32 hash = 0x1337babe;
			for (u32 addr = f.start; addr <= f.end; addr += 4)
			{
				u32 validbits = 0xFFFFFFFF;
				MIPSOpcode instr = Memory::Read_Instruction(addr);
				MIPSInfo flags = MIPSGetInfo(instr);
				if (flags & IN_IMM16)
					validbits&=~0xFFFF;
				if (flags & IN_IMM26)
					validbits&=~0x03FFFFFF;
				hash = __rotl(hash,13);
				hash ^= (instr&validbits);
			}
			f.hash=hash;
			f.hasHash=true;
		}
	}

	void ScanForFunctions(u32 startAddr, u32 endAddr /*, std::vector<u32> knownEntries*/)
	{
		Function currentFunction = {startAddr};

		u32 furthestBranch = 0;
		bool looking = false;
		bool end = false;
		bool isStraightLeaf=true;
		u32 addr;
		for (addr = startAddr; addr<=endAddr; addr+=4)
		{
			SymbolInfo syminfo;
			if (symbolMap.GetSymbolInfo(&syminfo, addr, ST_FUNCTION))
			{
				addr = syminfo.address + syminfo.size;
				continue;
			}

			MIPSOpcode op = Memory::Read_Instruction(addr);
			u32 target = GetBranchTargetNoRA(addr);
			if (target != INVALIDTARGET)
			{
				isStraightLeaf = false;
				if (target > furthestBranch)
				{
					furthestBranch = target;
				}
			}
			if (op == MIPS_MAKE_JR_RA())
			{
				if (furthestBranch >= addr)
				{
					looking = true;
					addr+=4;
				}
				else
				{
					end = true;
				}
			}

			if (looking)
			{
				if (addr >= furthestBranch)
				{
					u32 sureTarget = GetSureBranchTarget(addr);
					if (sureTarget != INVALIDTARGET && sureTarget < addr)
					{
						end = true;
					}
					sureTarget = GetJumpTarget(addr);
					if (sureTarget != INVALIDTARGET && sureTarget < addr && ((op&0xFC000000)==0x08000000))
					{
						end = true;
					}
					//end = true;
				}
			}
			if (end)
			{
				currentFunction.end = addr + 4;
				currentFunction.isStraightLeaf = isStraightLeaf;
				functions.push_back(currentFunction);
				furthestBranch = 0;
				addr += 4;
				looking = false;
				end = false;
				isStraightLeaf=true;
				currentFunction.start = addr+4;
			}
		}
		currentFunction.end = addr + 4;
		functions.push_back(currentFunction);

		for (vector<Function>::iterator iter = functions.begin(); iter!=functions.end(); iter++)
		{
			(*iter).size = ((*iter).end-(*iter).start+4);
			char temp[256];
			sprintf(temp,"z_un_%08x",(*iter).start);
			symbolMap.AddSymbol(std::string(temp).c_str(), (*iter).start,(*iter).end-(*iter).start+4,ST_FUNCTION);
		}
		HashFunctions();
	}

	struct HashMapFunc
	{
		char name[64];
		u32 hash;
		u32 size; //number of bytes
	};

	void StoreHashMap(const char *filename)
	{
		FILE *file = File::OpenCFile(filename,"wb");
		u32 num = 0;
		if(fwrite(&num,4,1,file) != 1) //fill in later
			WARN_LOG(CPU, "Could not store hash map %s", filename);

		for (vector<Function>::iterator iter = functions.begin(); iter!=functions.end(); iter++)
		{
			Function &f=*iter;
			if (f.hasHash && f.size>=12)
			{
				HashMapFunc temp;
				memset(&temp,0,sizeof(temp));
				strcpy(temp.name, f.name);
				temp.hash=f.hash;
				temp.size=f.size;
				if(fwrite((char*)&temp,sizeof(temp),1,file) != 1) {
					WARN_LOG(CPU, "Could not store hash map %s", filename);
					break;
				}
				num++;
			}
		}
		fseek(file,0,SEEK_SET);
		if(fwrite(&num,4,1,file) != 1) //fill in later
			WARN_LOG(CPU, "Could not store hash map %s", filename);
		fclose(file);
	}


	void LoadHashMap(const char *filename)
	{
		HashFunctions();
		UpdateHashToFunctionMap();

		FILE *file = File::OpenCFile(filename, "rb");
		int num;
		if(fread(&num,4,1,file) == 1) {
			for (int i=0; i<num; i++)
			{
				HashMapFunc temp;
				if(fread(&temp,sizeof(temp),1,file) == 1) {
					map<u32,Function*>::iterator iter = hashToFunction.find(temp.hash);
					if (iter != hashToFunction.end())
					{
						//yay, found a function!
						Function &f = *(iter->second);
						if (f.size==temp.size)
						{
							strcpy(f.name, temp.name);
							f.hash=temp.hash;
							f.size=temp.size;
						}
					}
				}
			}
		}
		fclose(file);
	}
	void CompileLeafs()
	{
		/*
		int count=0;
		for (vector<Function>::iterator iter = functions.begin(); iter!=functions.end(); iter++)
		{
		Function &f = *iter;
		if (f.isStraightLeaf)
		{
		MIPSComp::CompileAt(f.start);
		count++;
		}
		}
		LOG(CPU,"Precompiled %i straight leaf functions",count);*/
	}

	std::vector<MIPSGPReg> GetInputRegs(MIPSOpcode op)
	{
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & IS_VFPU) == 0)
		{
			if (info & IN_RS) vec.push_back(MIPS_GET_RS(op));
			if (info & IN_RT) vec.push_back(MIPS_GET_RT(op));
		}
		return vec;
	}
	std::vector<MIPSGPReg> GetOutputRegs(MIPSOpcode op)
	{
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & IS_VFPU) == 0)
		{
			if (info & OUT_RD) vec.push_back(MIPS_GET_RD(op));
			if (info & OUT_RT) vec.push_back(MIPS_GET_RT(op));
			if (info & OUT_RA) vec.push_back(MIPS_REG_RA);
		}
		return vec;
	}

	MipsOpcodeInfo GetOpcodeInfo(DebugInterface* cpu, u32 address)
	{
		MipsOpcodeInfo info;
		memset(&info,0,sizeof(info));

		if (!Memory::IsValidAddress(address)) {
			return info;
		}

		info.cpu = cpu;
		info.opcodeAddress = address;
		info.encodedOpcode = Memory::Read_Instruction(address);

		MIPSOpcode op = info.encodedOpcode;
		MIPSInfo opInfo = MIPSGetInfo(op);
		info.isLikelyBranch = (opInfo & LIKELY) != 0;
		
		//j , jal, ...
		if (opInfo & IS_JUMP)
		{
			info.isBranch = true;
			if (opInfo & OUT_RA)	// link
			{
				info.isLinkedBranch = true;
			}

			if (opInfo & IN_RS)		// to register
			{
				info.isBranchToRegister = true;
				info.branchRegisterNum = (int)MIPS_GET_RS(op);
				info.branchTarget = cpu->GetRegValue(0,info.branchRegisterNum);
			} else {				// to immediate
				info.branchTarget = GetJumpTarget(address);
			}
		}

		// movn, movz
		if (opInfo & IS_CONDMOVE)
		{
			info.isConditional = true;

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			switch (opInfo & CONDTYPE_MASK)
			{
			case CONDTYPE_EQ:
				info.conditionMet = (rt == 0);
				break;
			case CONDTYPE_NE:
				info.conditionMet = (rt != 0);
				break;
			}
		}

		// beq, bgtz, ...
		if (opInfo & IS_CONDBRANCH)
		{
			info.isBranch = true;
			info.isConditional = true;
			info.branchTarget = GetBranchTarget(address);
			
			if (opInfo & OUT_RA)	// link
			{
				info.isLinkedBranch = true;
			}

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			switch (opInfo & CONDTYPE_MASK)
			{
			case CONDTYPE_EQ:
				info.conditionMet = (rt == rs);
				if (MIPS_GET_RT(op) == MIPS_GET_RS(op))		// always true
				{
					info.isConditional = false;
				}
				break;
			case CONDTYPE_NE:
				info.conditionMet = (rt != rs);
				if (MIPS_GET_RT(op) == MIPS_GET_RS(op))		// always true
				{
					info.isConditional = false;
				}
				break;
			case CONDTYPE_LEZ:
				info.conditionMet = (((s32)rs) <= 0);
				break;
			case CONDTYPE_GTZ:
				info.conditionMet = (((s32)rs) > 0);
				break;
			case CONDTYPE_LTZ:
				info.conditionMet = (((s32)rs) < 0);
				break;
			case CONDTYPE_GEZ:
				info.conditionMet = (((s32)rs) >= 0);
				break;
			}
		}

		// lw, sh, ...
		if ((opInfo & IN_MEM) || (opInfo & OUT_MEM))
		{
			info.isDataAccess = true;
			
			switch (opInfo & MEMTYPE_MASK)
			{
			case MEMTYPE_BYTE:
				info.dataSize = 1;
				break;
			case MEMTYPE_HWORD:
				info.dataSize = 2;
				break;
			case MEMTYPE_WORD:
			case MEMTYPE_FLOAT:
				info.dataSize = 4;
				break;

			case MEMTYPE_VQUAD:
				info.dataSize = 16;
			}

			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			s16 imm16 = op & 0xFFFF;
			info.dataAddress = rs + imm16;
		}

		return info;
	}
}
