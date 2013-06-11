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

#include "MIPS.h"
#include "../Debugger/DebugInterface.h"

class MIPSDebugInterface : public DebugInterface
{
	MIPSState *cpu;
public:
	MIPSDebugInterface(MIPSState *_cpu){cpu=_cpu;}
	virtual const char *disasm(unsigned int address, unsigned int align);
	virtual int getInstructionSize(int instruction) {return 4;}
	virtual bool isAlive();
	virtual bool isBreakpoint(unsigned int address);
	virtual void setBreakpoint(unsigned int address);
	virtual void clearBreakpoint(unsigned int address);
	virtual void clearAllBreakpoints();
	virtual void toggleBreakpoint(unsigned int address);
	virtual unsigned int readMemory(unsigned int address);
	virtual unsigned int getPC() { return cpu->pc; }
	virtual void setPC(unsigned int address) {cpu->pc = address;}
	virtual void step() {}
	virtual void runToBreakpoint();
	virtual int getColor(unsigned int address);
	virtual const char *getDescription(unsigned int address);

	//overridden functions
	const char *GetName();
	int GetGPRSize() { return GPR_SIZE_32;}
	u32 GetGPR32Value(int reg) {return cpu->r[reg];}
	u32 GetPC() {return cpu->pc;}
	u32 GetLR() {return cpu->r[MIPS_REG_RA];}
	void SetPC(u32 _pc) {cpu->pc=_pc;}

	const char *GetCategoryName(int cat)
	{
		const char *names[3] = {("GPR"),("FPU"),("VFPU")};
		return names[cat];
	}
	int GetNumCategories() { return 3; }
	int GetNumRegsInCategory(int cat)
	{
		int r[3] = {32,32,32};
		return r[cat];
	}
	const char *GetRegName(int cat, int index);

	virtual void PrintRegValue(int cat, int index, char *out)
	{
		switch (cat)
		{
		case 0:	sprintf(out, "%08x", cpu->r[index]); break;
		case 1:	sprintf(out, "%f", cpu->f[index]); break;
		case 2:	sprintf(out, "N/A"); break;
		}
	}

	u32 GetRegValue(int cat, int index)
	{
		u32 temp;
		switch (cat)
		{
		case 0:
			return cpu->r[index];

		case 1:
			memcpy(&temp, &cpu->f[index], 4);
			return temp;

		case 2:
			memcpy(&temp, &cpu->v[index], 4);
			return temp;

		default:
			return 0;
		}
	}

	void SetRegValue(int cat, int index, u32 value)
	{
		switch (cat)
		{
		case 0:
			cpu->r[index] = value;
			break;

		case 1:
			memcpy(&cpu->f[index], &value, 4);
			break;

		case 2:
			memcpy(&cpu->v[index], &value, 4);
			break;

		default:
			break;
		}
	}
};
