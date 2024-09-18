#pragma once

#include <cstdint>

#include "Core/MIPS/MIPSStackWalk.h"

namespace Memory {

void MemFault_Init();

// Used by the hacky "Resume" button.
//
// TODO: Add a way to actually resume to the next instruction,
// rather than just jumping into the dispatcher again and hoping for the best. That will be
// a little tricky though, with per-backend work.
bool MemFault_MayBeResumable();
void MemFault_IgnoreLastCrash();

// Called by exception handlers. We simply filter out accesses to PSP RAM and otherwise
// just leave it as-is.
bool HandleFault(uintptr_t hostAddress, void *context);

}

// Stack walk utility function, walks from the current state. Useful in the debugger and crash report screens etc.
// Doesn't exactly belong here maybe, but can think of worse locations.
// threadID can be -1 for current.
std::vector<MIPSStackWalk::StackFrame> WalkCurrentStack(int threadID);

std::string FormatStackTrace(const std::vector<MIPSStackWalk::StackFrame> &frames);
