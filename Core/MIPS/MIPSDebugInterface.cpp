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
	return Memory::Read_U32(address);
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
	if (n==-1) return 0xFFFFFF;
	return colors[n%6];
}
const char *MIPSDebugInterface::getDescription(unsigned int address) 
{
	return symbolMap.GetDescription(address);
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

