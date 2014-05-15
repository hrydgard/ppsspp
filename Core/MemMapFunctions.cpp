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

#include "Common/Common.h"
#include "Common/Atomics.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/Reporting.h"

#include "Core/MIPS/MIPS.h"

namespace Memory
{

// =================================
// From Memmap.cpp
// ----------------

// Read and write shortcuts

// GetPointer must always return an address in the bottom 32 bits of address space, so that 64-bit
// programs don't have problems directly addressing any part of memory.

u8 *GetPointer(const u32 address)
{
	if ((address & 0x3E000000) == 0x08000000) {
		// RAM
		return GetPointerUnchecked(address);
	} else if ((address & 0x3F800000) == 0x04000000) {
		// VRAM
		return GetPointerUnchecked(address);
	} else if ((address & 0xBFFF0000) == 0x00010000) {
		// Scratchpad
		return GetPointerUnchecked(address);
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		// More RAM (remasters, etc.)
		return GetPointerUnchecked(address);
	} else {
		ERROR_LOG(MEMMAP, "Unknown GetPointer %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("Unknown GetPointer %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		if (!g_Config.bIgnoreBadMemAccess) {
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
		return 0;
	}
}

template <typename T>
inline void ReadFromHardware(T &var, const u32 address)
{
	// TODO: Figure out the fastest order of tests for both read and write (they are probably different).
	// TODO: Make sure this represents the mirrors in a correct way.

	// Could just do a base-relative read, too.... TODO

	if ((address & 0x3E000000) == 0x08000000) {
		// RAM
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0x3F800000) == 0x04000000) {
		// VRAM
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0xBFFF0000) == 0x00010000) {
		// Scratchpad
		var = *((const T*)GetPointerUnchecked(address));
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		// More RAM (remasters, etc.)
		var = *((const T*)GetPointerUnchecked(address));
	} else {
		if (g_Config.bJit) {
			WARN_LOG(MEMMAP, "ReadFromHardware: Invalid address %08x", address);
		} else {
			WARN_LOG(MEMMAP, "ReadFromHardware: Invalid address %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
		}
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("ReadFromHardware: Invalid address %08x near PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		if (!g_Config.bIgnoreBadMemAccess) {
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
		var = 0;
	}
}

template <typename T>
inline void WriteToHardware(u32 address, const T data)
{
	// Could just do a base-relative write, too.... TODO

	if ((address & 0x3E000000) == 0x08000000) {
		// RAM
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0x3F800000) == 0x04000000) {
		// VRAM
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0xBFFF0000) == 0x00010000) {
		// Scratchpad
		*(T*)GetPointerUnchecked(address) = data;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		// More RAM (remasters, etc.)
		*(T*)GetPointerUnchecked(address) = data;
	} else {
		if (g_Config.bJit) {
			WARN_LOG(MEMMAP, "WriteToHardware: Invalid address %08x", address);
		} else {
			WARN_LOG(MEMMAP, "WriteToHardware: Invalid address %08x	PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
		}
		static bool reported = false;
		if (!reported) {
			Reporting::ReportMessage("WriteToHardware: Invalid address %08x near PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
			reported = true;
		}
		if (!g_Config.bIgnoreBadMemAccess) {
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
	}
}

// =====================

bool IsRAMAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	}	else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return true;
	}	else {
		return false;
	}
}

bool IsVRAMAddress(const u32 address) {
	return ((address & 0x3F800000) == 0x04000000);
}

u8 Read_U8(const u32 _Address)
{		
	u8 _var = 0;
	ReadFromHardware<u8>(_var, _Address);
	return (u8)_var;
}

u16 Read_U16(const u32 _Address)
{
	u16_le _var = 0;
	ReadFromHardware<u16_le>(_var, _Address);
	return (u16)_var;
}

u32 Read_U32(const u32 _Address)
{
	u32_le _var = 0;
	ReadFromHardware<u32_le>(_var, _Address);
	return _var;
}

u64 Read_U64(const u32 _Address)
{
	u64_le _var = 0;
	ReadFromHardware<u64_le>(_var, _Address);
	return _var;
}

u32 Read_U8_ZX(const u32 _Address)
{
	return (u32)Read_U8(_Address);
}

u32 Read_U16_ZX(const u32 _Address)
{
	return (u32)Read_U16(_Address);
}

void Write_U8(const u8 _Data, const u32 _Address)	
{
	WriteToHardware<u8>(_Address, _Data);
}

void Write_U16(const u16 _Data, const u32 _Address)
{
	WriteToHardware<u16_le>(_Address, _Data);
}

void Write_U32(const u32 _Data, const u32 _Address)
{	
	WriteToHardware<u32_le>(_Address, _Data);
}

void Write_U64(const u64 _Data, const u32 _Address)
{
	WriteToHardware<u64_le>(_Address, _Data);
}

#ifdef SAFE_MEMORY

u8 ReadUnchecked_U8(const u32 _Address)
{
	u8 _var = 0;
	ReadFromHardware<u8>(_var, _Address);
	return _var;
}

u16 ReadUnchecked_U16(const u32 _Address)
{
	u16_le _var = 0;
	ReadFromHardware<u16_le>(_var, _Address);
	return _var;
}

u32 ReadUnchecked_U32(const u32 _Address)
{
	u32_le _var = 0;
	ReadFromHardware<u32_le>(_var, _Address);
	return _var;
}

void WriteUnchecked_U8(const u8 _iValue, const u32 _Address)
{
	WriteToHardware<u8>(_Address, _iValue);
}

void WriteUnchecked_U16(const u16 _iValue, const u32 _Address)
{
	WriteToHardware<u16_le>(_Address, _iValue);
}

void WriteUnchecked_U32(const u32 _iValue, const u32 _Address)
{
	WriteToHardware<u32_le>(_Address, _iValue);
}

#endif

}	// namespace Memory
