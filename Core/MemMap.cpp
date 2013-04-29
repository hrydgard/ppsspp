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
#include "MemoryUtil.h"
#include "MemArena.h"
#include "ChunkFile.h"

#include "MemMap.h"
#include "Core.h"
#include "MIPS/MIPS.h"
#include "MIPS/JitCommon/JitCommon.h"
#include "HLE/HLE.h"
#include "CPU.h"
#include "Debugger/SymbolMap.h"

namespace Memory
{

// The base pointer to the auto-mirrored arena.
u8*	base = NULL;

// The MemArena class
MemArena g_arena;
// ==============

// 64-bit: Pointers to low-mem (sub-0x10000000) mirror
// 32-bit: Same as the corresponding physical/virtual pointers.
u8 *m_pRAM;
u8 *m_pScratchPad;
u8 *m_pVRAM;

u8 *m_pPhysicalScratchPad;
u8 *m_pUncachedScratchPad;
// 64-bit: Pointers to high-mem mirrors
// 32-bit: Same as above
u8 *m_pPhysicalRAM;
u8 *m_pUncachedRAM;
u8 *m_pKernelRAM;	// RAM mirrored up to "kernel space". Fully accessible at all times currently.

u8 *m_pPhysicalVRAM;
u8 *m_pUncachedVRAM;


// We don't declare the IO region in here since its handled by other means.
static const MemoryView views[] =
{
	{&m_pScratchPad, &m_pPhysicalScratchPad,  0x00010000, SCRATCHPAD_SIZE, 0},
	{NULL,           &m_pUncachedScratchPad,  0x40010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS},
	{&m_pVRAM,       &m_pPhysicalVRAM,        0x04000000, 0x00800000, 0},
	{NULL,           &m_pUncachedVRAM,        0x44000000, 0x00800000, MV_MIRROR_PREVIOUS},
	{&m_pRAM,        &m_pPhysicalRAM,         0x08000000, RAM_SIZE, 0},	// only from 0x08800000 is it usable (last 24 megs)
	{NULL,           &m_pUncachedRAM,         0x48000000, RAM_SIZE, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pKernelRAM,           0x88000000, RAM_SIZE, MV_MIRROR_PREVIOUS},

	// TODO: There are a few swizzled mirrors of VRAM, not sure about the best way to
	// implement those.
};

static const int num_views = sizeof(views) / sizeof(MemoryView);

void Init()
{
	int flags = 0;
	base = MemoryMap_Setup(views, num_views, flags, &g_arena);

	INFO_LOG(MEMMAP, "Memory system initialized. RAM at %p (mirror at 0 @ %p, uncached @ %p)",
		m_pRAM, m_pPhysicalRAM, m_pUncachedRAM);
}

void DoState(PointerWrap &p)
{
	p.DoArray(m_pRAM, RAM_SIZE);
	p.DoMarker("RAM");
	p.DoArray(m_pVRAM, VRAM_SIZE);
	p.DoMarker("VRAM");
	p.DoArray(m_pScratchPad, SCRATCHPAD_SIZE);
	p.DoMarker("ScratchPad");
}

void Shutdown()
{
	u32 flags = 0;
	MemoryMap_Shutdown(views, num_views, flags, &g_arena);
	g_arena.ReleaseSpace();
	base = NULL;
	INFO_LOG(MEMMAP, "Memory system shut down.");
}

void Clear()
{
	if (m_pRAM)
		memset(m_pRAM, 0, RAM_SIZE);
	if (m_pScratchPad)
		memset(m_pScratchPad, 0, SCRATCHPAD_SIZE);
	if (m_pVRAM)
		memset(m_pVRAM, 0, VRAM_SIZE);
}

u32 Read_Instruction(u32 address)
{
	u32 inst = Read_U32(address);	
	if (MIPS_IS_EMUHACK(inst) && MIPSComp::jit)
	{
		JitBlockCache *bc = MIPSComp::jit->GetBlockCache();
		int block_num = bc->GetBlockNumberFromEmuHackOp(inst);
		if (block_num >= 0) {
			return bc->GetOriginalFirstOp(block_num);
		} else {
			return inst;
		}
	} else {
		return inst;
	}
}

u32 Read_Opcode_JIT(u32 address)
{
	return Read_Instruction(address);
}

// WARNING! No checks!
// We assume that _Address is cached
void Write_Opcode_JIT(const u32 _Address, const u32 _Value)
{
	Memory::WriteUnchecked_U32(_Value, _Address);
}

void Memset(const u32 _Address, const u8 _iValue, const u32 _iLength)
{	
	u8 *ptr = GetPointer(_Address);
	if (ptr != NULL)
	{
		memset(ptr,_iValue,_iLength);
	}
	else
	{
		for (size_t i = 0; i < _iLength; i++)
			Write_U8(_iValue, (u32)(_Address + i));
	}
}

void Memcpy(const u32 to_address, const void *from_data, const u32 len)
{
	memcpy(GetPointer(to_address), from_data, len);
}

void Memcpy(void *to_data, const u32 from_address, const u32 len)
{
	memcpy(to_data,GetPointer(from_address),len);
}

void GetString(std::string& _string, const u32 em_address)
{
	char stringBuffer[2048];
	char *string = stringBuffer;
	char c;
	u32 addr = em_address;
	while ((c = Read_U8(addr)))
	{
		*string++ = c;
		addr++;
	}
	*string++ = '\0';
	_string = stringBuffer;
}

const char *GetAddressName(u32 address)
{
	// TODO, follow GetPointer
	return "[mem]";
}

} // namespace
