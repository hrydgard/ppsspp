// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// The corresponding file is called MemTools in the Dolphin project.

#include "ppsspp_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
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

#elif defined(__APPLE__)

static void CheckKR(const char* name, kern_return_t kr) {
	_assert_msg_(kr == 0, "%s failed: kr=%x", name, kr);
}

static void ExceptionThread(mach_port_t port) {
	SetCurrentThreadName("Mach exception thread");
#pragma pack(4)
	struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		exception_type_t exception;
		mach_msg_type_number_t codeCnt;
		int64_t code[2];
		int flavor;
		mach_msg_type_number_t old_stateCnt;
		natural_t old_state[x86_THREAD_STATE64_COUNT];
		mach_msg_trailer_t trailer;
	} msg_in;

	struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int flavor;
		mach_msg_type_number_t new_stateCnt;
		natural_t new_state[x86_THREAD_STATE64_COUNT];
	} msg_out;
#pragma pack()
	memset(&msg_in, 0xee, sizeof(msg_in));
	memset(&msg_out, 0xee, sizeof(msg_out));
	mach_msg_header_t* send_msg = &msg_out.Head;
	mach_msg_size_t send_size = 0;
	mach_msg_option_t option = MACH_RCV_MSG;
	while (true) {
		// If this isn't the first run, send the reply message.  Then, receive
		// a message: either a mach_exception_raise_state RPC due to
		// thread_set_exception_ports, or MACH_NOTIFY_NO_SENDERS due to
		// mach_port_request_notification.
		CheckKR("mach_msg_overwrite",
			mach_msg_overwrite(send_msg, option, send_size, sizeof(msg_in), port,
				MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL, &msg_in.Head, 0));

		if (msg_in.Head.msgh_id == MACH_NOTIFY_NO_SENDERS) {
			// the other thread exited
			mach_port_destroy(mach_task_self(), port);
			return;
		}

		_assert_msg_(msg_in.Head.msgh_id == 2406, "unknown message received");
		_assert_msg_(msg_in.flavor == x86_THREAD_STATE64, "unknown flavor %d (expected %d)", msg_in.flavor, x86_THREAD_STATE64);

		x86_thread_state64_t* state = (x86_thread_state64_t*)msg_in.old_state;

		bool ok = g_badAccessHandler((uintptr_t)msg_in.code[1], state);

		// Set up the reply.
		msg_out.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(msg_in.Head.msgh_bits), 0);
		msg_out.Head.msgh_remote_port = msg_in.Head.msgh_remote_port;
		msg_out.Head.msgh_local_port = MACH_PORT_NULL;
		msg_out.Head.msgh_id = msg_in.Head.msgh_id + 100;
		msg_out.NDR = msg_in.NDR;
		if (ok) {
			msg_out.RetCode = KERN_SUCCESS;
			msg_out.flavor = x86_THREAD_STATE64;
			msg_out.new_stateCnt = x86_THREAD_STATE64_COUNT;
			memcpy(msg_out.new_state, msg_in.old_state, x86_THREAD_STATE64_COUNT * sizeof(natural_t));
		} else {
			// Pass the exception to the next handler (debugger or crash).
			msg_out.RetCode = KERN_FAILURE;
			msg_out.flavor = 0;
			msg_out.new_stateCnt = 0;
		}
		msg_out.Head.msgh_size =
			offsetof(__typeof__(msg_out), new_state) + msg_out.new_stateCnt * sizeof(natural_t);

		send_msg = &msg_out.Head;
		send_size = msg_out.Head.msgh_size;
		option |= MACH_SEND_MSG;
	}
}

void InstallExceptionHandler(BadAccessHandler badAccessHandler, bool logStackTraceOnCrash) {
	if (g_badAccessHandler) {
		// The rest of the setup we don't need to do again.
		g_badAccessHandler = badAccessHandler;
		return;
	}
	g_badAccessHandler = badAccessHandler;

	INFO_LOG(Log::System, "Installing exception handler");
	mach_port_t port;
	CheckKR("mach_port_allocate",
		mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port));
	std::thread exc_thread(ExceptionThread, port);
	exc_thread.detach();
	// Obtain a send right for thread_set_exception_ports to copy...
	CheckKR("mach_port_insert_right",
		mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND));
	// Mach tries the following exception ports in order: thread, task, host.
	// Debuggers set the task port, so we grab the thread port.
	CheckKR("thread_set_exception_ports",
		thread_set_exception_ports(mach_thread_self(), EXC_MASK_BAD_ACCESS, port,
			EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64));
	// ...and get rid of our copy so that MACH_NOTIFY_NO_SENDERS works.
	CheckKR("mach_port_mod_refs",
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, -1));
	mach_port_t previous;
	CheckKR("mach_port_request_notification",
		mach_port_request_notification(mach_task_self(), port, MACH_NOTIFY_NO_SENDERS, 0, port,
			MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous));
}

void UninstallExceptionHandler() {
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
	if (sicode != SEGV_MAPERR && sicode != SEGV_ACCERR) {
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
#else
	mcontext_t* ctx = &context->uc_mcontext;
#endif
	// assume it's not a write
	if (!g_badAccessHandler(bad_address,
#ifdef __APPLE__
		*ctx
#else
		ctx
#endif
	)) {
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
