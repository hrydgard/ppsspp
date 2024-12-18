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
#include <cstdio>
#include "Common/Math/expression_parser.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Debugger/DebugInterface.h"


class MIPSDebugInterface : public DebugInterface
{
	MIPSState *cpu;
public:
	MIPSDebugInterface(MIPSState *_cpu) { cpu = _cpu; }
	int getInstructionSize(int instruction) { return 4; }
	bool isAlive();
	bool isBreakpoint(unsigned int address);
	void setBreakpoint(unsigned int address);
	void clearBreakpoint(unsigned int address);
	void clearAllBreakpoints();
	void toggleBreakpoint(unsigned int address);
	unsigned int readMemory(unsigned int address);
	int getColor(unsigned int address, bool darkMode) const;
	std::string getDescription(unsigned int address);

	u32 GetGPR32Value(int reg) const override { return cpu->r[reg]; }
	float GetFPR32Value(int reg) const { return cpu->f[reg]; }
	void SetGPR32Value(int reg, u32 value) { cpu->r[reg] = value; }

	u32 GetPC() const override { return cpu->pc; }
	u32 GetRA() const override { return cpu->r[MIPS_REG_RA]; }
	u32 GetFPCond() const override { return cpu->fpcond; }
	void SetPC(u32 _pc) override { cpu->pc = _pc; }

	static const char *GetCategoryName(int cat) {
		static const char *const names[3] = { "GPR", "FPU", "VFPU" };
		return names[cat];
	}
	static int GetNumCategories() { return 3; }
	static constexpr int GetNumRegsInCategory(int cat) {
		constexpr int r[3] = { 32, 32, 128 };
		return r[cat];
	}
	static std::string GetRegName(int cat, int index);

	void PrintRegValue(int cat, int index, char *out, size_t outSize) const override {
		switch (cat) {
		case 0: snprintf(out, outSize, "%08X", cpu->r[index]); break;
		case 1: snprintf(out, outSize, "%f", cpu->f[index]); break;
		case 2: snprintf(out, outSize, "N/A"); break;
		}
	}

	u32 GetHi() const override {
		return cpu->hi;
	}

	u32 GetLLBit() const override {
		return cpu->llBit;
	}

	u32 GetLo() const override {
		return cpu->lo;
	}
	
	void SetHi(u32 val) override {
		cpu->hi = val;
	}

	void SetLo(u32 val) override {
		cpu->lo = val;
	}

	u32 GetRegValue(int cat, int index) const override {
		switch (cat) {
		case 0:
			return cpu->r[index];

		case 1:
			return cpu->fi[index];

		case 2:
			return cpu->vi[voffset[index]];

		default:
			return 0;
		}
	}

	void SetRegValue(int cat, int index, u32 value) override {
		switch (cat) {
		case 0:
			if (index != 0)
				cpu->r[index] = value;
			break;

		case 1:
			cpu->fi[index] = value;
			break;

		case 2:
			cpu->vi[voffset[index]] = value;
			break;

		default:
			break;
		}
	}
};

bool initExpression(const DebugInterface *debug, const char* exp, PostfixExpression& dest);
bool parseExpression(const DebugInterface *debug, PostfixExpression& exp, u32& dest);
void DisAsm(u32 pc, char *out, size_t outSize);
