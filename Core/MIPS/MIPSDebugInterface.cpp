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

#include "../Debugger/Breakpoints.h"
#include "../Debugger/SymbolMap.h"
#include "../Debugger/DebugInterface.h"

#include "MIPSDebugInterface.h"
#include "../Globals.h"
#include "../MemMap.h"	
#include "../MIPS/MIPSTables.h"	
#include "../MIPS/MIPS.h"
#include "../System.h"


class MipsExpressionFunctions: public IExpressionFunctions
{
public:
	MipsExpressionFunctions(DebugInterface* cpu): cpu(cpu) { };

	virtual bool parseReference(char* str, uint32& referenceIndex)
	{
		for (int i = 0; i < 32; i++)
		{
			char reg[8];
			sprintf(reg,"r%d",i);

			if (strcasecmp(str,reg) == 0 || strcasecmp(str,cpu->GetRegName(0,i)) == 0)
			{
				referenceIndex = i;
				return true;
			}
		}

		if (strcasecmp(str,"pc") == 0)
		{
			referenceIndex = 32;
			return true;
		} 

		return false;
	}

	virtual bool parseSymbol(char* str, uint32& symbolValue)
	{
		return cpu->getSymbolValue(str,symbolValue); 
	}

	virtual uint32 getReferenceValue(uint32 referenceIndex)
	{
		if (referenceIndex < 32) return cpu->GetRegValue(0,referenceIndex);
		if (referenceIndex == 32) return cpu->GetPC();
		return -1;
	}
	
	virtual bool getMemoryValue(uint32 address, int size, uint32& dest, char* error)
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
	MIPSState *x = currentCPU;
	currentCPU = cpu;
	
	static char mojs[256]; 
	if (Memory::IsValidAddress(address))
		MIPSDisAsm(Memory::Read_Opcode_JIT(address), address, mojs);
	else
		strcpy(mojs, "-");
	currentCPU = x;
	return mojs;
}
unsigned int MIPSDebugInterface::readMemory(unsigned int address)
{
	return Memory::Read_Instruction(address).encoding;
}

bool MIPSDebugInterface::isAlive()
{
	return PSP_IsInited();
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
	int n=symbolMap.GetSymbolNum(address);
	if (n==-1 || symbolMap.GetSymbolSize(n) < 4) return 0xFFFFFF;
	return colors[n%6];
}
const char *MIPSDebugInterface::getDescription(unsigned int address) 
{
	return symbolMap.GetDescription(address);
}

const char *MIPSDebugInterface::findSymbolForAddress(unsigned int address)
{
	return symbolMap.getDirectSymbol(address);
}

bool MIPSDebugInterface::getSymbolValue(char* symbol, u32& dest)
{
	return symbolMap.getSymbolValue(symbol,dest);
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
		"zero",	"at",	"v0",	"v1",
		"a0",		"a1",	"a2",	"a3",
		"t0",		"t1",	"t2",	"t3",
		"t4",		"t5",	"t6",	"t7",
		"s0",		"s1",	"s2",	"s3",
		"s4",		"s5",	"s6",	"s7",
		"t8",		"t9",	"k0",	"k1",
		"gp",		"sp",	"fp",	"ra"
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

