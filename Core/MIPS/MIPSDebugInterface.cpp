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

#ifndef _MSC_VER
#include <strings.h>
#endif

#include "Common/StringUtils.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/HLE/sceKernelThread.h"
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
	REF_INDEX_HLE      = 0x10000,
	REF_INDEX_THREAD   = REF_INDEX_HLE | 0,
	REF_INDEX_MODULE   = REF_INDEX_HLE | 1,
	REF_INDEX_USEC     = REF_INDEX_HLE | 2,
	REF_INDEX_TICKS    = REF_INDEX_HLE | 3,
};


class MipsExpressionFunctions: public IExpressionFunctions
{
public:
	MipsExpressionFunctions(DebugInterface* cpu): cpu(cpu) { }

	bool parseReference(char* str, uint32_t& referenceIndex) override
	{
		for (int i = 0; i < 32; i++)
		{
			char reg[8];
			snprintf(reg, sizeof(reg), "r%d", i);

			if (strcasecmp(str, reg) == 0 || strcasecmp(str, cpu->GetRegName(0, i).c_str()) == 0)
			{
				referenceIndex = i;
				return true;
			}
			else if (strcasecmp(str, cpu->GetRegName(1, i).c_str()) == 0)
			{
				referenceIndex = REF_INDEX_FPU | i;
				return true;
			}

			snprintf(reg, sizeof(reg), "fi%d", i);
			if (strcasecmp(str, reg) == 0)
			{
				referenceIndex = REF_INDEX_FPU_INT | i;
				return true;
			}
		}

		for (int i = 0; i < 128; i++)
		{
			if (strcasecmp(str, cpu->GetRegName(2, i).c_str()) == 0)
			{
				referenceIndex = REF_INDEX_VFPU | i;
				return true;
			}

			char reg[8];
			snprintf(reg, sizeof(reg), "vi%d", i);
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

		if (strcasecmp(str, "threadid") == 0) {
			referenceIndex = REF_INDEX_THREAD;
			return true;
		}
		if (strcasecmp(str, "moduleid") == 0) {
			referenceIndex = REF_INDEX_MODULE;
			return true;
		}
		if (strcasecmp(str, "usec") == 0) {
			referenceIndex = REF_INDEX_USEC;
			return true;
		}
		if (strcasecmp(str, "ticks") == 0) {
			referenceIndex = REF_INDEX_TICKS;
			return true;
		}

		return false;
	}

	bool parseSymbol(char* str, uint32_t& symbolValue) override
	{
		return g_symbolMap->GetLabelValue(str,symbolValue); 
	}

	uint32_t getReferenceValue(uint32_t referenceIndex) override
	{
		if (referenceIndex < 32)
			return cpu->GetRegValue(0, referenceIndex);
		if (referenceIndex == REF_INDEX_PC)
			return cpu->GetPC();
		if (referenceIndex == REF_INDEX_HI)
			return cpu->GetHi();
		if (referenceIndex == REF_INDEX_LO)
			return cpu->GetLo();
		if (referenceIndex == REF_INDEX_THREAD)
			return __KernelGetCurThread();
		if (referenceIndex == REF_INDEX_MODULE)
			return __KernelGetCurThreadModuleId();
		if (referenceIndex == REF_INDEX_USEC)
			return (uint32_t)CoreTiming::GetGlobalTimeUs();  // Loses information
		if (referenceIndex == REF_INDEX_TICKS)
			return (uint32_t)CoreTiming::GetTicks();
		if ((referenceIndex & ~(REF_INDEX_FPU | REF_INDEX_FPU_INT)) < 32)
			return cpu->GetRegValue(1, referenceIndex & ~(REF_INDEX_FPU | REF_INDEX_FPU_INT));
		if ((referenceIndex & ~(REF_INDEX_VFPU | REF_INDEX_VFPU_INT)) < 128)
			return cpu->GetRegValue(2, referenceIndex & ~(REF_INDEX_VFPU | REF_INDEX_VFPU_INT));
		return -1;
	}

	ExpressionType getReferenceType(uint32_t referenceIndex) override {
		if (referenceIndex & REF_INDEX_IS_FLOAT) {
			return EXPR_TYPE_FLOAT;
		}
		return EXPR_TYPE_UINT;
	}
	
	bool getMemoryValue(uint32_t address, int size, uint32_t& dest, std::string *error) override {
		// We allow, but ignore, bad access.
		// If we didn't, log/condition statements that reference registers couldn't be configured.
		uint32_t valid = Memory::ValidSize(address, size);
		uint8_t buf[4]{};
		if (valid != 0)
			memcpy(buf, Memory::GetPointerUnchecked(address), valid);

		switch (size) {
		case 1:
			dest = buf[0];
			return true;
		case 2:
			dest = (buf[1] << 8) | buf[0];
			return true;
		case 4:
			dest = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
			return true;
		}

		*error = StringFromFormat("Unexpected memory access size %d", size);
		return false;
	}

private:
	DebugInterface* cpu;
};



void MIPSDebugInterface::DisAsm(u32 pc, char *out, size_t outSize) {
	if (Memory::IsValidAddress(pc))
		MIPSDisAsm(Memory::Read_Opcode_JIT(pc), pc, out, outSize);
	else
		truncate_cpy(out, outSize, "-");
}

unsigned int MIPSDebugInterface::readMemory(unsigned int address) {
	if (Memory::IsValidRange(address, 4))
		return Memory::ReadUnchecked_Instruction(address).encoding;
	return 0;
}

bool MIPSDebugInterface::isAlive()
{
	return PSP_IsInited() && coreState != CORE_BOOT_ERROR && coreState != CORE_RUNTIME_ERROR && coreState != CORE_POWERDOWN;
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
	int n=g_symbolMap->GetFunctionNum(address);
	if (n==-1) return 0xFFFFFF;
	return colors[n%6];
}
std::string MIPSDebugInterface::getDescription(unsigned int address) 
{
	return g_symbolMap->GetDescription(address);
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

std::string MIPSDebugInterface::GetRegName(int cat, int index) {
	static const char * const regName[32] = {
		"zero",  "at",    "v0",    "v1",
		"a0",    "a1",    "a2",    "a3",
		"t0",    "t1",    "t2",    "t3",
		"t4",    "t5",    "t6",    "t7",
		"s0",    "s1",    "s2",    "s3",
		"s4",    "s5",    "s6",    "s7",
		"t8",    "t9",    "k0",    "k1",
		"gp",    "sp",    "fp",    "ra"
	};
	static const char * const fpRegName[32] = {
		"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
		"f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
		"f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
		"f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
	};

	if (cat == 0 && (unsigned)index < sizeof(regName)) {
		return regName[index];
	} else if (cat == 1 && (unsigned)index < sizeof(fpRegName)) {
		return fpRegName[index];
	} else if (cat == 2) {
		return GetVectorNotation(index, V_Single);
	}
	return "???";
}

