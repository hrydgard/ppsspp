// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common.h"
#include "MIPS.h"
#include "MIPSTables.h"
#include "MIPSDebugInterface.h"
#include "MIPSVFPUUtils.h"
#include "../System.h"
#include "../Debugger/Breakpoints.h"

#ifdef ANDROID
#include "ARM/JitCache.h"
#include "ARM/Jit.h"
#else
#include "x86/JitCache.h"
#include "x86/Jit.h"
#endif
#include "JitCommon/JitCommon.h"
#include "../../Core/CoreTiming.h"

MIPSState mipsr4k;
MIPSState *currentMIPS = &mipsr4k;
MIPSDebugInterface debugr4k(&mipsr4k);
MIPSDebugInterface *currentDebugMIPS = &debugr4k;

MIPSState::MIPSState()
{
	cpuType = CPUTYPE_ALLEGREX;
}

MIPSState::~MIPSState()
{
	delete MIPSComp::jit;
	MIPSComp::jit = 0;
}

void MIPSState::Reset()
{
	if (!MIPSComp::jit && PSP_CoreParameter().cpuCore == CPU_JIT)
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

	bool b[4] = {false, false, false, false};
	SetWriteMask(b);

	pc = 0;
	hi = 0;
	lo = 0;
	fpcond = 0;
	fcr0 = 0;
	fcr31 = 0;
	debugCount = 0;
	exceptions = 0;
	currentMIPS = this;
	inDelaySlot = false;
	nextPC = 0;
	// Initialize the VFPU random number generator with .. something?
	rng.Init(0x1337);
}

void MIPSState::SetWriteMask(const bool wm[4])
{
	for (int i = 0; i < 4; i++)
		vfpuWriteMask[i] = wm[i];
}

void MIPSState::SingleStep()
{
	int cycles = MIPS_SingleStep();
	CoreTiming::downcount -= cycles;
	CoreTiming::Advance();
}

void MIPSState::RunLoopUntil(u64 globalTicks)
{
	// Don't subvert this by setting useJIT to true - other places also check the coreparameter
	bool useJIT = PSP_CoreParameter().cpuCore == CPU_JIT;
#ifdef ANDROID
	useJIT = false;
#endif

	if (useJIT)
	{
		MIPSComp::jit->RunLoopUntil(globalTicks);
	}
	else
	{
		// INFO_LOG(CPU, "Entering run loop for %i ticks, pc=%08x", (int)globalTicks, mipsr4k.pc);
		while (coreState == CORE_RUNNING)
		{
			// NEVER stop in a delay slot!
#ifdef _DEBUG
			while (CoreTiming::downcount >= 0 && coreState == CORE_RUNNING)
#else
			while (CoreTiming::downcount >= 0)
#endif
			{
				// int cycles = 0;
				{
					again:
					u32 op = Memory::ReadUnchecked_U32(mipsr4k.pc);
					//u32 op = Memory::Read_Opcode_JIT(mipsr4k.pc);
					/*
					// Choke on VFPU
					u32 info = MIPSGetInfo(op);
					if (info & IS_VFPU)
					{
						if (!Core_IsStepping() && !GetAsyncKeyState(VK_LSHIFT))
						{
							Core_EnableStepping(true);
							return;
						}
					}*/

			//2: check for breakpoint (VERY SLOW)
#if defined(_DEBUG)
					if (CBreakPoints::IsAddressBreakPoint(pc))
					{
						Core_EnableStepping(true);
						if (CBreakPoints::IsTempBreakPoint(pc))
							CBreakPoints::RemoveBreakPoint(pc);
						break;
					}
#endif
					// u32 lastpc = pc;
					if (inDelaySlot)
					{
						MIPSInterpret(op);
						if (inDelaySlot)
						{
							pc = nextPC;
							inDelaySlot = false;
						}
					}
					else
					{
						MIPSInterpret(op);
					}

					if (!Memory::IsValidAddress(pc))
					{
						pc = pc;
					}
					if (r[MIPS_REG_RA] != 0 && !Memory::IsValidAddress(r[MIPS_REG_RA]))
					{
					//	pc = pc;
					}
					if (inDelaySlot)
						goto again;
				}

				CoreTiming::downcount -= 1;
				if (CoreTiming::GetTicks() > globalTicks)
				{
					DEBUG_LOG(CPU, "Hit the max ticks, bailing : %llu, %llu", globalTicks, CoreTiming::GetTicks());
					break;
				}

			}

			CoreTiming::Advance();
			// if (exceptions) checkExceptions();
			if (CoreTiming::GetTicks() > globalTicks)
			{
				DEBUG_LOG(CPU, "Hit the max ticks, bailing : %llu, %llu", globalTicks, CoreTiming::GetTicks());
				break;
			}
		}
	}
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
		// MessageBox(0, "Invalid FCR","...",0);
	}
	return 0;
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
