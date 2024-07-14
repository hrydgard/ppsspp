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

#include <cstdio>
#include <cstdarg>
#include <type_traits>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/MIPS/MIPS.h"

class PointerWrap;
class PSPAction;
typedef void (* HLEFunc)();

enum {
	// The low 8 bits are a value, indicating special jit handling.
	// Currently there are none.

	// The remaining 24 bits are flags.
	// Don't allow the call within an interrupt.  Not yet implemented.
	HLE_NOT_IN_INTERRUPT = 1 << 8,
	// Don't allow the call if dispatch or interrupts are disabled.
	HLE_NOT_DISPATCH_SUSPENDED = 1 << 9,
	// Indicates the call should write zeros to the stack (stackBytesToClear in the table.)
	HLE_CLEAR_STACK_BYTES = 1 << 10,
	// Indicates that this call operates in kernel mode.
	HLE_KERNEL_SYSCALL = 1 << 11,
};

struct HLEFunction
{
	// This is the id, or nid, of the function (which is how it's linked.)
	// Generally, the truncated least significant 32 bits of a SHA-1 hash.
	u32 ID;
	// A pointer to the C++ handler; see FunctionWrappers.h for helpers.
	HLEFunc func;
	// Name of the function.  Often the same as func, this should match the PSP func's name and hash.
	const char *name;
	// The return type, see argmask for the possible values.
	// An additional value is possible - 'v', which means void (no return type.)
	char retmask;
	// The argument types, in order.  Use the following characters:
	//   - x: for u32 (shown as hex in log)
	//   - i: for int/s32
	//   - f: for float
	//   - X: uppercase, for u64
	//   - I: uppercase, for s64
	//   - F: uppercase, for double
	//   - s: a string pointer (const char *, utf-8)
	//   - p: a pointer (e.g. u32 *, shown with value in log)
	const char *argmask;
	// Flags (see above, e.g. HLE_NOT_IN_INTERRUPT.)
	u32 flags;
	// See HLE_CLEAR_STACK_BYTES.
	u32 stackBytesToClear;
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

const char *GetFuncName(const char *module, u32 nib);
const char *GetFuncName(int module, int func);
const HLEFunction *GetFunc(const char *module, u32 nib);
int GetFuncIndex(int moduleIndex, u32 nib);
int GetModuleIndex(const char *modulename);

void RegisterModule(const char *name, int numFunctions, const HLEFunction *funcTable);

// Run the current thread's callbacks after the syscall finishes.
void hleCheckCurrentCallbacks();
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
// Set time spent in realtime sync.
void hleSetFlipTime(double t);
// Check if the current syscall context is kernel.
bool hleIsKernelMode();
// Enqueue a MIPS function to be called after this HLE call finishes.
void hleEnqueueCall(u32 func, int argc, const u32 *argv, PSPAction *afterAction = nullptr);

// Delays the result for usec microseconds, allowing other threads to run during this time.
u32 hleDelayResult(u32 result, const char *reason, int usec);
u64 hleDelayResult(u64 result, const char *reason, int usec);
void hleEatCycles(int cycles);
void hleEatMicro(int usec);

inline int hleDelayResult(int result, const char *reason, int usec) {
	return hleDelayResult((u32) result, reason, usec);
}

inline s64 hleDelayResult(s64 result, const char *reason, int usec) {
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

void HLEReturnFromMipsCall();

const HLEFunction *GetSyscallFuncPointer(MIPSOpcode op);
// For jit, takes arg: const HLEFunction *
void *GetQuickSyscallFunc(MIPSOpcode op);

void hleDoLogInternal(Log t, LogLevel level, u64 res, const char *file, int line, const char *reportTag, char retmask, const char *reason, const char *formatted_reason);

template <typename T>
T hleDoLog(Log t, LogLevel level, T res, const char *file, int line, const char *reportTag, char retmask, const char *reason, ...) {
	if ((int)level > MAX_LOGLEVEL || !GenericLogEnabled(level, t)) {
		return res;
	}

	char formatted_reason[4096] = {0};
	if (reason != nullptr) {
		va_list args;
		va_start(args, reason);
		formatted_reason[0] = ':';
		formatted_reason[1] = ' ';
		vsnprintf(formatted_reason + 2, sizeof(formatted_reason) - 3, reason, args);
		formatted_reason[sizeof(formatted_reason) - 1] = '\0';
		va_end(args);
	}

	u64 fmtRes = res;
	if (std::is_floating_point<T>::value) {
		// We reinterpret as the bits for now, so we can have a common helper.
		fmtRes = *(const u32 *)&res;
	} else if (std::is_signed<T>::value) {
		fmtRes = (s64)res;
	}
	hleDoLogInternal(t, level, fmtRes, file, line, reportTag, retmask, reason, formatted_reason);
	return res;
}

template <typename T>
T hleDoLog(Log t, LogLevel level, T res, const char *file, int line, const char *reportTag, char retmask) {
	if (((int)level > MAX_LOGLEVEL || !GenericLogEnabled(level, t)) && !reportTag) {
		return res;
	}

	u64 fmtRes = res;
	if (std::is_floating_point<T>::value) {
		// We reinterpret as the bits for now, so we can have a common helper.
		fmtRes = *(const u32 *)&res;
	} else if (std::is_signed<T>::value) {
		fmtRes = (s64)res;
	}
	hleDoLogInternal(t, level, fmtRes, file, line, reportTag, retmask, nullptr, "");
	return res;
}

// This is just a quick way to force logging to be more visible for one file.
#ifdef HLE_LOG_FORCE
#define HLE_LOG_LDEBUG LNOTICE
#define HLE_LOG_LVERBOSE LDEBUG

#undef DEBUG_LOG
#define DEBUG_LOG NOTICE_LOG
#undef VERBOSE_LOG
#define VERBOSE_LOG DEBUG_LOG
#else
#define HLE_LOG_LDEBUG LDEBUG
#define HLE_LOG_LVERBOSE LVERBOSE
#endif

#define hleLogHelper(t, level, res, retmask, ...) hleDoLog(t, LogLevel::level, res, __FILE__, __LINE__, nullptr, retmask, ##__VA_ARGS__)
#define hleLogError(t, res, ...) hleLogHelper(t, LERROR, res, 'x', ##__VA_ARGS__)
#define hleLogWarning(t, res, ...) hleLogHelper(t, LWARNING, res, 'x', ##__VA_ARGS__)
#define hleLogDebug(t, res, ...) hleLogHelper(t, HLE_LOG_LDEBUG, res, 'x', ##__VA_ARGS__)
#define hleLogVerbose(t, res, ...) hleLogHelper(t, HLE_LOG_LVERBOSE, res, 'x', ##__VA_ARGS__)
#define hleLogSuccessX(t, res, ...) hleLogHelper(t, HLE_LOG_LDEBUG, res, 'x', ##__VA_ARGS__)
#define hleLogSuccessI(t, res, ...) hleLogHelper(t, HLE_LOG_LDEBUG, res, 'i', ##__VA_ARGS__)
#define hleLogSuccessInfoX(t, res, ...) hleLogHelper(t, LINFO, res, 'x', ##__VA_ARGS__)
#define hleLogSuccessInfoI(t, res, ...) hleLogHelper(t, LINFO, res, 'i', ##__VA_ARGS__)
#define hleLogSuccessVerboseX(t, res, ...) hleLogHelper(t, HLE_LOG_LVERBOSE, res, 'x', ##__VA_ARGS__)
#define hleLogSuccessVerboseI(t, res, ...) hleLogHelper(t, HLE_LOG_LVERBOSE, res, 'i', ##__VA_ARGS__)

#define hleReportError(t, res, ...) hleDoLog(t, LogLevel::LERROR, res, __FILE__, __LINE__, "", 'x', ##__VA_ARGS__)
#define hleReportWarning(t, res, ...) hleDoLog(t, LogLevel::LWARNING, res, __FILE__, __LINE__, "", 'x', ##__VA_ARGS__)
#define hleReportDebug(t, res, ...) hleDoLog(t, LogLevel::HLE_LOG_LDEBUG, res, __FILE__, __LINE__, "", 'x', ##__VA_ARGS__)
