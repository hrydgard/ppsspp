// Copyright (c) 2018- PPSSPP Project.

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

#include <cstdio>
#include "Core/HLE/PSPThreadContext.h"
#include "Core/MIPS/MIPSDebugInterface.h"

struct PSPThreadContext;

class KernelThreadDebugInterface : public DebugInterface {
public:
	KernelThreadDebugInterface(PSPThreadContext *t) : ctx(*t) {}

	u32 GetGPR32Value(int reg) const override { return ctx.r[reg]; }
	u32 GetHi() const override { return ctx.hi; }
	u32 GetLo() const override { return ctx.lo; }
	u32 GetPC() const override { return ctx.pc; }
	u32 GetRA() const override { return ctx.r[MIPS_REG_RA]; }
	u32 GetLLBit() const override { return 0; }
	u32 GetFPCond() const override { return ctx.fpcond; }
	void SetPC(u32 _pc) override { ctx.pc = _pc; }
	void SetHi(u32 val) override { ctx.hi = val; }
	void SetLo(u32 val) override { ctx.lo = val; }

	void PrintRegValue(int cat, int index, char *out, size_t outSize) const override {
		switch (cat) {
		case 0: snprintf(out, outSize, "%08X", ctx.r[index]); break;
		case 1: snprintf(out, outSize, "%f", ctx.f[index]); break;
		case 2: snprintf(out, outSize, "N/A"); break;
		}
	}

	u32 GetRegValue(int cat, int index) const override {
		switch (cat) {
		case 0: return ctx.r[index];
		case 1: return ctx.fi[index];
		case 2: return ctx.vi[voffset[index]];
		default: return 0;
		}
	}

	void SetRegValue(int cat, int index, u32 value) override {
		switch (cat) {
		case 0:
			if (index != 0)
				ctx.r[index] = value;
			break;

		case 1:
			ctx.fi[index] = value;
			break;

		case 2:
			ctx.vi[voffset[index]] = value;
			break;

		default:
			break;
		}
	}

protected:
	PSPThreadContext &ctx;
};
