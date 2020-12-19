#pragma once

#include <cstdint>

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
