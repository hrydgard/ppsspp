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

u8 *GetPointerWrite(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		return GetPointerWriteUnchecked(address);
	} else {
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("Unknown GetPointerWrite %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}

		// Size is not known, we pass 0 to signal that.
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
		return nullptr;
	}
}

const u8 *GetPointer(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize)) { // More RAM (remasters, etc.)
		return GetPointerUnchecked(address);
	} else {
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("Unknown GetPointer %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		// Size is not known, we pass 0 to signal that.
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
		return nullptr;
	}
}

u8 *GetPointerWriteRange(const u32 address, const u32 size) {
	u8 *ptr = GetPointerWrite(address);
	if (ptr) {
		if (ValidSize(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was reported in GetPointerWrite already, if we're not ignoring errors.
		return nullptr;
	}
}

const u8 *GetPointerRange(const u32 address, const u32 size) {
	const u8 *ptr = GetPointer(address);
	if (ptr) {
		if (ValidSize(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was reported in GetPointer already, if we're not ignoring errors.
		return nullptr;
	}
}

template <typename T>
inline void ReadFromHardware(T &var, const u32 address) {
	// TODO: Figure out the fastest order of tests for both read and write (they are probably different).
	if ((address & 0x3E000000) == 0x08000000) {
		// RAM
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0x3F800000) == 0x04000000) {
		// VRAM
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0xBFFFC000) == 0x00010000) {
		// Scratchpad
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		// More RAM (remasters, etc.)
		var = *((const T*)GetPointerUnchecked(address));
	} else {
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("ReadFromHardware: Invalid address %08x near PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::READ_WORD);
		var = 0;
	}
}

template <typename T>
inline void WriteToHardware(u32 address, const T data) {
	if ((address & 0x3E000000) == 0x08000000) {
		// RAM
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0x3F800000) == 0x04000000) {
		// VRAM
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0xBFFFC000) == 0x00010000) {
		// Scratchpad
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		// More RAM (remasters, etc.)
		*(T*)GetPointerUnchecked(address) = data;
	} else {
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("WriteToHardware: Invalid address %08x near PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::WRITE_WORD);
	}
}

bool IsRAMAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return true;
	} else {
		return false;
	}
}

bool IsScratchpadAddress(const u32 address) {
	return (address & 0xBFFFC000) == 0x00010000;
}

u8 Read_U8(const u32 address) {
	u8 value = 0;
	ReadFromHardware<u8>(value, address);
	return (u8)value;
}

u16 Read_U16(const u32 address) {
	u16_le value = 0;
	ReadFromHardware<u16_le>(value, address);
	return (u16)value;
}

u32 Read_U32(const u32 address) {
	u32_le value = 0;
	ReadFromHardware<u32_le>(value, address);
	return value;
}

u64 Read_U64(const u32 address) {
	u64_le value = 0;
	ReadFromHardware<u64_le>(value, address);
	return value;
}

u32 Read_U8_ZX(const u32 address) {
	return (u32)Read_U8(address);
}

u32 Read_U16_ZX(const u32 address) {
	return (u32)Read_U16(address);
}

void Write_U8(const u8 _Data, const u32 address) {
	WriteToHardware<u8>(address, _Data);
}

void Write_U16(const u16 _Data, const u32 address) {
	WriteToHardware<u16_le>(address, _Data);
}

void Write_U32(const u32 _Data, const u32 address) {
	WriteToHardware<u32_le>(address, _Data);
}

void Write_U64(const u64 _Data, const u32 address) {
	WriteToHardware<u64_le>(address, _Data);
}

}	// namespace Memory
