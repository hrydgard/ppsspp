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
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HDRemaster.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MemFault.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Common/Thread/ParallelLoop.h"

namespace Memory {

// The base pointer to the auto-mirrored arena.
u8* base = nullptr;

// The MemArena class
MemArena g_arena;
// ==============

u8 *m_pPhysicalScratchPad;
u8 *m_pUncachedScratchPad;
// 64-bit: Pointers to high-mem mirrors
// 32-bit: Same as above
u8 *m_pPhysicalRAM[3];
u8 *m_pUncachedRAM[3];
u8 *m_pKernelRAM[3];	// RAM mirrored up to "kernel space". Fully accessible at all times currently.
// Technically starts at 0xA0000000, which we don't properly support (but we don't really support kernel code.)
// This matches how we handle 32-bit masking.
u8 *m_pUncachedKernelRAM[3];

// VRAM is mirrored 4 times.  The second and fourth mirrors are swizzled.
// In practice, a game accessing the mirrors most likely is deswizzling the depth buffer.
u8 *m_pPhysicalVRAM[4];
u8 *m_pUncachedVRAM[4];

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
	{&m_pPhysicalVRAM[0],     0x04000000, 0x00200000, 0},
	{&m_pPhysicalVRAM[1],     0x04200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalVRAM[2],     0x04400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalVRAM[3],     0x04600000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM[0],     0x44000000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM[1],     0x44200000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM[2],     0x44400000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pUncachedVRAM[3],     0x44600000, 0x00200000, MV_MIRROR_PREVIOUS},
	{&m_pPhysicalRAM[0],      0x08000000, g_MemorySize, MV_IS_PRIMARY_RAM},	// only from 0x08800000 is it usable (last 24 megs)
	{&m_pUncachedRAM[0],      0x48000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM},
	{&m_pKernelRAM[0],        0x88000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[0],0xC8000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM | MV_KERNEL},
	// Starts at memory + 31 MB.
	{&m_pPhysicalRAM[1],      0x09F00000, g_MemorySize, MV_IS_EXTRA1_RAM},
	{&m_pUncachedRAM[1],      0x49F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM},
	{&m_pKernelRAM[1],        0x89F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[1],0xC9F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	// Starts at memory + 31 * 2 MB.
	{&m_pPhysicalRAM[2],      0x0BE00000, g_MemorySize, MV_IS_EXTRA2_RAM},
	{&m_pUncachedRAM[2],      0x4BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM},
	{&m_pKernelRAM[2],        0x8BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[2],0xCBE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},

	// TODO: There are a few swizzled mirrors of VRAM, not sure about the best way to
	// implement those.
};

static const int num_views = sizeof(views) / sizeof(MemoryView);

inline static bool CanIgnoreView(const MemoryView &view) {
#ifdef MASKED_PSP_MEMORY
	// Basically, 32-bit platforms can ignore views that are masked out anyway.
	return (view.flags & MV_MIRROR_PREVIOUS) && (view.virtual_address & ~MEMVIEW32_MASK) != 0;
#else
	return false;
#endif
}

#if PPSSPP_PLATFORM(IOS) && PPSSPP_ARCH(64BIT)
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
			DEBUG_LOG(Log::MemMap, "Failed at view %d", i);
		}
