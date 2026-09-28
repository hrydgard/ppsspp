// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// The corresponding file is called MemTools in the Dolphin project.

#include "ppsspp_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/MachineContext.h"
#include "Common/ExceptionHandlerSetup.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#include "Common/CommonWindows.h"
#endif

static BadAccessHandler g_badAccessHandler;
static void *altStack = nullptr;

void SetupCRT(bool suppressDialogs) {
#if defined(_MSC_VER)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	if (suppressDialogs) {
		// 1. Redirect CRT assertions/errors/warnings to stderr.
		const _HFILE reportTarget = _CRTDBG_FILE_STDERR;

		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, reportTarget);

		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, reportTarget);

		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, reportTarget);

		// 2. Suppress the abort() message box & crash reporting dialogs.
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#if !PPSSPP_PLATFORM(UWP)
		// 3. Suppress Windows OS-level "Program has stopped working" modal dialogs.
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
	}
#endif
}

#ifdef MACHINE_CONTEXT_SUPPORTED

// We cannot handle exceptions in UWP builds. Bleh.
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static PVOID g_vectoredExceptionHandle;
static bool g_symInitialized = false;
static bool g_logCrashStackTrace = false;

// Logs a best-effort stack trace when we're about to let a genuinely unhandled access
// violation crash the process - e.g. a bad host pointer (not a guest PSP memory access)
// passed to a CRT function like strlen(). Only meant for diagnostics, so failures here are
// non-fatal; we just lose the extra info.
static void LogCrashStackTrace() {
	void *stack[32]{};
	USHORT captured = CaptureStackBackTrace(0, (ULONG)ARRAY_SIZE(stack), stack, nullptr);

	ERROR_LOG(Log::System, "Unhandled access violation - stack trace (%d frames):", (int)captured);

	HANDLE process = GetCurrentProcess();
	char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
	SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbolBuffer;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYM_NAME;

	for (USHORT i = 0; i < captured; i++) {
		DWORD64 address = (DWORD64)(uintptr_t)stack[i];
		std::string line = StringFromFormat("  #%d %016llx", (int)i, (unsigned long long)address);

		DWORD64 displacement = 0;
		if (SymFromAddr(process, address, &displacement, symbol)) {
			line += StringFromFormat(" %s+0x%llx", symbol->Name, (unsigned long long)displacement);
		}

		DWORD lineDisplacement = 0;
		IMAGEHLP_LINE64 lineInfo{};
		lineInfo.SizeOfStruct = sizeof(lineInfo);
		if (SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo)) {
			line += StringFromFormat(" (%s:%d)", lineInfo.FileName, (int)lineInfo.LineNumber);
		}

		ERROR_LOG(Log::System, "%s", line.c_str());
	}
}

