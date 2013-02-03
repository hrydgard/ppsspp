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

#include "../../Common/ChunkFile.h"

enum PSPInterrupt {
	PSP_GPIO_INTR      =  4,
	PSP_ATA_INTR       =  5,
	PSP_UMD_INTR       =  6,
	PSP_MSCM0_INTR     =  7,
	PSP_WLAN_INTR      =  8,
	PSP_AUDIO_INTR     = 10,
	PSP_I2C_INTR       = 12,
	PSP_SIRS_INTR      = 14,
	PSP_SYSTIMER0_INTR = 15,
	PSP_SYSTIMER1_INTR = 16,
	PSP_SYSTIMER2_INTR = 17,
	PSP_SYSTIMER3_INTR = 18,
	PSP_THREAD0_INTR   = 19,
	PSP_NAND_INTR      = 20,
	PSP_DMACPLUS_INTR  = 21,
	PSP_DMA0_INTR      = 22,
	PSP_DMA1_INTR      = 23,
	PSP_MEMLMD_INTR    = 24,
	PSP_GE_INTR        = 25,
	PSP_VBLANK_INTR    = 30,
	PSP_MECODEC_INTR   = 31,
	PSP_HPREMOTE_INTR  = 36,
	PSP_MSCM1_INTR     = 60,
	PSP_MSCM2_INTR     = 61,
	PSP_THREAD1_INTR   = 65,
	PSP_INTERRUPT_INTR = 66,
	PSP_NUMBER_INTERRUPTS = 67
};


// These are invented for PPSSPP:
enum PSPGeSubInterrupts {
  PSP_GE_SUBINTR_FINISH = 14,
  PSP_GE_SUBINTR_SIGNAL = 15
};

enum PSPInterruptTriggerType {
	// Trigger immediately, for CoreTiming events.
	PSP_INTR_IMMEDIATE = 0x0,
	// Trigger after the HLE syscall finishes.
	PSP_INTR_HLE = 0x1,
	// Only trigger (as above) if interrupts are not suspended.
	PSP_INTR_ONLY_IF_ENABLED = 0x2,
	// Always reschedule, even if there's no handler registered.
	// TODO: Maybe this should just always do this?  Not sure.
	PSP_INTR_ALWAYS_RESCHED = 0x4,
};

struct PendingInterrupt {
	PendingInterrupt(int intr_, int subintr_)
		: intr(intr_), subintr(subintr_), hasArg(false) {}
	PendingInterrupt(int intr_, int subintr_, int arg_)
		: intr(intr_), subintr(subintr_), hasArg(true), arg(arg_) {}

	u32 intr;
	u32 subintr;
	bool hasArg;
	int arg;
};

class SubIntrHandler
{
public:
	SubIntrHandler() {}
	virtual ~SubIntrHandler() {}
	virtual void queueUp();
	virtual void queueUpWithArg(int arg);
	virtual void copyArgsToCPU(const PendingInterrupt &pend);
	virtual void handleResult(int result) {}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(enabled);
		p.Do(intrNumber);
		p.Do(number);
		p.Do(handlerAddress);
		p.Do(handlerArg);
		p.DoMarker("SubIntrHandler");
	}

	bool enabled;
	int intrNumber;
	int number;
	u32 handlerAddress;
	u32 handlerArg;
};

typedef SubIntrHandler *(*SubIntrCreator)();

bool __IsInInterrupt();
void __InterruptsInit();
void __InterruptsDoState(PointerWrap &p);
void __InterruptsDoStateLate(PointerWrap &p);
void __InterruptsShutdown();
void __TriggerInterrupt(int type, PSPInterrupt intno, int subInterrupts = -1);
void __TriggerInterruptWithArg(int type, PSPInterrupt intno, int subintr, int arg);  // For GE "callbacks"
bool __RunOnePendingInterrupt();
void __KernelReturnFromInterrupt();

void __RegisterSubIntrCreator(u32 intrNumber, SubIntrCreator creator);
SubIntrHandler *__RegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 &error);
u32 __ReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber);

u32 sceKernelRegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg);
u32 sceKernelReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber);
u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber);

void Register_Kernel_Library();
void Register_InterruptManager();