#else
		if (CanIgnoreView(view)) {
			// This is handled by address masking in 32-bit, no view needs to be created.
			*view.out_ptr = *views[i - 1].out_ptr;
		} else {
			*view.out_ptr = (u8*)g_arena.CreateView(
				position, view.size, base + (view.virtual_address & MEMVIEW32_MASK));
			if (!*view.out_ptr) {
				DEBUG_LOG(Log::MemMap, "Failed at view %d", i);
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
				g_arena.ReleaseView(0, *views[j].out_ptr, views[j].size);
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
	if (!g_arena.GrabMemSpace(total_mem)) {
		// It'll already have logged.
		return false;
	}
#endif

#if !PPSSPP_PLATFORM(ANDROID)
	if (g_arena.NeedsProbing()) {
		int base_attempts = 0;
#if PPSSPP_PLATFORM(WINDOWS) && PPSSPP_ARCH(32BIT)
		// Try a whole range of possible bases. Return once we got a valid one.
		uintptr_t max_base_addr = 0x7FFF0000 - 0x10000000;
		uintptr_t min_base_addr = 0x01000000;
		uintptr_t stride = 0x400000;
#elif PPSSPP_ARCH(ARM64) && PPSSPP_PLATFORM(IOS)
		// iOS
		uintptr_t max_base_addr = 0x1FFFF0000ULL - 0x80000000ULL;
		uintptr_t min_base_addr = 0x100000000ULL;
		uintptr_t stride = 0x800000;
#else
		uintptr_t max_base_addr = 0;
		uintptr_t min_base_addr = 0;
		uintptr_t stride = 0;
		ERROR_LOG(Log::MemMap, "MemoryMap_Setup: Hit a wrong path, should not be needed on this platform.");
		return false;
#endif
		for (uintptr_t base_addr = min_base_addr; base_addr < max_base_addr; base_addr += stride) {
			base_attempts++;
			base = (u8 *)base_addr;
			if (Memory_TryBase(flags)) {
				INFO_LOG(Log::MemMap, "Found valid memory base at %p after %i tries.", base, base_attempts);
				return true;
			}
		}
		ERROR_LOG(Log::MemMap, "MemoryMap_Setup: Failed finding a memory base.");
		return false;
	}
	else
#endif
	{
#if !PPSSPP_PLATFORM(UWP)
		base = g_arena.Find4GBBase();
		if (!base) {
			return false;
		}
#endif
	}

	// Should return true...
	return Memory_TryBase(flags);
}

void MemoryMap_Shutdown(u32 flags) {
	size_t position = 0;
	size_t last_position = 0;

	for (int i = 0; i < num_views; i++) {
		if (views[i].size == 0)
			continue;
		SKIP(flags, views[i].flags);
        
		if (views[i].flags & MV_MIRROR_PREVIOUS) {
			position = last_position;
		}

		if (*views[i].out_ptr)
			g_arena.ReleaseView(position, *views[i].out_ptr, views[i].size);
		*views[i].out_ptr = nullptr;

		last_position = position;
		position += g_arena.roundup(views[i].size);
	}
	g_arena.ReleaseSpace();

#if PPSSPP_PLATFORM(UWP)
	VirtualFree(base, 0, MEM_RELEASE);
#endif
}

bool Init() {
	// On some 32 bit platforms (like Android, iOS, etc.), you can only map < 32 megs at a time.
	const static int MAX_MMAP_SIZE = 31 * 1024 * 1024;
	_dbg_assert_msg_(g_MemorySize <= MAX_MMAP_SIZE * 3, "ACK - too much memory for three mmap views.");
	for (size_t i = 0; i < ARRAY_SIZE(views); i++) {
		if (views[i].flags & MV_IS_PRIMARY_RAM)
			views[i].size = std::min((int)g_MemorySize, MAX_MMAP_SIZE);
		if (views[i].flags & MV_IS_EXTRA1_RAM)
			views[i].size = std::min(std::max((int)g_MemorySize - MAX_MMAP_SIZE, 0), MAX_MMAP_SIZE);
		if (views[i].flags & MV_IS_EXTRA2_RAM)
			views[i].size = std::min(std::max((int)g_MemorySize - MAX_MMAP_SIZE * 2, 0), MAX_MMAP_SIZE);
	}

	int flags = 0;
	if (!MemoryMap_Setup(flags)) {
		return false;
	}

	INFO_LOG(Log::MemMap, "Memory system initialized. Base at %p (RAM at @ %p, uncached @ %p)",
		base, m_pPhysicalRAM, m_pUncachedRAM);

	MemFault_Init();
	return true;
}

void Reinit() {
	_assert_msg_(PSP_IsInited(), "Cannot reinit during startup/shutdown");
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITING);
	Shutdown();
	Init();
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITED);
}