static LONG NTAPI GlobalExceptionHandler(PEXCEPTION_POINTERS pPtrs) {
	switch (pPtrs->ExceptionRecord->ExceptionCode) {
	case EXCEPTION_ACCESS_VIOLATION:
	{
		int accessType = (int)pPtrs->ExceptionRecord->ExceptionInformation[0];
		if (accessType == 8) {  // Rule out DEP
			return (DWORD)EXCEPTION_CONTINUE_SEARCH;
		}

		// virtual address of the inaccessible data
		uintptr_t badAddress = (uintptr_t)pPtrs->ExceptionRecord->ExceptionInformation[1];
		CONTEXT* ctx = pPtrs->ContextRecord;

		if (g_badAccessHandler(badAddress, ctx)) {
			return (DWORD)EXCEPTION_CONTINUE_EXECUTION;
		} else {
			if (g_logCrashStackTrace) {
				ERROR_LOG(Log::System, "Unhandled access violation (%s) at address %016llx, pc=%016llx",
					accessType == 1 ? "write" : "read", (unsigned long long)badAddress, (unsigned long long)(uintptr_t)pPtrs->ExceptionRecord->ExceptionAddress);
				LogCrashStackTrace();
			}
			// Let's not prevent debugging.
			return (DWORD)EXCEPTION_CONTINUE_SEARCH;
		}
	}

	case EXCEPTION_STACK_OVERFLOW:
		// Dolphin has some handling of this for the RET optimization emulation.
		return EXCEPTION_CONTINUE_SEARCH;

	case EXCEPTION_ILLEGAL_INSTRUCTION:
		// No SSE support? Or simply bad codegen?
		return EXCEPTION_CONTINUE_SEARCH;

	case EXCEPTION_PRIV_INSTRUCTION:
		// okay, dynarec codegen is obviously broken.
		return EXCEPTION_CONTINUE_SEARCH;

	case EXCEPTION_IN_PAGE_ERROR:
		// okay, something went seriously wrong, out of memory?
		return EXCEPTION_CONTINUE_SEARCH;

	case EXCEPTION_BREAKPOINT:
		// might want to do something fun with this one day?
		return EXCEPTION_CONTINUE_SEARCH;

	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

void InstallExceptionHandler(BadAccessHandler badAccessHandler, bool logStackTraceOnCrash) {
	g_logCrashStackTrace = logStackTraceOnCrash;
	if (g_vectoredExceptionHandle) {
		g_badAccessHandler = badAccessHandler;
		return;
	}

	INFO_LOG(Log::System, "Installing exception handler");
	g_badAccessHandler = badAccessHandler;

	if (logStackTraceOnCrash && !g_symInitialized) {
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
		g_symInitialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
	}

#ifdef USE_ASAN
	g_vectoredExceptionHandle = AddVectoredExceptionHandler(FALSE, GlobalExceptionHandler);
#else
	g_vectoredExceptionHandle = AddVectoredExceptionHandler(TRUE, GlobalExceptionHandler);
#endif
}

void UninstallExceptionHandler() {
	if (g_vectoredExceptionHandle) {
		RemoveVectoredExceptionHandler(g_vectoredExceptionHandle);
		INFO_LOG(Log::System, "Removed exception handler");
		g_vectoredExceptionHandle = nullptr;
	}
	g_badAccessHandler = nullptr;
}

#else

#include <signal.h>

static struct sigaction old_sa_segv;
static struct sigaction old_sa_bus;
static stack_t old_signal_stack{};
static bool old_signal_stack_valid = false;

// Hand the signal on to whatever was installed before us. Returning from a fault handler just
// re-runs the faulting instruction, so for anything we can't deal with this is the only exit
// that isn't an infinite loop.
static void ChainToPreviousHandler(int sig, siginfo_t *info, void *raw_context) {
	struct sigaction *old_sa = sig == SIGSEGV ? &old_sa_segv : &old_sa_bus;
	// Per the sigaction man page: with SA_SIGINFO it's sa_sigaction, otherwise sa_handler is
	// SIG_DFL, SIG_IGN, or a handler pointer.
	if (old_sa->sa_flags & SA_SIGINFO) {
		old_sa->sa_sigaction(sig, info, raw_context);
		return;
	}
	if (old_sa->sa_handler == SIG_DFL) {
		signal(sig, SIG_DFL);
		return;
	}
	if (old_sa->sa_handler == SIG_IGN) {
		// Ignore signal
		return;
	}
	old_sa->sa_handler(sig);
}

static void sigsegv_handler(int sig, siginfo_t* info, void* raw_context) {
	if (sig != SIGSEGV && sig != SIGBUS) {
		// We are not interested in other signals - handle it as usual.
		return;
	}
	ucontext_t* context = (ucontext_t*)raw_context;
	int sicode = info->si_code;
	// Darwin reports some faults (e.g. on PROT_NONE pages) as SIGBUS.
	bool addressFault = sig == SIGSEGV ? (sicode == SEGV_MAPERR || sicode == SEGV_ACCERR) : (sicode == BUS_ADRERR || sicode == BUS_ADRALN);
	if (!addressFault) {
		// Not an address fault we can do anything with - an MTE, protection-key or shadow
		// stack fault, or a signal sent with kill(). Returning here would re-run the
		// faulting instruction forever at 100% CPU, and would also swallow the signal from
		// whatever was installed before us (a crash reporter, say).
		ChainToPreviousHandler(sig, info, raw_context);
		return;
	}
	uintptr_t bad_address = (uintptr_t)info->si_addr;

	// Get all the information we can out of the context.
#ifdef __OpenBSD__
	ucontext_t* ctx = context;
#elif defined(__APPLE__)
	// uc_mcontext is a pointer here, and the registers are in its thread state.
	SContext* ctx = &context->uc_mcontext->__ss;
#else
	mcontext_t* ctx = &context->uc_mcontext;
#endif
	if (!g_badAccessHandler(bad_address, ctx)) {
		// retry and crash
		// According to the sigaction man page, if sa_flags "SA_SIGINFO" is set to the sigaction
		// function pointer, otherwise sa_handler contains one of:
		// SIG_DEF: The 'default' action is performed
		// SIG_IGN: The signal is ignored
		// Any other value is a function pointer to a signal handler

		ChainToPreviousHandler(sig, info, raw_context);
	}
}

void InstallExceptionHandler(BadAccessHandler badAccessHandler, bool logStackTraceOnCrash) {
	if (!badAccessHandler) {
		return;
	}
	if (g_badAccessHandler) {
		g_badAccessHandler = badAccessHandler;
		return;
	}
	
	size_t altStackSize = SIGSTKSZ;

	// Add some extra room.
	altStackSize += 65536;

	INFO_LOG(Log::System, "Installed exception handler. stack size: %d", (int)altStackSize);
	g_badAccessHandler = badAccessHandler;

	stack_t signal_stack{};
	if (sigaltstack(nullptr, &old_signal_stack) == 0) {
		old_signal_stack_valid = true;
	}
	altStack = malloc(altStackSize);
#ifdef __FreeBSD__
	signal_stack.ss_sp = (char*)altStack;
#else
	signal_stack.ss_sp = altStack;
#endif
	signal_stack.ss_size = altStackSize;
	signal_stack.ss_flags = 0;
	if (sigaltstack(&signal_stack, nullptr)) {
		_assert_msg_(false, "sigaltstack failed");
	}
	struct sigaction sa{};
	sa.sa_handler = nullptr;
	sa.sa_sigaction = &sigsegv_handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, &old_sa_segv);
#ifdef __APPLE__
	sigaction(SIGBUS, &sa, &old_sa_bus);
#endif
}

void UninstallExceptionHandler() {
	if (!g_badAccessHandler) {
		return;
	}
	if (old_signal_stack_valid) {
		// Darwin fails with ENOMEM on a size below MINSIGSTKSZ, even with SS_DISABLE.
		if ((old_signal_stack.ss_flags & SS_DISABLE) && old_signal_stack.ss_size < MINSIGSTKSZ) {
			old_signal_stack.ss_size = MINSIGSTKSZ;
		}
		if (0 != sigaltstack(&old_signal_stack, nullptr)) {
			ERROR_LOG(Log::System, "Could not restore previous signal altstack");
		}
		old_signal_stack_valid = false;
	}
	if (altStack) {
		free(altStack);
		altStack = nullptr;
	}
	sigaction(SIGSEGV, &old_sa_segv, nullptr);
#ifdef __APPLE__
	sigaction(SIGBUS, &old_sa_bus, nullptr);
#endif
	INFO_LOG(Log::System, "Uninstalled exception handler");
	g_badAccessHandler = nullptr;
}

#endif

#else  // !MACHINE_CONTEXT_SUPPORTED

void InstallExceptionHandler(BadAccessHandler badAccessHandler, bool logStackTraceOnCrash) {
	ERROR_LOG(Log::System, "Exception handler not implemented on this platform, can't install");
}
void UninstallExceptionHandler() { }

#endif  // MACHINE_CONTEXT_SUPPORTED
