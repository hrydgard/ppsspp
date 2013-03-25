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

#include <map>

class PointerWrap;

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
  PSP_GE_SUBINTR_SIGNAL = 0,
  PSP_GE_SUBINTR_FINISH = 1,
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

enum PSPSubInterruptTriggerType {
	// Trigger all sub intr
	PSP_INTR_SUB_ALL    = -2,
	// Trigger code at the interrupt handler level
	PSP_INTR_SUB_NONE   = -1,
	// Trigger specific sub interrupt
	PSP_INTR_SUB_NUMBER = 0,
};

struct PendingInterrupt {
	PendingInterrupt(int intr_, int subintr_)
		: intr(intr_), subintr(subintr_) {}

	void DoState(PointerWrap &p);

	int intr;
	int subintr;
};

struct SubIntrHandler
{
	bool enabled;
	int intrNumber;
	int subIntrNumber;
	u32 handlerAddress;
	u32 handlerArg;
};

class IntrHandler
{
public:
	IntrHandler(int intrNumber_)
		: intrNumber(intrNumber_)
	{
	}
	virtual ~IntrHandler() {}

	virtual bool run(PendingInterrupt& pend);
	virtual void copyArgsToCPU(PendingInterrupt& pend);
	virtual void handleResult(PendingInterrupt& pend);
	void queueUp(int subintr);

	SubIntrHandler* add(int subIntrNum);
	void remove(int subIntrNum);
	bool has(int subIntrNum) const;
	void enable(int subIntrNum);
	void disable(int subIntrNum);
	SubIntrHandler *get(int subIntrNum);
	void clear();


	void DoState(PointerWrap &p);

private:
	int intrNumber;
	std::map<int, SubIntrHandler> subIntrHandlers;
};

bool __InterruptsEnabled();
bool __IsInInterrupt();
void __InterruptsInit();
void __InterruptsDoState(PointerWrap &p);
void __InterruptsDoStateLate(PointerWrap &p);
void __InterruptsShutdown();
void __TriggerInterrupt(int type, PSPInterrupt intno, int subInterrupts = -1);
bool __RunOnePendingInterrupt();
void __KernelReturnFromInterrupt();

void __RegisterIntrHandler(u32 intrNumber, IntrHandler* handler);
SubIntrHandler *__RegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 &error);
int __ReleaseSubIntrHandler(int intrNumber, int subIntrNumber);

u32 sceKernelRegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg);
int sceKernelReleaseSubIntrHandler(int intrNumber, int subIntrNumber);
u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber);

void Register_Kernel_Library();
void Register_InterruptManager();

