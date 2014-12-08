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

#include <string>
#include <cstring>

#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/MIPS/MIPSDebugInterface.h"

#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"

enum ReferenceIndexType {
	REF_INDEX_PC       = 32,
	REF_INDEX_HI       = 33,
	REF_INDEX_LO       = 34,
	REF_INDEX_FPU      = 0x1000,
	REF_INDEX_FPU_INT  = 0x2000,
	REF_INDEX_VFPU     = 0x4000,
	REF_INDEX_VFPU_INT = 0x8000,
	REF_INDEX_IS_FLOAT = REF_INDEX_FPU | REF_INDEX_VFPU,
};


class MipsExpressionFunctions: public IExpressionFunctions
{
public:
	MipsExpressionFunctions(DebugInterface* cpu): cpu(cpu) { };

	bool parseReference(char* str, uint32& referenceIndex) override
	{
		for (int i = 0; i < 32; i++)
		{
			char reg[8];
			sprintf(reg, "r%d", i);

			if (strcasecmp(str, reg) == 0 || strcasecmp(str, cpu->GetRegName(0, i)) == 0)
			{
				referenceIndex = i;
				return true;
			}
			else if (strcasecmp(str, cpu->GetRegName(1, i)) == 0)
			{
				referenceIndex = REF_INDEX_FPU | i;
				return true;
			}

			sprintf(reg, "fi%d", i);
			if (strcasecmp(str, reg) == 0)
			{
				referenceIndex = REF_INDEX_FPU_INT | i;
				return true;
			}
		}

		for (int i = 0; i < 128; i++)
		{
			if (strcasecmp(str, cpu->GetRegName(2, i)) == 0)
			{
				referenceIndex = REF_INDEX_VFPU | i;
				return true;
			}

			char reg[8];
			sprintf(reg, "vi%d", i);
			if (strcasecmp(str, reg) == 0)
			{
				referenceIndex = REF_INDEX_VFPU_INT | i;
				return true;
			}
		}

		if (strcasecmp(str, "pc") == 0)
		{
			referenceIndex = REF_INDEX_PC;
			return true;
		}

		if (strcasecmp(str, "hi") == 0)
		{
			referenceIndex = REF_INDEX_HI;
			return true;
		}

		if (strcasecmp(str, "lo") == 0)
		{
			referenceIndex = REF_INDEX_LO;
			return true;
		}

		return false;
	}

	bool parseSymbol(char* str, uint32& symbolValue) override
	{
		return symbolMap.GetLabelValue(str,symbolValue); 
	}

	uint32 getReferenceValue(uint32 referenceIndex) override
	{
		if (referenceIndex < 32)
			return cpu->GetRegValue(0, referenceIndex);
		if (referenceIndex == REF_INDEX_PC)
			return cpu->GetPC();
		if (referenceIndex == REF_INDEX_HI)
			return cpu->GetHi();
		if (referenceIndex == REF_INDEX_LO)
			return cpu->GetLo();
		if ((referenceIndex & ~(REF_INDEX_FPU | REF_INDEX_FPU_INT)) < 32)
			return cpu->GetRegValue(1, referenceIndex & ~(REF_INDEX_FPU | REF_INDEX_FPU_INT));
		if ((referenceIndex & ~(REF_INDEX_VFPU | REF_INDEX_VFPU_INT)) < 128)
			return cpu->GetRegValue(2, referenceIndex & ~(REF_INDEX_VFPU | REF_INDEX_VFPU_INT));
		return -1;
	}

	ExpressionType getReferenceType(uint32 referenceIndex) override {
		if (referenceIndex & REF_INDEX_IS_FLOAT) {
			return EXPR_TYPE_FLOAT;
		}
		return EXPR_TYPE_UINT;
	}
	
