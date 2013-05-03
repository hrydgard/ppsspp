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

#include "Common.h"
#include "Atomics.h"

#include "MemMap.h"
#include "Config.h"
#include "Host.h"

#include "MIPS/MIPS.h"

// TODO: Fix this
#undef ENABLE_MEM_CHECK

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
	if ((address & 0x3E000000) == 0x08000000)
	{
		return m_pRAM + (address & RAM_MASK);
	}
	else if ((address & 0x3F800000) == 0x04000000)
	{
		return m_pVRAM + (address & VRAM_MASK);
	}
	else if ((address & 0xBFFF0000) == 0x00010000)
	{
		return m_pScratchPad + (address & SCRATCHPAD_MASK);
	}
	else
	{
		ERROR_LOG(MEMMAP, "Unknown GetPointer %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
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

	if ((address & 0x3E000000) == 0x08000000)
	{
		var = *((const T*)&m_pRAM[address & RAM_MASK]);
	}
	else if ((address & 0x3F800000) == 0x04000000)
	{
		var = *((const T*)&m_pVRAM[address & VRAM_MASK]);
	}
	else if ((address & 0xBFFF0000) == 0x00010000)
	{
		// Scratchpad
		var = *((const T*)&m_pScratchPad[address & SCRATCHPAD_MASK]);
	}
	else
	{
		if (g_Config.bJit) {
			WARN_LOG(MEMMAP, "ReadFromHardware: Invalid address %08x", address);
		} else {
			WARN_LOG(MEMMAP, "ReadFromHardware: Invalid address %08x PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
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

	if ((address & 0x3E000000) == 0x08000000)
	{
		*(T*)&m_pRAM[address & RAM_MASK] = data;
	}
	else if ((address & 0x3F800000) == 0x04000000)
	{
		*(T*)&m_pVRAM[address & VRAM_MASK] = data;
	}
	else if ((address & 0xBFFF0000) == 0x00010000)
	{
		*(T*)&m_pScratchPad[address & SCRATCHPAD_MASK] = data;
	}
	else
	{
		if (g_Config.bJit) {
			WARN_LOG(MEMMAP, "WriteToHardware: Invalid address %08x", address);
		} else {
			WARN_LOG(MEMMAP, "WriteToHardware: Invalid address %08x	PC %08x LR %08x", address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
		}
		if (!g_Config.bIgnoreBadMemAccess) {
			Core_EnableStepping(true);
			host->SetDebugMode(true);
		}
	}
}

// =====================

bool IsValidAddress(const u32 address)
{
	if ((address & 0x3E000000) == 0x08000000)
	{
		return true;
	}
	else if ((address & 0x3F800000) == 0x04000000)
	{
		return true;
	}
	else if ((address & 0xBFFF0000) == 0x00010000)
	{
		return true;
	}
	else
		return false;
}

u8 Read_U8(const u32 _Address)
{		
	u8 _var = 0;
	ReadFromHardware<u8>(_var, _Address);
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _var, _Address, false, 1, PC);
	}
#endif
	return (u8)_var;
}

u16 Read_U16(const u32 _Address)
{
	u16 _var = 0;
	ReadFromHardware<u16>(_var, _Address);
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _var, _Address, false, 2, PC);
	}
#endif
	return (u16)_var;
}

u32 Read_U32(const u32 _Address)
{
	u32 _var = 0;	
	ReadFromHardware<u32>(_var, _Address);
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _var, _Address, false, 4, PC);
	}
#endif
	return _var;
}

u64 Read_U64(const u32 _Address)
{
	u64 _var = 0;
	ReadFromHardware<u64>(_var, _Address);
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, (u32)_var, _Address, false, 8, PC);
	}
#endif
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
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _Data,_Address,true,1,PC);
	}
#endif
	WriteToHardware<u8>(_Address, _Data);
}


void Write_U16(const u16 _Data, const u32 _Address)
{
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _Data,_Address,true,2,PC);
	}
#endif

	WriteToHardware<u16>(_Address, _Data);
}

void Write_U32(const u32 _Data, const u32 _Address)
{	
#ifdef ENABLE_MEM_CHECK
	TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, _Data,_Address,true,4,PC);
	}
#endif
	WriteToHardware<u32>(_Address, _Data);
}

void Write_U64(const u64 _Data, const u32 _Address)
{
#ifdef ENABLE_MEM_CHECK
		TMemCheck *mc = PowerPC::memchecks.GetMemCheck(_Address);
	if (mc)
	{
		mc->numHits++;
		mc->Action(&PowerPC::debug_interface, (u32)_Data,_Address,true,8,PC);
	}
#endif

	WriteToHardware<u64>(_Address, _Data);
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
	u16 _var = 0;
	ReadFromHardware<u16>(_var, _Address);
	return _var;
}

u32 ReadUnchecked_U32(const u32 _Address)
{
	u32 _var = 0;
	ReadFromHardware<u32>(_var, _Address);
	return _var;
}

void WriteUnchecked_U8(const u8 _iValue, const u32 _Address)
{
	WriteToHardware<u8>(_Address, _iValue);
}

void WriteUnchecked_U16(const u16 _iValue, const u32 _Address)
{
	WriteToHardware<u16>(_Address, _iValue);
}

void WriteUnchecked_U32(const u32 _iValue, const u32 _Address)
{
	WriteToHardware<u32>(_Address, _iValue);
}

#endif

}	// namespace Memory
