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

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

class PointerWrap;
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

typedef char SyscallModuleName[32];

struct Syscall
{
	SyscallModuleName moduleName;
	u32 symAddr;
	u32 nid;
};

#define PARAM(n) currentMIPS->r[MIPS_REG_A0 + n]
#define PARAM64(n) (currentMIPS->r[MIPS_REG_A0 + n] | ((u64)currentMIPS->r[MIPS_REG_A0 + n + 1] << 32))
#define PARAMF(n) currentMIPS->f[12 + n]
#define RETURN(n) currentMIPS->r[MIPS_REG_V0] = n
#define RETURN64(n) {u64 RETURN64_tmp = n; currentMIPS->r[MIPS_REG_V0] = RETURN64_tmp & 0xFFFFFFFF; currentMIPS->r[MIPS_REG_V1] = RETURN64_tmp >> 32;}
#define RETURNF(fl) currentMIPS->f[0] = fl

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

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
// Don't set temp regs to 0xDEADBEEF.
void hleSkipDeadbeef();
// Set time spent in debugger (for more useful debug stats while debugging.)
void hleSetSteppingTime(double t);

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
bool FuncImportIsSyscall(const char *module, u32 nib);
bool WriteSyscall(const char *module, u32 nib, u32 address);
void CallSyscall(MIPSOpcode op);
void WriteFuncStub(u32 stubAddr, u32 symAddr);
void WriteFuncMissingStub(u32 stubAddr, u32 nid);

const HLEFunction *GetSyscallInfo(MIPSOpcode op);
// For jit, takes arg: const HLEFunction *
void *GetQuickSyscallFunc(MIPSOpcode op);

