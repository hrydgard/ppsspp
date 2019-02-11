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

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(UWP)
#include "Common/CommonWindows.h"
#endif

#include <algorithm>
#include <mutex>

#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "Common/MemArena.h"
#include "Common/ChunkFile.h"
#include "Common/MachineContext.h"

#include "Core/MemMap.h"
#include "Core/HDRemaster.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/HLE.h"

#include "Core/Core.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

namespace Memory {

// The base pointer to the auto-mirrored arena.
u8* base = NULL;

// The MemArena class
MemArena g_arena;
// ==============

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

std::recursive_mutex g_shutdownLock;

// We don't declare the IO region in here since its handled by other means.
static MemoryView views[] =
{
	{&m_pPhysicalScratchPad,  0x00010000, SCRATCHPAD_SIZE, 0},
	{&m_pUncachedScratchPad,  0x40010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalVRAM1,       0x04000000, 0x00200000, 0},
	{&m_pPhysicalVRAM2,       0x04200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalVRAM3,       0x04400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalVRAM4,       0x04600000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM1,       0x44000000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM2,       0x44200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM3,       0x44400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM4,       0x44600000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalRAM,         0x08000000, g_MemorySize, MV_IS_PRIMARY_RAM},	// only from 0x08800000 is it usable (last 24 megs)
	{&m_pUncachedRAM,         0x48000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM},
	{&m_pKernelRAM,           0x88000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM | MV_KERNEL},
	// Starts at memory + 31 MB.
	{&m_pPhysicalRAM2,        0x09F00000, g_MemorySize, MV_IS_EXTRA1_RAM},
	{&m_pUncachedRAM2,        0x49F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM},
	{&m_pKernelRAM2,          0x89F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	// Starts at memory + 31 * 2 MB.
	{&m_pPhysicalRAM3,        0x0BE00000, g_MemorySize, MV_IS_EXTRA2_RAM},
	{&m_pUncachedRAM3,        0x4BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM},
	{&m_pKernelRAM3,          0x8BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},

	// TODO: There are a few swizzled mirrors of VRAM, not sure about the best way to
	// implement those.
};

static const int num_views = sizeof(views) / sizeof(MemoryView);

inline static bool CanIgnoreView(const MemoryView &view) {
#if PPSSPP_ARCH(32BIT)
	// Basically, 32-bit platforms can ignore views that are masked out anyway.
	return (view.flags & MV_MIRROR_PREVIOUS) && (view.virtual_address & ~MEMVIEW32_MASK) != 0;
#else
	return false;
#endif
}

#if defined(IOS) && PPSSPP_ARCH(64BIT)
#define SKIP(a_flags, b_flags) \
	if ((b_flags) & MV_KERNEL) \
		continue;
#else
#define SKIP(a_flags, b_flags) \
	;
#endif

static bool Memory_TryBase(u32 flags) {
	// OK, we know where to find free space. Now grab it!
	// We just mimic the popular BAT setup.

	size_t position = 0;
	size_t last_position = 0;

	// Zero all the pointers to be sure.
	for (int i = 0; i < num_views; i++) {
		if (views[i].out_ptr)
			*views[i].out_ptr = 0;
	}

	int i;
	for (i = 0; i < num_views; i++) {
		const MemoryView &view = views[i];
		if (view.size == 0)
			continue;
		SKIP(flags, view.flags);
		
		if (view.flags & MV_MIRROR_PREVIOUS) {
			position = last_position;
		}
#ifndef MASKED_PSP_MEMORY
		*view.out_ptr = (u8*)g_arena.CreateView(
			position, view.size, base + view.virtual_address);
		if (!*view.out_ptr) {
			goto bail;
			DEBUG_LOG(MEMMAP, "Failed at view %d", i);
		}
#else
		if (CanIgnoreView(view)) {
			// This is handled by address masking in 32-bit, no view needs to be created.
			*view.out_ptr = *views[i - 1].out_ptr;
		} else {
			*view.out_ptr = (u8*)g_arena.CreateView(
				position, view.size, base + (view.virtual_address & MEMVIEW32_MASK));
			if (!*view.out_ptr) {
				DEBUG_LOG(MEMMAP, "Failed at view %d", i);
				goto bail;
			}
		}
#endif
		last_position = position;
		position += g_arena.roundup(view.size);
	}

	return true;
bail:
	// Argh! ERROR! Free what we grabbed so far so we can try again.
	for (int j = 0; j <= i; j++) {
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (*views[j].out_ptr) {
			if (!CanIgnoreView(views[j])) {
				g_arena.ReleaseView(*views[j].out_ptr, views[j].size);
			}
			*views[j].out_ptr = NULL;
		}
	}
	return false;
}

bool MemoryMap_Setup(u32 flags) {
#if PPSSPP_PLATFORM(UWP)
	// We reserve the memory, then simply commit in TryBase.
	base = (u8*)VirtualAllocFromApp(0, 0x10000000, MEM_RESERVE, PAGE_READWRITE);
#else

	// Figure out how much memory we need to allocate in total.
	size_t total_mem = 0;
	for (int i = 0; i < num_views; i++) {
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (!CanIgnoreView(views[i]))
			total_mem += g_arena.roundup(views[i].size);
	}

	// Grab some pagefile backed memory out of the void ...
	g_arena.GrabLowMemSpace(total_mem);
#endif

#if !PPSSPP_PLATFORM(ANDROID)
	if (g_arena.NeedsProbing()) {
		int base_attempts = 0;
#if defined(_WIN32) && PPSSPP_ARCH(32BIT)
		// Try a whole range of possible bases. Return once we got a valid one.
		uintptr_t max_base_addr = 0x7FFF0000 - 0x10000000;
		uintptr_t min_base_addr = 0x01000000;
		uintptr_t stride = 0x400000;
#else
		// iOS
		uintptr_t max_base_addr = 0x1FFFF0000ULL - 0x80000000ULL;
		uintptr_t min_base_addr = 0x100000000ULL;
		uintptr_t stride = 0x800000;
#endif
		for (uintptr_t base_addr = min_base_addr; base_addr < max_base_addr; base_addr += stride) {
			base_attempts++;
			base = (u8 *)base_addr;
			if (Memory_TryBase(flags)) {
				INFO_LOG(MEMMAP, "Found valid memory base at %p after %i tries.", base, base_attempts);
				return true;
			}
		}
		ERROR_LOG(MEMMAP, "MemoryMap_Setup: Failed finding a memory base.");
		PanicAlert("MemoryMap_Setup: Failed finding a memory base.");
		return false;
	}
	else
#endif
	{
#if !PPSSPP_PLATFORM(UWP)
		base = g_arena.Find4GBBase();
#endif
	}

	// Should return true...
	return Memory_TryBase(flags);
}

void MemoryMap_Shutdown(u32 flags) {
	for (int i = 0; i < num_views; i++) {
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
		if (*views[i].out_ptr)
			g_arena.ReleaseView(*views[i].out_ptr, views[i].size);
		*views[i].out_ptr = nullptr;
	}
	g_arena.ReleaseSpace();

#if PPSSPP_PLATFORM(UWP)
	VirtualFree(base, 0, MEM_RELEASE);
#endif
}

void Init() {
	// On some 32 bit platforms, you can only map < 32 megs at a time.
	// TODO: Wait, wtf? What platforms are those? This seems bad.
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
	int flags = 0;
	MemoryMap_Setup(flags);

	INFO_LOG(MEMMAP, "Memory system initialized. Base at %p (RAM at @ %p, uncached @ %p)",
		base, m_pPhysicalRAM, m_pUncachedRAM);
}

void Reinit() {
	_assert_msg_(SYSTEM, PSP_IsInited(), "Cannot reinit during startup/shutdown");
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITING);
	Shutdown();
	Init();
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITED);
}

