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
#include <string_view>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/MIPS/MIPS.h"
#include "Core/ConfigValues.h"

#ifdef _MSC_VER
#pragma warning (error: 4834)  // discarding return value of function with 'nodiscard' attribute
#endif

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

struct HLEFunction {
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

struct HLEModule {
	std::string_view name;
	int numFunctions;
	const HLEFunction *funcTable;
};

typedef char SyscallModuleName[32];

struct Syscall {
	SyscallModuleName moduleName;
	u32 symAddr;
	u32 nid;
};

#define PARAM(n) currentMIPS->r[MIPS_REG_A0 + n]
#define PARAM64(n) (currentMIPS->r[MIPS_REG_A0 + n] | ((u64)currentMIPS->r[MIPS_REG_A0 + n + 1] << 32))
#define PARAMF(n) currentMIPS->f[12 + n]
#define RETURN(n) currentMIPS->r[MIPS_REG_V0] = n;
#define RETURN64(n) {u64 RETURN64_tmp = n; currentMIPS->r[MIPS_REG_V0] = RETURN64_tmp & 0xFFFFFFFF; currentMIPS->r[MIPS_REG_V1] = RETURN64_tmp >> 32;}
#define RETURNF(fl) currentMIPS->f[0] = fl

struct HLEModuleMeta {
	// This is the modname (name from the PRX header). Probably, we should really blacklist on the module names of the exported symbol metadata.
	const char *modname;
	const char *importName;  // Technically a module can export functions with different module names, but doesn't seem to happen.
	DisableHLEFlags disableFlag;
};

const HLEModuleMeta *GetHLEModuleMetaByFlag(DisableHLEFlags flag);
const HLEModuleMeta *GetHLEModuleMeta(std::string_view modname);
bool ShouldHLEModule(std::string_view modname, bool *wasDisabledManually = nullptr);
bool ShouldHLEModuleByImportName(std::string_view importModuleName);

const char *GetHLEFuncName(std::string_view module, u32 nib);
const char *GetHLEFuncName(int module, int func);
const HLEModule *GetHLEModuleByName(std::string_view name);
const HLEFunction *GetHLEFunc(std::string_view module, u32 nib);
int GetHLEFuncIndexByNib(int moduleIndex, u32 nib);
int GetHLEModuleIndex(std::string_view modulename);
u32 GetNibByName(std::string_view module, std::string_view function);

void RegisterHLEModule(std::string_view name, int numFunctions, const HLEFunction *funcTable);
int GetNumRegisteredHLEModules();
const HLEModule *GetHLEModuleByIndex(int index);
DisableHLEFlags AlwaysDisableHLEFlags();

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

// Don't manually call this, it's called by the various syscall return macros.
// This should only be called once per syscall!
void hleLeave();

void hleCoreTimingForceCheck();

// Causes the syscall to not fully execute immediately, instead give the Ge a chance to
// execute display lists.
void hleSplitSyscallOverGe();

// Called after a split syscall from System.cpp
void hleFinishSyscallAfterGe();

[[nodiscard]]
inline int hleDelayResult(int result, const char *reason, int usec) {
	return hleDelayResult((u32) result, reason, usec);
}

[[nodiscard]]
inline s64 hleDelayResult(s64 result, const char *reason, int usec) {
	return hleDelayResult((u64) result, reason, usec);
}

void HLEInit();
void HLEDoState(PointerWrap &p);
void HLEShutdown();
u32 GetSyscallOp(std::string_view module, u32 nib);
bool WriteHLESyscall(std::string_view module, u32 nib, u32 address);
void CallSyscall(MIPSOpcode op);
void WriteFuncStub(u32 stubAddr, u32 symAddr);
void WriteFuncMissingStub(u32 stubAddr, u32 nid);

void HLEReturnFromMipsCall();

const HLEFunction *GetSyscallFuncPointer(MIPSOpcode op);
// For jit, takes arg: const HLEFunction *
void *GetQuickSyscallFunc(MIPSOpcode op);

void hleDoLogInternal(Log t, LogLevel level, u64 res, const char *file, int line, const char *reportTag, const char *reason, const char *formatted_reason);

template <bool leave, bool convert_code, typename T>
[[nodiscard]]
#ifdef __GNUC__
__attribute__((format(printf, 7, 8)))
#endif
NO_INLINE
T hleDoLog(Log t, LogLevel level, T res, const char *file, int line, const char *reportTag, const char *reasonFmt, ...) {
	if (!GenericLogEnabled(t, level)) {
		if (leave) {
			hleLeave();
		}
		return res;
	}

	if (convert_code && (int)res >= 0) {
		level = LogLevel::LDEBUG;
	}

	char formatted_reason[4096] = {0};
	if (reasonFmt != nullptr) {
		va_list args;
		va_start(args, reasonFmt);
		formatted_reason[0] = ':';
		formatted_reason[1] = ' ';
		vsnprintf(formatted_reason + 2, sizeof(formatted_reason) - 3, reasonFmt, args);
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
	hleDoLogInternal(t, level, fmtRes, file, line, reportTag, reasonFmt, formatted_reason);
	if (leave) {
		hleLeave();
	}
	return res;
}

template <bool leave, bool convert_code, typename T>
[[nodiscard]]
NO_INLINE
T hleDoLog(Log t, LogLevel level, T res, const char *file, int line, const char *reportTag) {
	if (((int)level > MAX_LOGLEVEL || !GenericLogEnabled(t, level)) && !reportTag) {
		if (leave) {
			hleLeave();
		}
		return res;
	}

	if (convert_code && (int)res >= 0) {
		level = LogLevel::LDEBUG;
	}

	u64 fmtRes = res;
	if (std::is_floating_point<T>::value) {
		// We reinterpret as the bits for now, so we can have a common helper.
		fmtRes = *(const u32 *)&res;
	} else if (std::is_signed<T>::value) {
		fmtRes = (s64)res;
	}
	hleDoLogInternal(t, level, fmtRes, file, line, reportTag, nullptr, "");
	if (leave) {
		hleLeave();
	}
	return res;
}

// These unwind the log stack.
template <typename T>
[[nodiscard]]
inline T hleNoLog(T t) {
	hleLeave();
	return t;
}
inline void hleNoLogVoid() {
	hleLeave();
}

// TODO: See if we can search the tables with constexpr tricks!
void hlePushFuncDesc(std::string_view module, std::string_view funcName);

// Calls a syscall from another syscall, managing the logging stack.
template<class R, class F, typename... Args>
inline R hleCallImpl(std::string_view module, std::string_view funcName, F func, Args... args) {
	hlePushFuncDesc(module, funcName);
	return func(args...);
}

// Note: ## is to eat the last comma if it's not needed (no arguments).
#define hleCall(module, retType, funcName, ...) hleCallImpl<retType>(#module, #funcName, funcName, ## __VA_ARGS__)

// This is just a quick way to force logging to be more visible for one file.
#ifdef HLE_LOG_FORCE
#define HLE_LOG_LDEBUG LNOTICE
#define HLE_LOG_LVERBOSE LDEBUG

#undef DEBUG_LOG
#define DEBUG_LOG NOTICE_LOG
#undef VERBOSE_LOG
#define VERBOSE_LOG DEBUG_LOG
#undef DEBUG_LEVEL
#define DEBUG_LEVEL NOTICE_LEVEL
#undef VERBOSE_LEVEL
#define VERBOSE_LEVEL NOTICE_LEVEL
#else
#define HLE_LOG_LDEBUG LogLevel::LDEBUG
#define HLE_LOG_LVERBOSE LogLevel::LVERBOSE
#endif

// IMPORTANT: These *must* only be used directly in HLE functions. They cannot be used by utility functions
// called by them. Use regular ERROR_LOG etc for those.

#define hleLogReturnHelper(convert, t, level, res, ...) \
	(((int)level <= MAX_LOGLEVEL) ? hleDoLog<true, convert>(t, level, (res), __FILE__, __LINE__, nullptr, ##__VA_ARGS__) : hleNoLog(res))

#define hleLogError(t, res, ...) hleLogReturnHelper(false, t, LogLevel::LERROR, res, ##__VA_ARGS__)
#define hleLogWarning(t, res, ...) hleLogReturnHelper(false, t, LogLevel::LWARNING, res, ##__VA_ARGS__)
#define hleLogDebug(t, res, ...) hleLogReturnHelper(false, t, HLE_LOG_LDEBUG, res, ##__VA_ARGS__)
#define hleLogInfo(t, res, ...) hleLogReturnHelper(false, t, LogLevel::LINFO, res, ##__VA_ARGS__)
#define hleLogVerbose(t, res, ...) hleLogReturnHelper(false, t, HLE_LOG_LVERBOSE, res, ##__VA_ARGS__)

#define hleLogDebugOrWarn(t, res, ...) hleLogReturnHelper(true, t, LogLevel::LWARNING, res, ##__VA_ARGS__)
#define hleLogDebugOrError(t, res, ...) hleLogReturnHelper(true, t, LogLevel::LERROR, res, ##__VA_ARGS__)

#define hleReportError(t, res, ...) hleDoLog<true, false>(t, LogLevel::LERROR, res, __FILE__, __LINE__, "", ##__VA_ARGS__)
#define hleReportWarning(t, res, ...) hleDoLog<true, false>(t, LogLevel::LWARNING, res, __FILE__, __LINE__, "", ##__VA_ARGS__)