	bool getMemoryValue(uint32 address, int size, uint32& dest, char* error) override
	{
		switch (size)
		{
		case 1: case 2: case 4:
			break;
		default:
			sprintf(error,"Invalid memory access size %d",size);
			return false;
		}

		if (address % size)
		{
			sprintf(error,"Invalid memory access (unaligned)");
			return false;
		}

		switch (size)
		{
		case 1:
			dest = Memory::Read_U8(address);
			break;
		case 2:
			dest = Memory::Read_U16(address);
			break;
		case 4:
			dest = Memory::Read_U32(address);
			break;
		}

		return true;
	}

private:
	DebugInterface* cpu;
};



const char *MIPSDebugInterface::disasm(unsigned int address, unsigned int align)
{
	static char mojs[256];
	if (Memory::IsValidAddress(address))
		MIPSDisAsm(Memory::Read_Opcode_JIT(address), address, mojs);
	else
		strcpy(mojs, "-");
	return mojs;
}

unsigned int MIPSDebugInterface::readMemory(unsigned int address)
{
	return Memory::Read_Instruction(address).encoding;
}

bool MIPSDebugInterface::isAlive()
{
	return PSP_IsInited() && coreState != CORE_ERROR && coreState != CORE_POWERDOWN;
}

bool MIPSDebugInterface::isBreakpoint(unsigned int address) 
{
	return CBreakPoints::IsAddressBreakPoint(address);
}

void MIPSDebugInterface::setBreakpoint(unsigned int address)
{
	CBreakPoints::AddBreakPoint(address);
}
void MIPSDebugInterface::clearBreakpoint(unsigned int address)
{
	CBreakPoints::RemoveBreakPoint(address);
}
void MIPSDebugInterface::clearAllBreakpoints() {}
void MIPSDebugInterface::toggleBreakpoint(unsigned int address)
{
	CBreakPoints::IsAddressBreakPoint(address)?CBreakPoints::RemoveBreakPoint(address):CBreakPoints::AddBreakPoint(address);
}


int MIPSDebugInterface::getColor(unsigned int address)
{
	int colors[6] = {0xe0FFFF,0xFFe0e0,0xe8e8FF,0xFFe0FF,0xe0FFe0,0xFFFFe0};
	int n=symbolMap.GetFunctionNum(address);
	if (n==-1) return 0xFFFFFF;
	return colors[n%6];
}
std::string MIPSDebugInterface::getDescription(unsigned int address) 
{
	return symbolMap.GetDescription(address);
}

bool MIPSDebugInterface::initExpression(const char* exp, PostfixExpression& dest)
{
	MipsExpressionFunctions funcs(this);
	return initPostfixExpression(exp,&funcs,dest);
}

bool MIPSDebugInterface::parseExpression(PostfixExpression& exp, u32& dest)
{
	MipsExpressionFunctions funcs(this);
	return parsePostfixExpression(exp,&funcs,dest);
}

void MIPSDebugInterface::runToBreakpoint() 
{

}

const char *MIPSDebugInterface::GetName()
{
	return ("R4");
}


const char *MIPSDebugInterface::GetRegName(int cat, int index)
{
	static const char *regName[32] = {
		"zero",  "at",    "v0",    "v1",
		"a0",    "a1",    "a2",    "a3",
		"t0",    "t1",    "t2",    "t3",
		"t4",    "t5",    "t6",    "t7",
		"s0",    "s1",    "s2",    "s3",
		"s4",    "s5",    "s6",    "s7",
		"t8",    "t9",    "k0",    "k1",
		"gp",    "sp",    "fp",    "ra"
	};

	// really nasty hack so that this function can be called several times on one line of c++.
	static int access=0;
	access++;
	access &= 3;
	static char temp[4][16];

	if (cat == 0)
		return regName[index];
	else if (cat == 1)
	{
		sprintf(temp[access],"f%i",index);
		return temp[access];
	}
	else if (cat == 2)
	{
		sprintf(temp[access],"v%03x",index);
		return temp[access];
	}
	else
		return "???";
}

