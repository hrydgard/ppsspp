#pragma once

#include <cstdint>

namespace Memory {

void MemFault_Init();

// Called by exception handlers. We simply filter out accesses to PSP RAM and otherwise
// just leave it as-is.
bool HandleFault(uintptr_t hostAddress, void *context);

}
