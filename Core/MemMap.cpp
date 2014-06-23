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
#ifndef __SYMBIAN32__
#include "Common/MemArena.h"
#endif
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
u8* base = NULL;

#ifdef __SYMBIAN32__
RChunk* memmap;
#else
// The MemArena class
MemArena g_arena;
#endif
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

// VRAM is mirrored 4 times.  The second and fourth mirrors are swizzled.
// In practice, a game accessing the mirrors most likely is deswizzling the depth buffer.
u8 *m_pPhysicalVRAM1;
u8 *m_pPhysicalVRAM2;
u8 *m_pPhysicalVRAM3;
u8 *m_pPhysicalVRAM4;
u8 *m_pUncachedVRAM1;
u8 *m_pUncachedVRAM2;
u8 *m_pUncachedVRAM3;
u8 *m_pUncachedVRAM4;

// Holds the ending address of the PSP's user space.
// Required for HD Remasters to work properly.
// This replaces RAM_NORMAL_SIZE at runtime.
u32 g_MemorySize;
// Used to store the PSP model on game startup.
u32 g_PSPModel;

// We don't declare the IO region in here since its handled by other means.
static MemoryView views[] =
{
	{&m_pScratchPad, &m_pPhysicalScratchPad,  0x00010000, SCRATCHPAD_SIZE, 0},
	{NULL,           &m_pUncachedScratchPad,  0x40010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS},
	{&m_pVRAM,       &m_pPhysicalVRAM1,       0x04000000, 0x00200000, 0},
	{NULL,           &m_pPhysicalVRAM2,       0x04200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pPhysicalVRAM3,       0x04400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pPhysicalVRAM4,       0x04600000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pUncachedVRAM1,       0x44000000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pUncachedVRAM2,       0x44200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pUncachedVRAM3,       0x44400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{NULL,           &m_pUncachedVRAM4,       0x44600000, 0x00200000, MV_MIRROR_PREVIOUS},
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

inline static bool CanIgnoreView(const MemoryView &view) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined(_XBOX)
	// Basically, 32-bit platforms can ignore views that are masked out anyway.
	return (view.flags & MV_MIRROR_PREVIOUS) && (view.virtual_address & ~MEMVIEW32_MASK) != 0;
#else
	return false;
#endif
}

// yeah, this could also be done in like two bitwise ops...
#define SKIP(a_flags, b_flags) 
//	if (!(a_flags & MV_WII_ONLY) && (b_flags & MV_WII_ONLY)) 
//		continue; 
//	if (!(a_flags & MV_FAKE_VMEM) && (b_flags & MV_FAKE_VMEM)) 
//		continue; 

static bool Memory_TryBase(u32 flags) {
	// OK, we know where to find free space. Now grab it!
	// We just mimic the popular BAT setup.

#if defined(_XBOX)
	void *ptr;
#elif !defined(__SYMBIAN32__)
	size_t position = 0;
	size_t last_position = 0;
#endif

	// Zero all the pointers to be sure.
	for (int i = 0; i < num_views; i++)
	{
		if (views[i].out_ptr_low)
			*views[i].out_ptr_low = 0;
		if (views[i].out_ptr)
			*views[i].out_ptr = 0;
	}

	int i;
	for (i = 0; i < num_views; i++)
	{
		const MemoryView &view = views[i];
		if (view.size == 0)
			continue;
		SKIP(flags, view.flags);
		
#ifdef __SYMBIAN32__
		if (!CanIgnoreView(view)) {
			*(view.out_ptr_low) = (u8*)(base + view.virtual_address);
			memmap->Commit(view.virtual_address & MEMVIEW32_MASK, view.size);
		}
		*(view.out_ptr) = (u8*)base + (view.virtual_address & MEMVIEW32_MASK);
#elif defined(_XBOX)
		if (!CanIgnoreView(view)) {
			*(view.out_ptr_low) = (u8*)(base + view.virtual_address);
			ptr = VirtualAlloc(base + (view.virtual_address & MEMVIEW32_MASK), view.size, MEM_COMMIT, PAGE_READWRITE);
		}
		*(view.out_ptr) = (u8*)base + (view.virtual_address & MEMVIEW32_MASK);
#else
		if (view.flags & MV_MIRROR_PREVIOUS) {
			position = last_position;
		} else {
			*(view.out_ptr_low) = (u8*)g_arena.CreateView(position, view.size);
			if (!*view.out_ptr_low)
				goto bail;
		}
#ifdef _M_X64
		*view.out_ptr = (u8*)g_arena.CreateView(
			position, view.size, base + view.virtual_address);
#else
		if (CanIgnoreView(view)) {
			// No need to create multiple identical views.
			*view.out_ptr = *views[i - 1].out_ptr;
		} else {
			*view.out_ptr = (u8*)g_arena.CreateView(
				position, view.size, base + (view.virtual_address & MEMVIEW32_MASK));
			if (!*view.out_ptr)
				goto bail;
		}
#endif
		last_position = position;
		position += g_arena.roundup(view.size);
#endif
	}

	return true;

#if !defined(_XBOX) && !defined(__SYMBIAN32__)
bail:
	// Argh! ERROR! Free what we grabbed so far so we can try again.
	for (int j = 0; j <= i; j++)
	{
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (views[j].out_ptr_low && *views[j].out_ptr_low)
		{
			g_arena.ReleaseView(*views[j].out_ptr_low, views[j].size);
			*views[j].out_ptr_low = NULL;
		}
		if (*views[j].out_ptr)
		{
			if (!CanIgnoreView(views[j])) {
				g_arena.ReleaseView(*views[j].out_ptr, views[j].size);
			}
			*views[j].out_ptr = NULL;
		}
	}
	return false;
#endif
}

void MemoryMap_Setup(u32 flags)
{
	// Find a base to reserve 256MB
#if defined(_XBOX)
	base = (u8*)VirtualAlloc(0, 0x10000000, MEM_RESERVE|MEM_LARGE_PAGES, PAGE_READWRITE);
#elif defined(__SYMBIAN32__)
	memmap = new RChunk();
	memmap->CreateDisconnectedLocal(0 , 0, 0x10000000);
	base = memmap->Base();
#else
	size_t total_mem = 0;

	for (int i = 0; i < num_views; i++)
	{
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (!CanIgnoreView(views[i]))
			total_mem += g_arena.roundup(views[i].size);
	}
	// Grab some pagefile backed memory out of the void ...
	g_arena.GrabLowMemSpace(total_mem);
	// 32-bit Windows retrieves base a different way
#if defined(_M_X64) || !defined(_WIN32)
	// This really shouldn't fail - in 64-bit, there will always be enough address space.
	// Linux32 is fine with the x64 method, although limited to 32-bit with no automirrors.
	base = MemArena::Find4GBBase();
#endif
#endif


	// Now, create views in high memory where there's plenty of space.
#if defined(_WIN32) && !defined(_M_X64) && !defined(_XBOX)
	// Try a whole range of possible bases. Return once we got a valid one.
	int base_attempts = 0;
	u32 max_base_addr = 0x7FFF0000 - 0x10000000;

	for (u32 base_addr = 0x01000000; base_addr < max_base_addr; base_addr += 0x400000)
	{
		base_attempts++;
		base = (u8 *)base_addr;
		if (Memory_TryBase(flags)) 
		{
			INFO_LOG(MEMMAP, "Found valid memory base at %p after %i tries.", base, base_attempts);
			base_attempts = 0;
			break;
		}
	}

	if (base_attempts)
		PanicAlert("No possible memory base pointer found!");
#else
	// Try base we retrieved earlier
	if (!Memory_TryBase(flags))
	{
		ERROR_LOG(MEMMAP, "MemoryMap_Setup: Failed finding a memory base.");
		PanicAlert("MemoryMap_Setup: Failed finding a memory base.");
	}
#endif
	return;
}

void MemoryMap_Shutdown(u32 flags)
{
#ifdef __SYMBIAN32__
	memmap->Decommit(0, memmap->MaxSize());
	memmap->Close();
	delete memmap;
#else
	for (int i = 0; i < num_views; i++)
	{
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (views[i].out_ptr_low && *views[i].out_ptr_low)
			g_arena.ReleaseView(*views[i].out_ptr_low, views[i].size);
		if (*views[i].out_ptr && (!views[i].out_ptr_low || *views[i].out_ptr != *views[i].out_ptr_low))
			g_arena.ReleaseView(*views[i].out_ptr, views[i].size);
		*views[i].out_ptr = NULL;
		if (views[i].out_ptr_low)
			*views[i].out_ptr_low = NULL;
	}
	g_arena.ReleaseSpace();
#endif
}

void Init()
{
	int flags = 0;

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
	MemoryMap_Setup(flags);

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

	MemoryMap_Shutdown(flags);
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
	} else if (resolveReplacements && MIPS_IS_REPLACEMENT(inst.encoding)) {
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
