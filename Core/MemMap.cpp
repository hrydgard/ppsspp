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

#include <algorithm>

#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "Common/MemArena.h"
#include "Common/ChunkFile.h"

#include "Core/MemMap.h"
#include "Core/HDRemaster.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/HLE/HLE.h"

#include "Core/Core.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Config.h"
#include "Core/HLE/ReplaceTables.h"

namespace Memory {

// The base pointer to the auto-mirrored arena.
u8*	base = NULL;

// The MemArena class
MemArena g_arena;
// ==============

// 64-bit: Pointers to low-mem (sub-0x10000000) mirror
// 32-bit: Same as the corresponding physical/virtual pointers.
u8 *m_pRAM;
u8 *m_pRAM2;
u8 *m_pRAM3;
u8 *m_pScratchPad;
u8 *m_pVRAM;

u8 *m_pPhysicalScratchPad;
u8 *m_pUncachedScratchPad;
// 64-bit: Pointers to high-mem mirrors
// 32-bit: Same as above
u8 *m_pPhysicalRAM;
u8 *m_pUncachedRAM;
u8 *m_pKernelRAM;	// RAM mirrored up to "kernel space". Fully accessible at all times currently.
u8 *m_pPhysicalRAM2;
u8 *m_pUncachedRAM2;
u8 *m_pKernelRAM2;
u8 *m_pPhysicalRAM3;
u8 *m_pUncachedRAM3;
u8 *m_pKernelRAM3;

u8 *m_pPhysicalVRAM;
u8 *m_pUncachedVRAM;

// Holds the ending address of the PSP's user space.
// Required for HD Remasters to work properly.
// These replace RAM_NORMAL_SIZE and RAM_NORMAL_MASK, respectively.
u32 g_MemorySize;
u32 g_MemoryMask;
// Used to store the PSP model on game startup.
u32 g_PSPModel;

// We don't declare the IO region in here since its handled by other means.
static MemoryView views[] =
{
	{&m_pScratchPad, &m_pPhysicalScratchPad,  0x00010000, SCRATCHPAD_SIZE, 0},
	{NULL,           &m_pUncachedScratchPad,  0x40010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS},
	{&m_pVRAM,       &m_pPhysicalVRAM,        0x04000000, 0x00800000, 0},
	{NULL,           &m_pUncachedVRAM,        0x44000000, 0x00800000, MV_MIRROR_PREVIOUS},
	{&m_pRAM,        &m_pPhysicalRAM,         0x08000000, g_MemorySize, MV_IS_PRIMARY_RAM},	// only from 0x08800000 is it usable (last 24 megs)
	{NULL,           &m_pUncachedRAM,         0x48000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM},
	{NULL,           &m_pKernelRAM,           0x88000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM},
	// Starts at memory + 31 MB.
	{&m_pRAM2,       &m_pPhysicalRAM2,        0x09F00000, g_MemorySize, MV_IS_EXTRA1_RAM},
	{NULL,           &m_pUncachedRAM2,        0x49F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM},
	{NULL,           &m_pKernelRAM2,          0x89F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM},
	// Starts at memory + 31 * 2 MB.
	{&m_pRAM3,       &m_pPhysicalRAM3,        0x0BE00000, g_MemorySize, MV_IS_EXTRA2_RAM},
	{NULL,           &m_pUncachedRAM3,        0x4BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM},
	{NULL,           &m_pKernelRAM3,          0x8BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM},

	// TODO: There are a few swizzled mirrors of VRAM, not sure about the best way to
	// implement those.
};

static const int num_views = sizeof(views) / sizeof(MemoryView);

void Init()
{
	int flags = 0;
	// This mask is used ONLY after validating the address is in the correct range.
	// So let's just use a fixed mask to remove the uncached/user memory bits.
	// Using (Memory::g_MemorySize - 1) won't work for e.g. 0x04C00000.
	Memory::g_MemoryMask = 0x07FFFFFF;

	// On some 32 bit platforms, you can only map < 32 megs at a time.
	const static int MAX_MMAP_SIZE = 31 * 1024 * 1024;
	_dbg_assert_msg_(MEMMAP, g_MemorySize < MAX_MMAP_SIZE * 3, "ACK - too much memory for three mmap views.");
	for (size_t i = 0; i < ARRAY_SIZE(views); i++) {
		if (views[i].flags & MV_IS_PRIMARY_RAM)
			views[i].size = std::min((int)g_MemorySize, MAX_MMAP_SIZE);
		if (views[i].flags & MV_IS_EXTRA1_RAM)
			views[i].size = std::min(std::max((int)g_MemorySize - MAX_MMAP_SIZE, 0), MAX_MMAP_SIZE);
		if (views[i].flags & MV_IS_EXTRA2_RAM)
			views[i].size = std::min(std::max((int)g_MemorySize - MAX_MMAP_SIZE * 2, 0), MAX_MMAP_SIZE);
	}
	base = MemoryMap_Setup(views, num_views, flags, &g_arena);

	INFO_LOG(MEMMAP, "Memory system initialized. RAM at %p (mirror at 0 @ %p, uncached @ %p)",
		m_pRAM, m_pPhysicalRAM, m_pUncachedRAM);
}

void DoState(PointerWrap &p)
{
	auto s = p.Section("Memory", 1, 2);
	if (!s)
		return;

	if (s < 2) {
		if (!g_RemasterMode)
			g_MemorySize = RAM_NORMAL_SIZE;
		g_PSPModel = PSP_MODEL_FAT;
	} else {
		u32 oldMemorySize = g_MemorySize;
		p.Do(g_PSPModel);
		p.DoMarker("PSPModel");
		if (!g_RemasterMode) {
			g_MemorySize = g_PSPModel == PSP_MODEL_FAT ? RAM_NORMAL_SIZE : RAM_DOUBLE_SIZE;
			if (oldMemorySize < g_MemorySize) {
				Shutdown();
				Init();
			}
		}
	}

	p.DoArray(GetPointer(PSP_GetKernelMemoryBase()), g_MemorySize);
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
	DEBUG_LOG(MEMMAP, "Memory system shut down.");
}

void Clear()
{
	if (m_pRAM)
		memset(GetPointerUnchecked(PSP_GetKernelMemoryBase()), 0, g_MemorySize);
	if (m_pScratchPad)
		memset(m_pScratchPad, 0, SCRATCHPAD_SIZE);
	if (m_pVRAM)
		memset(m_pVRAM, 0, VRAM_SIZE);
}

static Opcode Read_Instruction(u32 address, bool resolveReplacements, Opcode inst)
{
	if (!MIPS_IS_EMUHACK(inst.encoding)) {
		return inst;
	}

	if (MIPS_IS_RUNBLOCK(inst.encoding) && MIPSComp::jit) {
		JitBlockCache *bc = MIPSComp::jit->GetBlockCache();
		int block_num = bc->GetBlockNumberFromEmuHackOp(inst, true);
		if (block_num >= 0) {
			inst = bc->GetOriginalFirstOp(block_num);
			if (resolveReplacements && MIPS_IS_REPLACEMENT(inst)) {
				u32 op;
				if (GetReplacedOpAt(address, &op)) {
					if (MIPS_IS_EMUHACK(op)) {
						ERROR_LOG(HLE,"WTF 1");
						return Opcode(op);
					} else {
						return Opcode(op);
					}
				} else {
					ERROR_LOG(HLE, "Replacement, but no replacement op? %08x", inst.encoding);
				}
			}
			return inst;
		} else {
			return inst;
		}
	} else if (MIPS_IS_REPLACEMENT(inst.encoding)) {
		u32 op;
		if (GetReplacedOpAt(address, &op)) {
			if (MIPS_IS_EMUHACK(op)) {
				ERROR_LOG(HLE,"WTF 2");
				return Opcode(op);
			} else {
				return Opcode(op);
			}
		} else {
			return inst;
		}
	} else {
		return inst;
	}
}

Opcode Read_Instruction(u32 address, bool resolveReplacements)
{
	Opcode inst = Opcode(Read_U32(address));
	return Read_Instruction(address, resolveReplacements, inst);
}

Opcode ReadUnchecked_Instruction(u32 address, bool resolveReplacements)
{
	Opcode inst = Opcode(ReadUnchecked_U32(address));
	return Read_Instruction(address, resolveReplacements, inst);
}

Opcode Read_Opcode_JIT(u32 address)
{
	Opcode inst = Opcode(Read_U32(address));
	if (MIPS_IS_RUNBLOCK(inst.encoding) && MIPSComp::jit) {
		JitBlockCache *bc = MIPSComp::jit->GetBlockCache();
		int block_num = bc->GetBlockNumberFromEmuHackOp(inst, true);
		if (block_num >= 0) {
			return bc->GetOriginalFirstOp(block_num);
		} else {
			return inst;
		}
	} else {
		return inst;
	}
}

// WARNING! No checks!
// We assume that _Address is cached
void Write_Opcode_JIT(const u32 _Address, const Opcode _Value)
{
	Memory::WriteUnchecked_U32(_Value.encoding, _Address);
}

void Memset(const u32 _Address, const u8 _iValue, const u32 _iLength)
{
	u8 *ptr = GetPointer(_Address);
	if (ptr != NULL) {
		memset(ptr, _iValue, _iLength);
	}
	else
	{
		for (size_t i = 0; i < _iLength; i++)
			Write_U8(_iValue, (u32)(_Address + i));
	}
#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(_Address, true, _iLength, currentMIPS->pc);
#endif
}

const char *GetAddressName(u32 address)
{
	// TODO, follow GetPointer
	return "[mem]";
}

} // namespace
