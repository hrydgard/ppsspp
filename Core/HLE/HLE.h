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

#include "../Globals.h"
#include "../MIPS/MIPS.h"

typedef void (* HLEFunc)();

enum {
	// The low 8 bits are a value, indicating special jit handling.
	// Currently there are none.

	// The remaining 24 bits are flags.
	// Don't allow the call within an interrupt.  Not yet implemented.
	HLE_NOT_IN_INTERRUPT = 1 << 8,
	// Don't allow the call if dispatch or interrupts are disabled.
	HLE_NOT_DISPATCH_SUSPENDED = 1 << 9,
};

struct HLEFunction
{
	u32 ID;
	HLEFunc func;
	const char *name;
	u32 flags;
};

struct HLEModule
{
	const char *name;
	int numFunctions;
	const HLEFunction *funcTable;
};

struct Syscall
{
	char moduleName[32];
	u32 symAddr;
	u32 nid;
};

#define PARAM(n) currentMIPS->r[4+n]
#define PARAMF(n) currentMIPS->f[12+n]
#define RETURN(n) currentMIPS->r[2]=n
#define RETURNF(fl) currentMIPS->f[0]=fl

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#include "FunctionWrappers.h"


#define P_INT(n, name) const int name = currentMIPS->r[4+n];

const char *GetFuncName(const char *module, u32 nib);
const char *GetFuncName(int module, int func);
const HLEFunction *GetFunc(const char *module, u32 nib);
int GetFuncIndex(int moduleIndex, u32 nib);
int GetModuleIndex(const char *modulename);

void RegisterModule(const char *name, int numFunctions, const HLEFunction *funcTable);

// Run the current thread's callbacks after the syscall finishes.
void hleCheckCurrentCallbacks();
// Check and potentially run all thread's callbacks after the syscall finishes.
void hleCheckAllCallbacks();
// Reschedule after the syscall finishes.
void hleReSchedule(const char *reason);
// Reschedule and go into a callback processing state after the syscall finishes.
void hleReSchedule(bool callbacks, const char *reason);
// Run interrupts after the syscall finishes.
void hleRunInterrupts();
// Pause emulation after the syscall finishes.
void hleDebugBreak();

// Delays the result for usec microseconds, allowing other threads to run during this time.
u32 hleDelayResult(u32 result, const char *reason, int usec);
u64 hleDelayResult(u64 result, const char *reason, int usec);
void hleEatCycles(int cycles);
void hleEatMicro(int usec);

inline int hleDelayResult(int result, const char *reason, int usec)
{
	return hleDelayResult((u32) result, reason, usec);
}

inline s64 hleDelayResult(s64 result, const char *reason, int usec)
{
	return hleDelayResult((u64) result, reason, usec);
}

void HLEInit();
void HLEDoState(PointerWrap &p);
void HLEShutdown();
u32 GetNibByName(const char *module, const char *function);
u32 GetSyscallOp(const char *module, u32 nib);
void WriteSyscall(const char *module, u32 nib, u32 address);
void CallSyscall(u32 op);
void ResolveSyscall(const char *moduleName, u32 nib, u32 address);

