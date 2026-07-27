// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstdint>

// On Windows, context is a CONTEXT object.
// On Apple, context is a x86_thread_state64_t.
// On Unix/Linux, context is a mcontext_t.
// On OpenBSD, context is a ucontext_t.
// Ugh, might need to abstract this better.
typedef bool (*BadAccessHandler)(uintptr_t address, void *context);

// logStackTraceOnCrash: if true, log a best-effort stack trace (via dbghelp on Windows)
// when a genuinely unhandled access violation is about to crash the process. Off by
// default since it links dbghelp and walks the stack from inside the exception handler -
// opt in only when you're chasing a native crash.
void InstallExceptionHandler(BadAccessHandler accessHandler, bool logStackTraceOnCrash = false);

// Implementation note: This must be a no-op if InstallExceptionHandler hasn't been called.
void UninstallExceptionHandler();