static void DoMemoryVoid(PointerWrap &p, uint32_t start, uint32_t size) {
	uint8_t *d = GetPointerWrite(start);
	uint8_t *&storage = *p.ptr;

	// We only handle aligned data and sizes.
	if ((size & 0x3F) != 0 || ((uintptr_t)d & 0x3F) != 0)
		return p.DoVoid(d, size);

	switch (p.mode) {
	case PointerWrap::MODE_READ:
		ParallelMemcpy(&g_threadManager, d, storage, size);
		break;
	case PointerWrap::MODE_WRITE:
		ParallelMemcpy(&g_threadManager, storage, d, size);
		break;
	case PointerWrap::MODE_MEASURE:
		// Nothing to do here.
		break;
	case PointerWrap::MODE_VERIFY:
		ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
			for (int i = l; i < h; i++)
				_dbg_assert_msg_(d[i] == storage[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", d[i], d[i], &d[i], storage[i], storage[i], &storage[i]);
		}, 0, size, 128);
		break;
	case PointerWrap::MODE_NOOP:
		break;
	}
	storage += size;
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
		Do(p, g_PSPModel);
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
		Do(p, g_PSPModel);
		p.DoMarker("PSPModel");
		Do(p, g_MemorySize);
		if (oldMemorySize != g_MemorySize) {
			Reinit();
		}
	}

	DoMemoryVoid(p, PSP_GetKernelMemoryBase(), g_MemorySize);
	p.DoMarker("RAM");

	DoMemoryVoid(p, PSP_GetVidMemBase(), VRAM_SIZE);
	p.DoMarker("VRAM");
	DoArray(p, m_pPhysicalScratchPad, SCRATCHPAD_SIZE);
	p.DoMarker("ScratchPad");
}

void Shutdown() {
	std::lock_guard<std::recursive_mutex> guard(g_shutdownLock);
	u32 flags = 0;
	MemoryMap_Shutdown(flags);
	base = nullptr;
	DEBUG_LOG(Log::MemMap, "Memory system shut down.");
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

	// No mutex on jit access here, but we assume the caller has locked, if necessary.
	if (MIPS_IS_RUNBLOCK(inst.encoding) && MIPSComp::jit) {
		inst = MIPSComp::jit->GetOriginalOp(inst);
		if (resolveReplacements && MIPS_IS_REPLACEMENT(inst)) {
			u32 op;
			if (GetReplacedOpAt(address, &op)) {
				if (MIPS_IS_EMUHACK(op)) {
					ERROR_LOG(Log::MemMap, "WTF 1");
					return Opcode(op);
				} else {
					return Opcode(op);
				}
			} else {
				ERROR_LOG(Log::MemMap, "Replacement, but no replacement op? %08x", inst.encoding);
			}
		}
		return inst;
	} else if (resolveReplacements && MIPS_IS_REPLACEMENT(inst.encoding)) {
		u32 op;
		if (GetReplacedOpAt(address, &op)) {
			if (MIPS_IS_EMUHACK(op)) {
				ERROR_LOG(Log::MemMap, "WTF 2");
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
	// No mutex around jit access here, but we assume caller has if necessary.
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

void Memset(const u32 _Address, const u8 _iValue, const u32 _iLength, const char *tag) {
	if (IsValidRange(_Address, _iLength)) {
		uint8_t *ptr = GetPointerWriteUnchecked(_Address);
		memset(ptr, _iValue, _iLength);
	} else {
		for (size_t i = 0; i < _iLength; i++)
			Write_U8(_iValue, (u32)(_Address + i));
	}

	NotifyMemInfo(MemBlockFlags::WRITE, _Address, _iLength, tag, strlen(tag));
}

} // namespace

void PSPPointerNotifyRW(int rw, uint32_t ptr, uint32_t bytes, const char * tag, size_t tagLen) {
	if (MemBlockInfoDetailed(bytes)) {
		if (rw & 1)
			NotifyMemInfo(MemBlockFlags::WRITE, ptr, bytes, tag, tagLen);
		if (rw & 2)
			NotifyMemInfo(MemBlockFlags::READ, ptr, bytes, tag, tagLen);
	}
}
