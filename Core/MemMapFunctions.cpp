// Copyright (C) 2003 Dolphin Project / 2012 PPSSPP Project

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

#include "Common/CommonTypes.h"
#include "Common/LogReporting.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"

#include "Core/MIPS/MIPS.h"

namespace Memory {

u8 *GetPointerWriteOrException(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0xBF800000) == 0x04000000 || // VRAM
		(address & 0x3FFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3FFFFFFF) >= 0x08000000 && (address & 0x3FFFFFFF) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		return GetPointerWriteUnchecked(address);
	} else {
		// Size is not known, we pass 0 to signal that.
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
		return nullptr;
	}
}

const u8 *GetPointerOrException(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0xBF800000) == 0x04000000 || // VRAM
		(address & 0x3FFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3FFFFFFF) >= 0x08000000 && (address & 0x3FFFFFFF) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		return GetPointerUnchecked(address);
	} else {
		// Size is not known, we pass 0 to signal that.
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
		return nullptr;
	}
}

u8 *GetPointerWriteRangeOrException(const u32 address, const u32 size) {
	u8 *ptr = GetPointerWriteOrException(address);
	if (ptr) {
		if (ClampValidSizeAt(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was handled in GetPointerWrite already, if we're not ignoring errors.
		return nullptr;
	}
}

const u8 *GetPointerRangeOrException(const u32 address, const u32 size) {
	const u8 *ptr = GetPointerOrException(address);
	if (ptr) {
		if (ClampValidSizeAt(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was handled in GetPointer already, if we're not ignoring errors.
		return nullptr;
	}
}

template <typename T>
inline void ReadMemoryOrException(T &var, const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0xBF800000) == 0x04000000 || // VRAM
		(address & 0x3FFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3FFFFFFF) >= 0x08000000 && (address & 0x3FFFFFFF) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		var = *((const T*)GetPointerUnchecked(address));
	} else {
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::READ_WORD);
		var = 0;
	}
}

template <typename T>
inline void WriteMemoryOrException(u32 address, const T data) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0xBF800000) == 0x04000000 || // VRAM
		(address & 0x3FFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3FFFFFFF) >= 0x08000000 && (address & 0x3FFFFFFF) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		*(T*)GetPointerUnchecked(address) = data;
	} else {
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::WRITE_WORD);
	}
}

bool IsRAMAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	} else if ((address & 0x3FFFFFFF) >= 0x08000000 && (address & 0x3FFFFFFF) < 0x08000000 + g_MemorySize) {
		return true;
	} else {
		return false;
	}
}

bool IsScratchpadAddress(const u32 address) {
	// Ignore both the kernel bit (0x80000000) and the uncached bit (0x40000000) - the
	// scratchpad is mirrored across all four combinations, same as RAM (see MemMap.cpp).
	return (address & ~(0xC0000000 | (SCRATCHPAD_SIZE - 1))) == 0x00010000;
}

u8 ReadOrException_U8(const u32 address) {
	u8 value = 0;
	ReadMemoryOrException<u8>(value, address);
	return (u8)value;
}

u16 ReadOrException_U16(const u32 address) {
	u16_le value = 0;
	ReadMemoryOrException<u16_le>(value, address);
	return (u16)value;
}

u32 ReadOrException_U32(const u32 address) {
	u32_le value = 0;
	ReadMemoryOrException<u32_le>(value, address);
	return value;
}

u64 ReadOrException_U64(const u32 address) {
	u64_le value = 0;
	ReadMemoryOrException<u64_le>(value, address);
	return value;
}

void WriteOrException_U8(const u8 _Data, const u32 address) {
	WriteMemoryOrException<u8>(address, _Data);
}

void WriteOrException_U16(const u16 _Data, const u32 address) {
	WriteMemoryOrException<u16_le>(address, _Data);
}

void WriteOrException_U32(const u32 _Data, const u32 address) {
	WriteMemoryOrException<u32_le>(address, _Data);
}

void WriteOrException_U64(const u64 _Data, const u32 address) {
	WriteMemoryOrException<u64_le>(address, _Data);
}

}	// namespace Memory
