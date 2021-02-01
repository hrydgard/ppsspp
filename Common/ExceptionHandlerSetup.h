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

void InstallExceptionHandler(BadAccessHandler accessHandler);
void UninstallExceptionHandler();
