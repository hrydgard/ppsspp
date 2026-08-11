#pragma once
#include <cstring>
#include "Core/MemMap.h"

// Used in various places in the PSP OS.
template<typename T> bool ReadVariableSizedStruct(u32 addr, T *out) {
	int size = Memory::ReadUnchecked_U32(addr);
	if (!Memory::IsValid4AlignedRange(addr, size)) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	// Only copy the right size to support different struct versions.
	// Let's add a debug assert in case we have a struct that is too small for the data.
	_dbg_assert_(sizeof(*out) >= size);
	Memory::Memcpy(out, addr, std::min(size, (int)sizeof(*out)));
	return true;
}
