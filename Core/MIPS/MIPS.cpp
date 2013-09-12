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

#include <cmath>
#include <limits>

#include "math/math_util.h"
#include "Common.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/HLE/sceDisplay.h"

#if defined(ARM)
#include "ARM/ArmJit.h"
#elif defined(PPC)
#include "PPC/PpcJit.h"
#else
#include "x86/Jit.h"
#endif
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"

MIPSState mipsr4k;
MIPSState *currentMIPS = &mipsr4k;
MIPSDebugInterface debugr4k(&mipsr4k);
MIPSDebugInterface *currentDebugMIPS = &debugr4k;


#ifndef M_LOG2E
#define M_E        2.71828182845904523536f
#define M_LOG2E    1.44269504088896340736f
#define M_LOG10E   0.434294481903251827651f
#define M_LN2      0.693147180559945309417f
#define M_LN10     2.30258509299404568402f
#undef M_PI
#define M_PI       3.14159265358979323846f
#define M_PI_2     1.57079632679489661923f
#define M_PI_4     0.785398163397448309616f
#define M_1_PI     0.318309886183790671538f
#define M_2_PI     0.636619772367581343076f
#define M_2_SQRTPI 1.12837916709551257390f
#define M_SQRT2    1.41421356237309504880f
#define M_SQRT1_2  0.707106781186547524401f
#endif

const float cst_constants[32] = {
	0,
	std::numeric_limits<float>::max(),  // all these are verified on real PSP
	sqrtf(2.0f),
	sqrtf(0.5f),
	2.0f/sqrtf((float)M_PI),
	2.0f/(float)M_PI,
	1.0f/(float)M_PI,
	(float)M_PI/4,
	(float)M_PI/2,
	(float)M_PI,
	(float)M_E,
	(float)M_LOG2E,
	(float)M_LOG10E,
	(float)M_LN2,
	(float)M_LN10,
	2*(float)M_PI,
	(float)M_PI/6,
	log10f(2.0f),
	logf(10.0f)/logf(2.0f),
	sqrtf(3.0f)/2.0f,
};


MIPSState::MIPSState()
{
	MIPSComp::jit = 0;
}

MIPSState::~MIPSState()
{
	if (MIPSComp::jit)
	{
		delete MIPSComp::jit;
		MIPSComp::jit = 0;
	}
}

void MIPSState::Reset()
{
	if (MIPSComp::jit)
	{
		delete MIPSComp::jit;
		MIPSComp::jit = 0;
	}
		
	if (PSP_CoreParameter().cpuCore == CPU_JIT)
		MIPSComp::jit = new MIPSComp::Jit(this);

	memset(r, 0, sizeof(r));
	memset(f, 0, sizeof(f));
	memset(v, 0, sizeof(v));
	memset(vfpuCtrl, 0, sizeof(vfpuCtrl));

	vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
	vfpuCtrl[VFPU_CTRL_CC] = 0x3f;
	vfpuCtrl[VFPU_CTRL_INF4] = 0;
	vfpuCtrl[VFPU_CTRL_RCX0] = 0x3f800001;
	vfpuCtrl[VFPU_CTRL_RCX1] = 0x3f800002;
	vfpuCtrl[VFPU_CTRL_RCX2] = 0x3f800004;
	vfpuCtrl[VFPU_CTRL_RCX3] = 0x3f800008;
	vfpuCtrl[VFPU_CTRL_RCX4] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX5] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX6] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX7] = 0x3f800000;

	pc = 0;
	hi = 0;
	lo = 0;
	fpcond = 0;
	fcr0 = 0;
	fcr31 = 0;
	debugCount = 0;
	currentMIPS = this;
	inDelaySlot = false;
	llBit = 0;
	nextPC = 0;
	downcount = 0;
	// Initialize the VFPU random number generator with .. something?
	rng.Init(0x1337);
}

void MIPSState::DoState(PointerWrap &p) {
	// Reset the jit if we're loading.
	if (p.mode == p.MODE_READ)
		Reset();
	if (MIPSComp::jit)
		MIPSComp::jit->DoState(p);
	else
		MIPSComp::Jit::DoDummyState(p);

	p.DoArray(r, sizeof(r) / sizeof(r[0]));
	p.DoArray(f, sizeof(f) / sizeof(f[0]));
	p.DoArray(v, sizeof(v) / sizeof(v[0]));
	p.DoArray(vfpuCtrl, sizeof(vfpuCtrl) / sizeof(vfpuCtrl[0]));
	p.Do(pc);
	p.Do(nextPC);
	p.Do(downcount);
	p.Do(hi);
	p.Do(lo);
	p.Do(fpcond);
	p.Do(fcr0);
	p.Do(fcr31);
	p.Do(rng.m_w);
	p.Do(rng.m_z);
	p.Do(inDelaySlot);
	p.Do(llBit);
	p.Do(debugCount);
	p.DoMarker("MIPSState");
}

void MIPSState::SingleStep()
{
	int cycles = MIPS_SingleStep();
	currentMIPS->downcount -= cycles;
	CoreTiming::Advance();
}

// returns 1 if reached ticks limit
int MIPSState::RunLoopUntil(u64 globalTicks)
{
	switch (PSP_CoreParameter().cpuCore)
	{
	case CPU_JIT:
		MIPSComp::jit->RunLoopUntil(globalTicks);
		break;

	case CPU_INTERPRETER:
		return MIPSInterpret_RunUntil(globalTicks);
	}
	return 1;
}

void MIPSState::WriteFCR(int reg, int value)
{
	if (reg == 31)
	{
		fcr31 = value;
		fpcond = (value >> 23) & 1;
	}
	else
	{
		WARN_LOG_REPORT(CPU, "WriteFCR: Unexpected reg %d (value %08x)", reg, value);
		// MessageBox(0, "Invalid FCR","...",0);
	}
	DEBUG_LOG(CPU, "FCR%i written to, value %08x", reg, value);
}

u32 MIPSState::ReadFCR(int reg)
{
	DEBUG_LOG(CPU,"FCR%i read",reg);
	if (reg == 31)
	{
		fcr31 = (fcr31 & ~(1<<23)) | ((fpcond & 1)<<23);
		return fcr31;
	}
	else if (reg == 0)
	{
		return fcr0;
	}
	else
	{
		WARN_LOG_REPORT(CPU, "ReadFCR: Unexpected reg %d", reg);
		// MessageBox(0, "Invalid FCR","...",0);
	}
	return 0;
}

void MIPSState::InvalidateICache(u32 address, int length)
{
	// Only really applies to jit.
	if (MIPSComp::jit)
		MIPSComp::jit->ClearCacheAt(address, length);
}


// Interrupts should be served directly on the running thread.
void MIPSState::Irq()
{
//	if (IRQEnabled())
	{
	}
}


void MIPSState::SWI()
{
}

const char *MIPSState::DisasmAt(u32 compilerPC) {
	static char temp[256];
	MIPSDisAsm(Memory::Read_Instruction(compilerPC), 0, temp);
	return temp;
}