void DoState(PointerWrap &p) {
	auto s = p.Section("Memory", 1, 3);
	if (!s)
		return;

	if (s < 2) {
		if (!g_RemasterMode)
			g_MemorySize = RAM_NORMAL_SIZE;
		g_PSPModel = PSP_MODEL_FAT;
	} else if (s == 2) {
		// In version 2, we determine memory size based on PSP model.
		u32 oldMemorySize = g_MemorySize;
		p.Do(g_PSPModel);
		p.DoMarker("PSPModel");
		if (!g_RemasterMode) {
			g_MemorySize = g_PSPModel == PSP_MODEL_FAT ? RAM_NORMAL_SIZE : RAM_DOUBLE_SIZE;
			if (oldMemorySize < g_MemorySize) {
				Reinit();
			}
		}
	} else {
		// In version 3, we started just saving the memory size directly.
		// It's no longer based strictly on the PSP model.
		u32 oldMemorySize = g_MemorySize;
		p.Do(g_PSPModel);
		p.DoMarker("PSPModel");
		p.Do(g_MemorySize);
		if (oldMemorySize != g_MemorySize) {
			Reinit();
		}
	}

	p.DoArray(GetPointer(PSP_GetKernelMemoryBase()), g_MemorySize);
	p.DoMarker("RAM");

	p.DoArray(m_pPhysicalVRAM1, VRAM_SIZE);
	p.DoMarker("VRAM");
	p.DoArray(m_pPhysicalScratchPad, SCRATCHPAD_SIZE);
	p.DoMarker("ScratchPad");
}

