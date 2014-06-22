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

#include "util/random/rng.h"
#include "Common/CommonTypes.h"
#include "Core/CoreParameter.h"
#include "Core/Opcode.h"

class PointerWrap;

typedef Memory::Opcode MIPSOpcode;

enum MIPSGPReg
{
	MIPS_REG_ZERO=0,
	MIPS_REG_COMPILER_SCRATCH=1,

	MIPS_REG_V0=2,
	MIPS_REG_V1=3,

	MIPS_REG_A0=4,
	MIPS_REG_A1=5,
	MIPS_REG_A2=6,
	MIPS_REG_A3=7,
	MIPS_REG_A4=8,	// Seems to be N32 register calling convention - there are 8 args instead of 4.
	MIPS_REG_A5=9,

	MIPS_REG_T4=12,
	MIPS_REG_T5=13,
	MIPS_REG_T6=14,
	MIPS_REG_T7=15,

	MIPS_REG_S0=16,
	MIPS_REG_S1=17,
	MIPS_REG_S2=18,
	MIPS_REG_S3=19,
	MIPS_REG_S4=20,
	MIPS_REG_S5=21,
	MIPS_REG_S6=22,
	MIPS_REG_S7=23,
	MIPS_REG_T8=24,
	MIPS_REG_T9=25,
	MIPS_REG_K0=26,
	MIPS_REG_K1=27,
	MIPS_REG_GP=28,
	MIPS_REG_SP=29,
	MIPS_REG_FP=30,
	MIPS_REG_RA=31,

	MIPS_REG_INVALID=-1,

	// Not real regs, just for convenience/jit mapping.
	MIPS_REG_HI = 32,
	MIPS_REG_LO = 33,
	MIPS_REG_FPCOND = 34,
	MIPS_REG_VFPUCC = 35,
};

enum
{
	VFPU_CTRL_SPREFIX,
	VFPU_CTRL_TPREFIX,
	VFPU_CTRL_DPREFIX,
	VFPU_CTRL_CC,
	VFPU_CTRL_INF4,
	VFPU_CTRL_RSV5,
	VFPU_CTRL_RSV6,
	VFPU_CTRL_REV,
	VFPU_CTRL_RCX0,
	VFPU_CTRL_RCX1,
	VFPU_CTRL_RCX2,
	VFPU_CTRL_RCX3,
	VFPU_CTRL_RCX4,
	VFPU_CTRL_RCX5,
	VFPU_CTRL_RCX6,
	VFPU_CTRL_RCX7,

	VFPU_CTRL_MAX,
	//unknown....
};

enum VCondition
{
	VC_FL,
	VC_EQ,
	VC_LT,
	VC_LE,
	VC_TR,
	VC_NE,
	VC_GE,
	VC_GT,
	VC_EZ,
	VC_EN,
	VC_EI,
	VC_ES,
	VC_NZ,
	VC_NN,
	VC_NI,
	VC_NS
};

// In memory, we order the VFPU registers differently. 
// Games use columns a whole lot more than rows, and it would thus be good if columns
// were contiguous in memory. Also, matrices aren't but should be.
extern u8 voffset[128];
extern u8 fromvoffset[128];

class MIPSState
{
public:
	MIPSState();
	~MIPSState();

	void Init();
	void Shutdown();
	void Reset();
	void UpdateCore(CPUCore desired);

	void DoState(PointerWrap &p);

	// MUST start with r and be followed by f!
	u32 r[32];
	union {
		float f[32];
		u32 fi[32];
		int fs[32];
	};
	union {
		float v[128];
		u32 vi[128];
	};
	// Temps don't get flushed so we don't reserve space for them.
	// If vfpuCtrl (prefixes) get mysterious values, check the VFPU regcache code.
	u32 vfpuCtrl[16];

	union {
		struct {
			u32 pc;

			u32 hi;
			u32 lo;

			u32 fcr31; //fpu control register
			u32 fpcond;  // cache the cond flag of fcr31  (& 1 << 23)
		};
		u32 other[6];
	};

	u32 nextPC;
	int downcount;  // This really doesn't belong here, it belongs in CoreTiming. But you gotta do what you gotta do, this needs to be reachable in the ARM JIT.

	bool inDelaySlot;
	int llBit;  // ll/sc
	u32 temp;  // can be used to save temporaries during calculations when we need more than R0 and R1

	GMRng rng;	// VFPU hardware random number generator. Probably not the right type.

	// Debug stuff
	u32 debugCount;	// can be used to count basic blocks before crashes, etc.

	static const u32 FCR0_VALUE = 0x00003351;

	void WriteFCR(int reg, int value);
	u32 ReadFCR(int reg);

	u8 VfpuWriteMask() const {
		return (vfpuCtrl[VFPU_CTRL_DPREFIX] >> 8) & 0xF;
	}
	bool VfpuWriteMask(int i) const {
		return (vfpuCtrl[VFPU_CTRL_DPREFIX] >> (8 + i)) & 1;
	}

	void SingleStep();
	int RunLoopUntil(u64 globalTicks);
	// To clear jit caches, etc.
	void InvalidateICache(u32 address, int length = 4);
};


class MIPSDebugInterface;

//The one we are compiling or running currently
extern MIPSState *currentMIPS;
extern MIPSDebugInterface *currentDebugMIPS;
extern MIPSState mipsr4k;

extern const float cst_constants[32];
