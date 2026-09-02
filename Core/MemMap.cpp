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
#include <map>
#include <mutex>

#include "Common/CommonTypes.h"
#include "Common/MemArena.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"

#include "Core/System.h"
#include "Core/Core.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HDRemaster.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MemFault.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Common/Thread/ParallelLoop.h"

namespace Memory {

// The base pointer to the auto-mirrored arena.
u8* base = nullptr;

// Sparse backing for invalid accesses configured to continue. This preserves
// unchecked low-address writes without reserving Jpcsp's full flat allocation.
static std::map<u32, u32> ignoredMemoryWords;

static constexpr u32 NormalizeIgnoredAddress(u32 address) {
	return address & 0x1FFFFFFF;
}

u8 ReadIgnored_U8(u32 address) {
	address = NormalizeIgnoredAddress(address);
	auto it = ignoredMemoryWords.find(address & ~3U);
	return it == ignoredMemoryWords.end() ? 0 : (u8)(it->second >> ((address & 3) * 8));
}

u16 ReadIgnored_U16(u32 address) {
	address = NormalizeIgnoredAddress(address) & ~1U;
	return (u16)(ReadIgnored_U8(address) | (ReadIgnored_U8(address + 1) << 8));
}

u32 ReadIgnored_U32(u32 address) {
	address = NormalizeIgnoredAddress(address) & ~3U;
	auto it = ignoredMemoryWords.find(address);
	return it == ignoredMemoryWords.end() ? 0 : it->second;
}

u64 ReadIgnored_U64(u32 address) {
	address = NormalizeIgnoredAddress(address) & ~3U;
	return (u64)ReadIgnored_U32(address) | ((u64)ReadIgnored_U32(address + 4) << 32);
}

void WriteIgnored_U8(u8 value, u32 address) {
	address = NormalizeIgnoredAddress(address);
	u32 &word = ignoredMemoryWords[address & ~3U];
	const int shift = (address & 3) * 8;
	word = (word & ~(0xFFU << shift)) | ((u32)value << shift);
}

void WriteIgnored_U16(u16 value, u32 address) {
	address = NormalizeIgnoredAddress(address) & ~1U;
	WriteIgnored_U8((u8)value, address);
	WriteIgnored_U8((u8)(value >> 8), address + 1);
}

void WriteIgnored_U32(u32 value, u32 address) {
	address = NormalizeIgnoredAddress(address) & ~3U;
	ignoredMemoryWords[address] = value;
}

void WriteIgnored_U64(u64 value, u32 address) {
	address = NormalizeIgnoredAddress(address) & ~3U;
	WriteIgnored_U32((u32)value, address);
	WriteIgnored_U32((u32)(value >> 32), address + 4);
}

// The MemArena class
MemArena g_arena;
// ==============

u8 *m_pNullPage;
u8 *m_pPhysicalScratchPad;
u8 *m_pUncachedScratchPad;
u8 *m_pKernelScratchPad;
u8 *m_pKseg1ScratchPad;
u8 *m_pUncachedKernelScratchPad;
// 64-bit: Pointers to high-mem mirrors
// 32-bit: Same as above
u8 *m_pPhysicalRAM[3];
u8 *m_pUncachedRAM[3];
u8 *m_pKernelRAM[3];	// RAM mirrored up to "kernel space". Fully accessible at all times currently.
// PSP KSEG1 (0xA0000000-0xBFFFFFFF) is the real uncached kernel alias used by
// firmware modules.  Keep PPSSPP's historical 0xC0000000 mirror as well for
// compatibility with existing HLE/JIT assumptions.
u8 *m_pKseg1RAM[3];
u8 *m_pUncachedKernelRAM[3];

// VRAM is mirrored 4 times.  The second and fourth mirrors are swizzled, so actually it's not correct
// to mirror them like we do here unfortunately.
// In practice, a game accessing the mirrors most likely is deswizzling the depth buffer, and things mostly work out
// since when we write to the depth buffer (like with the depth rasterizer or the software renderer)
// we write unswizzled data anyway. There are some exceptions, Silent Hill abuses the swizzling in ways that break
// our software renderer.
static u8 *m_pPhysicalVRAM[4];
static u8 *m_pUncachedVRAM[4];

// Hardware registers are mapped at 0xbc000000 and forwards (physical address 0x1C000000, but we need
// the 0x80000000 kernel flag and 0x40000000 uncached flag for these). Exception vectors are at 0xbfc00000.
// Since we do HLE emulation, we currently don't bother with any of that.
// Some limited documentation here: https://www.psdevwiki.com/psp/Hardware_Registers#Introduction

// Holds the ending address of the PSP's user space.
// Required for HD Remasters to work properly.
// This replaces RAM_NORMAL_SIZE at runtime.
u32 g_MemorySize;
// Used to store the PSP model on game startup.
u32 g_PSPModel;

static MemMapSetupFlags g_setupFlags;


// We don't declare the IO region in here since its handled by other means.
static MemoryView views[] = {
	{&m_pNullPage,            0x00000000, 0x00010000, MV_NULL_PAGE}, // Null page, usually not enabled. Only used for working around some race condition bugs.
	{&m_pPhysicalScratchPad,  0x00010000, SCRATCHPAD_SIZE, 0},
	{&m_pUncachedScratchPad,  0x40010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS},
	// Kernel-mode firmware uses KSEG0 (cached) and KSEG1 (uncached).  The C001
	// mirror is retained because PPSSPP historically treated the high bits as
	// independently combinable flags.
	{&m_pKernelScratchPad,        0x80010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS | MV_KERNEL},
	{&m_pKseg1ScratchPad,         0xA0010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS | MV_KERNEL},
	{&m_pUncachedKernelScratchPad,0xC0010000, SCRATCHPAD_SIZE, MV_MIRROR_PREVIOUS | MV_KERNEL},
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
	{&m_pKseg1RAM[0],         0xA8000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[0],0xC8000000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_PRIMARY_RAM | MV_KERNEL},
	// Starts at memory + 31 MB.
	{&m_pPhysicalRAM[1],      0x09F00000, g_MemorySize, MV_IS_EXTRA1_RAM},
	{&m_pUncachedRAM[1],      0x49F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM},
	{&m_pKernelRAM[1],        0x89F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	{&m_pKseg1RAM[1],         0xA9F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[1],0xC9F00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA1_RAM | MV_KERNEL},
	// Starts at memory + 31 * 2 MB.
	{&m_pPhysicalRAM[2],      0x0BE00000, g_MemorySize, MV_IS_EXTRA2_RAM},
	{&m_pUncachedRAM[2],      0x4BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM},
	{&m_pKernelRAM[2],        0x8BE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},
	{&m_pKseg1RAM[2],         0xABE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},
	{&m_pUncachedKernelRAM[2],0xCBE00000, g_MemorySize, MV_MIRROR_PREVIOUS | MV_IS_EXTRA2_RAM | MV_KERNEL},

	// TODO: There are a few swizzled mirrors of VRAM, not sure about the best way to
	// implement those.
};

inline static bool CanIgnoreView(const MemoryView &view) {
#ifdef MASKED_PSP_MEMORY
	// Basically, 32-bit platforms can ignore views that are masked out anyway.
	return (view.flags & MV_MIRROR_PREVIOUS) && (view.virtual_address & ~MEMVIEW32_MASK) != 0;
#else
	return false;
#endif
}

static bool SkipView(MemMapSetupFlags flags, u32 viewFlags) {
#if PPSSPP_PLATFORM(IOS) && PPSSPP_ARCH(64BIT)
	// We use a limited memory map on iOS with masking, we don't need to allocate the kernel space views.
	if (viewFlags & MV_KERNEL) {
		return true;
	}
#endif
	if ((viewFlags & MV_NULL_PAGE) && !(flags & MemMapSetupFlags::AllocNullPage))
		return true;
	return false;
}

static bool Memory_TryBase(MemMapSetupFlags flags) {
	// OK, we know where to find free space. Now grab it!
	// We just mimic the popular BAT setup.

	size_t position = 0;
	size_t last_position = 0;

	// Zero all the pointers to be sure.
	for (int i = 0; i < ARRAY_SIZE(views); i++) {
		if (views[i].out_ptr) {
			*views[i].out_ptr = 0;
		}
	}

	int i;  // the final value of i is used in the next loop
	for (i = 0; i < ARRAY_SIZE(views); i++) {
		const MemoryView &view = views[i];
		if (view.size == 0)
			continue;
		if (SkipView(flags, view.flags))
			continue;
		
		if (view.flags & MV_MIRROR_PREVIOUS) {
			position = last_position;
		}
#ifndef MASKED_PSP_MEMORY
		*view.out_ptr = (u8*)g_arena.CreateView(
			position, view.size, base + view.virtual_address);
		if (!*view.out_ptr) {
			ERROR_LOG(Log::MemMap, "Failed at view %d", i);
			goto bail;
		}
#else
		if (CanIgnoreView(view)) {
			// This is handled by address masking in 32-bit, no view needs to be created.
			*view.out_ptr = *views[i - 1].out_ptr;
		} else {
			*view.out_ptr = (u8*)g_arena.CreateView(
				position, view.size, base + (view.virtual_address & MEMVIEW32_MASK));
			if (!*view.out_ptr) {
				ERROR_LOG(Log::MemMap, "Failed at view %d", i);
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
		if (SkipView(flags, views[i].flags))
			continue;
		if (views[j].out_ptr && *views[j].out_ptr) {
			if (!CanIgnoreView(views[j])) {
				g_arena.ReleaseView(0, *views[j].out_ptr, views[j].size);
			}
			*views[j].out_ptr = nullptr;
		}
	}
	return false;
}

bool MemoryMap_Setup(MemMapSetupFlags flags) {
	g_setupFlags = flags;
#if PPSSPP_PLATFORM(UWP)
	// We reserve the memory, then simply commit in TryBase.
	base = (u8*)VirtualAllocFromApp(0, 0x10000000, MEM_RESERVE, PAGE_READWRITE);
#else

	// Figure out how much memory we need to allocate in total.
	size_t total_mem = 0;
	for (int i = 0; i < ARRAY_SIZE(views); i++) {
		if (views[i].size == 0)
			continue;
		if (SkipView(flags, views[i].flags))
			continue;
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

void MemoryMap_Shutdown() {
	size_t position = 0;
	size_t last_position = 0;
	const MemMapSetupFlags flags = g_setupFlags;
	g_setupFlags = MemMapSetupFlags::Default;

	for (int i = 0; i < ARRAY_SIZE(views); i++) {
		if (views[i].size == 0)
			continue;
		if (SkipView(flags, views[i].flags))
			continue;

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

bool Init(MemMapSetupFlags flags) {
	ignoredMemoryWords.clear();
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

	if (!MemoryMap_Setup(flags)) {
		return false;
	}

	INFO_LOG(Log::MemMap, "Memory system initialized. Base at %p (RAM at @ %p, uncached @ %p)",
		base, m_pPhysicalRAM, m_pUncachedRAM);

	MemFault_Init();
	return true;
}

void Reinit() {
	_assert_msg_(PSP_GetBootState() == BootState::Complete, "Cannot reinit during startup/shutdown");
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITING);
	// Held across both halves: between Shutdown() and Init() there is no memory map at all, and a
	// reader that only saw Shutdown()'s own acquire could slip into that gap.
	CoreShutdownLock coreLock = Core_LockAgainstShutdown();
	MemMapSetupFlags flags = g_setupFlags;
	Shutdown();
	Init(flags);
	Core_NotifyLifecycle(CoreLifecycle::MEMORY_REINITED);
}

static void DoMemoryVoid(PointerWrap &p, uint32_t start, uint32_t size) {
	uint8_t *d = GetPointerWriteOrException(start);
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
	auto s = p.Section("Memory", 1, 4);
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
	if (s >= 4) {
		Do(p, ignoredMemoryWords);
	} else if (p.mode == p.MODE_READ) {
		ignoredMemoryWords.clear();
	}
}

void Shutdown() {
	CoreShutdownLock coreLock = Core_LockAgainstShutdown();
	u32 flags = 0;
	MemoryMap_Shutdown();
	ignoredMemoryWords.clear();
	base = nullptr;
	DEBUG_LOG(Log::MemMap, "Memory system shut down.");
}

bool IsActive() {
	return base != nullptr;
}

static Opcode Read_Instruction(u32 address, bool resolveReplacements, Opcode inst) {
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

Opcode Read_Instruction(u32 address, bool resolveReplacements) {
	if (!IsValid4AlignedAddress(address)) {
		// BAD!
		_dbg_assert_(false);
		return Opcode(0);
	}

	Opcode inst = Opcode(ReadUnchecked_U32(address));
	return Read_Instruction(address, resolveReplacements, inst);
}

Opcode ReadUnchecked_Instruction(u32 address, bool resolveReplacements) {
	_dbg_assert_((address & 3) == 0);
	Opcode inst = Opcode(ReadUnchecked_U32(address));
	return Read_Instruction(address, resolveReplacements, inst);
}

// WARNING! Caller checks that address is valid!
Opcode Read_Opcode_JIT(u32 address) {
	_dbg_assert_(Memory::IsValid4AlignedAddress(address));
	Opcode inst = Opcode(ReadUnchecked_U32(address));
	// No mutex around jit access here, but we assume caller has if necessary.
	if (MIPS_IS_RUNBLOCK(inst.encoding) && MIPSComp::jit) {
		return MIPSComp::jit->GetOriginalOp(inst);
	} else {
		return inst;
	}
}

// WARNING! No checks!
void Write_Opcode_JIT(const u32 address, const Opcode& _Value) {
	_dbg_assert_((address & 3) == 0);
	Memory::WriteUnchecked_U32(_Value.encoding, address);
}

void Memset(const u32 addr, const u8 value, const u32 size, const char *tag) {
	if (size == 0) {
		// We ignore invalid addresses etc if the length is zero.
		return;
	}

	if (IsValidRange(addr, size)) {
		uint8_t *ptr = GetPointerWriteUnchecked(addr);
		memset(ptr, value, size);
	} else {
		// TODO: This mainly seems to be produced by GPUCommon::PerformMemorySet, called from
		// Replace_memset_jak().
		if (Memory::IsValidRange(addr, size)) {
			for (size_t i = 0; i < size; i++) {
				Memory::WriteUnchecked_U8(value, (u32)(addr + i));
			}
		}
	}

	if (tag) {
		NotifyMemInfo(MemBlockFlags::WRITE, addr, size, tag, strlen(tag));
	}
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