void Shutdown() {
	std::lock_guard<std::recursive_mutex> guard(g_shutdownLock);
	u32 flags = 0;
	MemoryMap_Shutdown(flags);
	base = nullptr;
	DEBUG_LOG(MEMMAP, "Memory system shut down.");
}

void Clear() {
	if (m_pPhysicalRAM)
		memset(GetPointerUnchecked(PSP_GetKernelMemoryBase()), 0, g_MemorySize);
	if (m_pPhysicalScratchPad)
		memset(m_pPhysicalScratchPad, 0, SCRATCHPAD_SIZE);
	if (m_pPhysicalVRAM1)
		memset(m_pPhysicalVRAM1, 0, VRAM_SIZE);
}

bool IsActive() {
	return base != nullptr;
}

// Wanting to avoid include pollution, MemMap.h is included a lot.
MemoryInitedLock::MemoryInitedLock()
{
	g_shutdownLock.lock();
}
MemoryInitedLock::~MemoryInitedLock()
{
	g_shutdownLock.unlock();
}

MemoryInitedLock Lock()
{
	return MemoryInitedLock();
}

__forceinline static Opcode Read_Instruction(u32 address, bool resolveReplacements, Opcode inst)
{
	if (!MIPS_IS_EMUHACK(inst.encoding)) {
		return inst;
	}

	if (MIPS_IS_RUNBLOCK(inst.encoding) && MIPSComp::jit) {
		inst = MIPSComp::jit->GetOriginalOp(inst);
		if (resolveReplacements && MIPS_IS_REPLACEMENT(inst)) {
			u32 op;
			if (GetReplacedOpAt(address, &op)) {
				if (MIPS_IS_EMUHACK(op)) {
					ERROR_LOG(MEMMAP, "WTF 1");
					return Opcode(op);
				} else {
					return Opcode(op);
				}
			} else {
				ERROR_LOG(MEMMAP, "Replacement, but no replacement op? %08x", inst.encoding);
			}
		}
		return inst;
	} else if (resolveReplacements && MIPS_IS_REPLACEMENT(inst.encoding)) {
		u32 op;
		if (GetReplacedOpAt(address, &op)) {
			if (MIPS_IS_EMUHACK(op)) {
				ERROR_LOG(MEMMAP, "WTF 2");
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
		return MIPSComp::jit->GetOriginalOp(inst);
	} else {
		return inst;
	}
}

// WARNING! No checks!
// We assume that _Address is cached
void Write_Opcode_JIT(const u32 _Address, const Opcode& _Value)
{
	Memory::WriteUnchecked_U32(_Value.encoding, _Address);
}

void Memset(const u32 _Address, const u8 _iValue, const u32 _iLength) {
	if (IsValidRange(_Address, _iLength)) {
		uint8_t *ptr = GetPointerUnchecked(_Address);
		memset(ptr, _iValue, _iLength);
	} else {
		for (size_t i = 0; i < _iLength; i++)
			Write_U8(_iValue, (u32)(_Address + i));
	}

	CBreakPoints::ExecMemCheck(_Address, true, _iLength, currentMIPS->pc);
}

bool HandleFault(uintptr_t hostAddress, void *ctx) {
	SContext *context = (SContext *)ctx;
	const uint8_t *codePtr = (uint8_t *)(context->CTX_PC);

	// TODO: Check that codePtr is within the current JIT space.
	// bool inJitSpace = MIPSComp::jit->IsInSpace(codePtr);
	// if (!inJitSpace) return false;

	// TODO: Disassemble at codePtr to figure out if it's a read or a write.

	uintptr_t baseAddress = (uintptr_t)base;

#ifdef MASKED_PSP_MEMORY
	const uintptr_t addressSpaceSize = 0x100000000ULL;
#else
	const uintptr_t addressSpaceSize = 0x40000000ULL;
#endif

	// Check whether hostAddress is within the PSP memory space, which (likely) means it was a game that did the bad access.
	if (hostAddress >= baseAddress && hostAddress <= baseAddress + addressSpaceSize) {
		uint32_t guestAddress = hostAddress - baseAddress;
		// Maybe we should also somehow check whether the JIT is on the stack.
		ERROR_LOG(SYSTEM, "Bad memory access detected and ignored: %08x (%p)", guestAddress, hostAddress);
		return true;
	}

	// A regular crash of some sort. Pass it on.
	return false;
}

} // namespace
