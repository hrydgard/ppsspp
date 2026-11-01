// Copyright (c) 2012- PPSSPP Project.

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
#include <cstring>
#include <vector>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/PopsBoot.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HW/MeMMIO.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/Interpreter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Util/KL4E.h"

namespace MeCore {

// The ME's state after a reset, read on a PSP-1000 (a kernel-mode probe, not kept as a test: the
// values left over from Sony's ME code aren't deterministic): COP0 register 22 is 1, which sends it
// on to 0x40 from the reset vector; Status 0x20500404 (CU1, BEV, SR, IM2, ERL); the GPRs keep
// whatever was in them, sp 0x801FFFE0 from Sony's code (top of the 2MB eDRAM); mfic 0; Count 0, and
// it doesn't run, with ERL set or Status cleared.
// Sysreg 0x40 read 0x20000002 and 0x50 0xD61D.
static const u32 ME_CPU_ID = 1;
static const u32 RESET_STATUS = 0x20500404;
static const u32 STATUS_RESET_BITS = 0x00500004;

// Pending interrupts are checked between runs of this many cycles at most.
static const int INTERRUPT_CHECK_CYCLES = 256;

enum : u32 {
	STATUS_IE = 0x01,
	STATUS_EXL = 0x02,
	STATUS_ERL = 0x04,
	CAUSE_IP2 = 0x0400,  // The interrupt controller.
	CAUSE_IP7 = 0x8000,  // Count reached Compare. Never on its own, since Count doesn't run.
	CAUSE_IP_MASK = 0xFF00,
	CAUSE_SOFT_IP = 0x0300,
	CAUSE_EXCCODE_BD = 0x8000007C,
};

MEState *g_meState = nullptr;
static int meIntrEvent = -1;

// Booting the VSH counts as homebrew: vshmain.prx loads as an ELF. A PS1 game always gets the ME:
// the firmware's PS1 emulator plays its sound on it.
bool Wanted(bool homebrew, bool pops) {
	return pops || (homebrew && g_Config.bEmulateMediaEngine);
}

static void MeIntrCallback(u64 userdata, int cyclesLate) {
	__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_MECODEC_INTR, PSP_INTR_SUB_NONE);
}

// Reset keeps the GPRs, and homebrew relies on that: mcidclan's samples run C on the stack Sony's
// code left in sp.
static void ResetCPU(MEState *me) {
	MIPSState &cpu = me->cpu;
	cpu.cop0[30] = cpu.pc;  // ErrorEPC
	cpu.cop0[12] |= STATUS_RESET_BITS;
	cpu.pc = Memory::BOOT_PAGE_ADDRESS;
	cpu.nextPC = 0;
	cpu.inDelaySlot = false;
	cpu.llBit = 0;
	cpu.cop0[9] = 0;
	me->halted = false;
	me->interruptEnable = 0;
	me->intcRaised = 0;
	me->intcEnabled = 0;
}

// What Sony's ME code leaves, which homebrew finds there.
static void PowerOn(MEState *me) {
	MIPSState &cpu = me->cpu;
	cpu.InitRegisters();
	cpu.cop0[12] = RESET_STATUS;
	cpu.cop0[22] = ME_CPU_ID;
	cpu.r[MIPS_REG_SP] = 0x80000000 + cpu.space->edramSize - 0x20;
	me->ramConfig = 0x20000002;
	// The luma offset (16) and chroma offset of the firmware's default conversion (avcodec.prx).
	me->dmacplus.csc[(0x15C - 0x120) / 4] = 0x110;
	me->busClock = 0xD61D;
	// Held in reset, as the register says: a read-modify-write of it mustn't release the ME.
	me->resetFlags = 4;
	ResetCPU(me);
}

bool Init() {
	_dbg_assert_(!g_meState);
	Memory::AddressSpace *space = Memory::CreateMESpace();
	if (!space) {
		WARN_LOG(Log::ME, "No Media Engine address space on this platform, not emulating the ME");
		return false;
	}
	g_meState = new MEState();
	g_meState->cpu.space = space;
	g_meState->cpu.Init();
	g_meState->tickBase = CoreTiming::GetTicks(&mipsr4k);
	PowerOn(g_meState);
	meIntrEvent = CoreTiming::RegisterEvent("MeIntr", &MeIntrCallback);
	// Not for POPS, whose code that reaches hardware is all kernel code anyway. A savestate's POPS boot
	// is known by now; a new one turns it off in KernelInit().
	Memory::g_kernelModeAnywhere = !PopsBootActive();
	INFO_LOG(Log::ME, "Media Engine created, %d KB eDRAM", space->edramSize / 1024);
	return true;
}

// Sony's ME firmware, as me_wrapper boots it (6.61's sceMeCore_driver_5dff5c50 and the boot at
// 0x1c30): it reads the image to just below 0x883FF000 and decrypts it there, which leaves two KL4E
// streams, the code and its data. Its ME stub decompresses the code to 0x88300000 and calls it with
// a0 at the second stream, which the code decompresses into the ME's eDRAM itself. That ends in the
// main loop's halt, waiting for commands. The stub runs main CPU kernel routines (KL4E, caches), so
// its part is done here. The compressed copy goes right after the code instead, and the stack (at
// 0x88400000 on a PSP) below our idle threads' stacks, which are at the top of kernel memory. mcidclan's libraries also call functions in the image, picking per-model
// address tables by the word at 0x18 (0x279c1d44 the PSP-1000's meimg, 0x279c637c the slim's
// t2img).
static const u32 FIRMWARE_IMAGE_ADDRESS = 0x08300000;
// The code clears up to 0x083FFC00 once it's done with the compressed copy, but our idle threads'
// stacks start at 0x083FE000 (they don't mind).
static const u32 FIRMWARE_AREA_END = 0x083FE000;
static const u32 FIRMWARE_STACK = 0x80000000 | FIRMWARE_AREA_END;
// Where it returns to if something fails: halt, b ., in the boot page.
static const u32 FIRMWARE_FAILED = 0xBFC00020;
// What me_wrapper's bootstrap jumps to after a reset, standing in for its ME stub (below).
static const u32 FIRMWARE_RESTART = 0xBFC00030;

// Loads the image, and returns where its second stream is (0 if there's no image).
static u32 LoadFirmwareImage() {
	// The 6.61 me_wrapper picks by the Tachyon version: me_t2img.img from the slim on, meimg.img before.
	const char *path = Memory::g_PSPModel == PSP_MODEL_FAT ? "flash0:/kd/resource/meimg.img" : "flash0:/kd/resource/me_t2img.img";
	std::vector<u8> file;
	if (pspFileSystem.ReadEntireFile(path, file, true) < 0 || file.size() < 0x150) {
		WARN_LOG(Log::ME, "No %s in the firmware, so no ME firmware", path);
		return 0;
	}
	std::vector<u8> plain(file.size());
	const int plainSize = pspDecryptPRX(file.data(), plain.data(), (u32)file.size());
	bool isKL3E = false;
	if (plainSize <= 4 || !IsKL4EMagic(plain.data(), plainSize, &isKL3E)) {
		ERROR_LOG(Log::ME, "%s didn't decrypt", path);
		return 0;
	}
	if (kernelMemory.AllocAt(FIRMWARE_IMAGE_ADDRESS, FIRMWARE_AREA_END - FIRMWARE_IMAGE_ADDRESS, "ME firmware") != FIRMWARE_IMAGE_ADDRESS) {
		WARN_LOG(Log::ME, "Kernel memory at %08x is taken, the ME firmware may get overwritten", FIRMWARE_IMAGE_ADDRESS);
	}
	// Leave room for the compressed copy, and some stack.
	const u32 room = FIRMWARE_AREA_END - FIRMWARE_IMAGE_ADDRESS - plainSize - 0x10000;
	u8 *dest = Memory::GetTypedPointerWriteRange<u8>(FIRMWARE_IMAGE_ADDRESS, room);
	const u8 *end = nullptr;
	const int size = dest ? DecompressKL4E(dest, room, plain.data() + 4, (size_t)plainSize - 4, &end, isKL3E) : -1;
	if (size <= 0 || !end || end + 4 > plain.data() + plainSize || memcmp(end, "KL4E", 4) != 0) {
		ERROR_LOG(Log::ME, "%s didn't decompress", path);
		return 0;
	}
	const u32 compressed = (FIRMWARE_IMAGE_ADDRESS + size + 0x3F) & ~0x3F;
	Memory::MemcpyUnchecked(compressed, plain.data(), plainSize);
	INFO_LOG(Log::ME, "Loaded %s: code at %08x, %d bytes", path, FIRMWARE_IMAGE_ADDRESS, size);
	return compressed + (u32)(end - plain.data());
}

// The state the bootstrap and stub leave the ME in, and what me_wrapper leaves in the boot page.
static void BootFirmware(MEState *me, u32 data) {
	u8 *page = Memory::GetBootPage();
	auto poke = [&](u32 offset, u32 value) { *(u32_le *)(page + offset) = value; };
	poke(FIRMWARE_FAILED & 0xFFF, 0x70000000);         // halt
	poke((FIRMWARE_FAILED & 0xFFF) + 4, 0x1000FFFE);   // b FIRMWARE_FAILED
	poke((FIRMWARE_FAILED & 0xFFF) + 8, 0);
	// me_wrapper's bootstrap, which the ME runs from 0x40 whenever it comes out of reset, with the
	// layout of 6.61's (me_wrapper+0): mcidclan's MIST restarts the ME through it, after patching
	// +0x20..+0x28 to point a0 at 0x88400000. If 0xBC100000 is set, which nothing seems to do, it
	// goes to the firmware's debug handler. The cache invalidation is left out.
	static const u32 bootstrap[0xD4 / 4] = {
		0x3C02BC10, 0x8C420000, 0x10400005, 0x00000000,  // lui v0,0xBC10; lw v0,0(v0); beqz v0,+0x20; nop
		0x40424800, 0x00400008, 0x00000000, 0x00000000,  // cfc0 v0,$9; jr v0; nop; nop
		0x24090000, 0x3C040000, 0x34840000,              // li t1,ramcfg; lui a0,hi(image); ori a0,a0,lo(image)
		0x3C02BC10, 0xAC490040, 0x24030007, 0xAC430050,  // 0xBC100040 = t1, 0xBC100050 = 7
		0x2403FFFF, 0xAC430004, 0x0000000F,              // 0xBC100004 = -1; sync
		0x40806800, 0x3C032000, 0x40836000,              // Cause = 0, Status = 0x20000000
		0x3C02BCC0, 0x24030001, 0xAC430010,              // VME 0x10 = 1, and wait for it
		0x8C430010, 0x30630001, 0x1460FFFD, 0x00000000,
		0x24030001, 0xAC430070, 0x24030008, 0xAC430030,  // VME 0x70 = 1, 0x30 = 8, 0x40 = 2
		0x24030002, 0xAC430040,
	};
	for (size_t i = 0; i < ARRAY_SIZE(bootstrap); i++) {
		poke(0x40 + (u32)i * 4, bootstrap[i]);
	}
	poke(0x40 + 0x20, 0x24090000 | (me->ramConfig & 3));
	poke(0x40 + 0x24, 0x3C040000 | ((0x80000000 | data) >> 16));
	poke(0x40 + 0x28, 0x34840000 | (data & 0xFFFF));
	poke(0x40 + 0xC0, 0x3C1F0000 | (FIRMWARE_RESTART >> 16));     // lui ra
	poke(0x40 + 0xC4, 0x27FF0000 | (FIRMWARE_RESTART & 0xFFFF));  // addiu ra
	poke(0x40 + 0xC8, 0x3C1D0000 | (FIRMWARE_STACK >> 16));       // lui sp
	poke(0x40 + 0xCC, 0x03E00008);                                 // jr ra
	// me_wrapper's ME stub decompresses the image again when a0 is in 0x88000000..0x883FFFFF. Only
	// its own reboots do that, which don't happen without it, so here it always does what it does
	// otherwise: run the image, which then restores eDRAM from a0, as when resuming from sleep.
	poke(FIRMWARE_RESTART & 0xFFF, 0x3C010000 | (0x80000000 | FIRMWARE_IMAGE_ADDRESS) >> 16);  // lui at
	poke((FIRMWARE_RESTART & 0xFFF) + 4, 0x00200008);  // jr at
	poke((FIRMWARE_RESTART & 0xFFF) + 8, 0);
	poke(0x70C, 0x01FF01FF);
	poke(0x710, CoreTiming::GetClockFrequencyHz() / 1000000);
	poke(0x714, 2);
	poke(0x700, 0);

	me->busClock = 7;
	me->nmiFlags = 0xFFFFFFFF;
	me->vmeControl[0x70 / 4] = 1;
	me->vmeControl[0x30 / 4] = 8;
	me->vmeControl[0x40 / 4] = 2;

	MIPSState &cpu = me->cpu;
	cpu.pc = 0x80000000 | FIRMWARE_IMAGE_ADDRESS;
	cpu.r[MIPS_REG_A0] = 0x80000000 | data;
	cpu.r[MIPS_REG_SP] = FIRMWARE_STACK;
	cpu.r[MIPS_REG_RA] = FIRMWARE_FAILED;
	cpu.cop0[12] = 0x20000000;
	cpu.cop0[13] = 0;
	me->resetFlags &= ~4;
	me->inReset = false;
}

// A sysevent handler record (PspSysEventHandler in pspsdk), with its name after it.
static u32 CreateSysEventHandler(const char *name) {
	u32 size = 0x40 + 16;
	const u32 addr = kernelMemory.Alloc(size, false, "SysEventHandler");
	if (addr == (u32)-1)
		return 0;
	memset(Memory::GetPointerWriteUnchecked(addr), 0, size);
	Memory::WriteUnchecked_U32(0x40, addr);              // size
	Memory::WriteUnchecked_U32(addr + 0x40, addr + 4);   // name
	Memory::WriteUnchecked_U32(0x00FFFF00, addr + 8);    // type mask: suspend and resume
	Memory::MemcpyUnchecked(addr + 0x40, name, (u32)strlen(name) + 1);
	return addr;
}

void KernelInit() {
	if (!g_meState)
		return;
	// The POPS manager boots the ME with its own code (POPS's reboot has no me_wrapper).
	if (PopsBootActive()) {
		Memory::g_kernelModeAnywhere = false;
		return;
	}
	if (const u32 data = LoadFirmwareImage()) {
		BootFirmware(g_meState, data);
		g_meState->booting = true;
	}
	// Its handler is never called: nothing here suspends.
	g_meState->sysEventHandlers = CreateSysEventHandler("SceMeRpc");
}

void Shutdown() {
	if (!g_meState) {
		return;
	}
	g_meState->cpu.Shutdown();
	delete g_meState;
	g_meState = nullptr;
	Memory::DestroyMESpace();
	Memory::g_kernelModeAnywhere = false;
}

void DoState(PointerWrap &p) {
	auto s = p.Section("MeCore", 1);
	if (!s) {
		return;
	}

	bool present = g_meState != nullptr;
	Do(p, present);
	if (p.mode == PointerWrap::MODE_READ) {
		if (!present) {
			Shutdown();
		} else if (!g_meState) {
			if (!Init()) {
				p.SetError(PointerWrap::ERROR_FAILURE);
				return;
			}
			// Compiled for user mode.
			mipsr4k.ClearJitCacheDeferred();
		}
	}
	if (!g_meState) {
		return;
	}

	MEState *me = g_meState;
	me->cpu.DoState(p);
	Do(p, me->inReset);
	Do(p, me->halted);
	Do(p, me->tickBase);
	Do(p, me->interruptEnable);
	Do(p, me->intcRaised);
	Do(p, me->intcEnabled);
	Do(p, me->resetFlags);
	Do(p, me->busClock);
	Do(p, me->nmiFlags);
	Do(p, me->ramConfig);
	Do(p, me->mutex);
	Do(p, me->sysreg54);
	Do(p, me->sysreg5C);
	Do(p, me->sysreg70);
	Do(p, me->booting);
	DoArray(p, me->sysregOther, ARRAY_SIZE(me->sysregOther));
	DoArray(p, me->audio.regs, ARRAY_SIZE(me->audio.regs));
	DoArray(p, me->audio.fifo, ARRAY_SIZE(me->audio.fifo));
	Do(p, me->audio.fifoCount);
	Do(p, me->audio.underrun);
	Do(p, me->audio.lastPlayed);
	Do(p, me->audio.nextPlayed);
	DoArray(p, me->memProtect, ARRAY_SIZE(me->memProtect));
	DoArray(p, me->vmeControl, ARRAY_SIZE(me->vmeControl));
	DoArray(p, me->clocks, ARRAY_SIZE(me->clocks));
	Do(p, me->sysEventHandlers);
	Do(p, me->dmacplus.intTCStatus);
	DoArray(p, me->dmacplus.lcd, ARRAY_SIZE(me->dmacplus.lcd));
	DoArray(p, me->dmacplus.csc, ARRAY_SIZE(me->dmacplus.csc));
	DoArray(p, &me->dmacplus.channels[0][0], 4 * 5);
	DoArray(p, me->cpu.space->GetPointerUnchecked(0), me->cpu.space->edramSize);
	DoArray(p, me->cpu.space->GetPointerUnchecked(0x04000000), me->cpu.space->localSize);
	Do(p, meIntrEvent);
	CoreTiming::RestoreRegisterEvent(meIntrEvent, "MeIntr", &MeIntrCallback);
}

static u32 PendingInterrupts(const MEState *me) {
	u32 cause = me->cpu.cop0[13] & CAUSE_IP_MASK;
	if (me->intcRaised & me->intcEnabled) {
		cause |= CAUSE_IP2;
	}
	return cause;
}

// An unmasked interrupt wakes it from halt, and is taken if interrupts are enabled. Never in a
// delay slot: the run loop only stops outside one.
static void CheckInterrupts(MEState *me) {
	MIPSState &cpu = me->cpu;
	u32 &status = cpu.cop0[12];
	if ((PendingInterrupts(me) & status & CAUSE_IP_MASK) == 0) {
		return;
	}
	me->halted = false;
	// Either Status.IE or mtic enables them: homebrew takes interrupts with IE set and $ic at its reset
	// value of 0 (a PSP-1000 probe), while Sony's firmware never sets IE and gates them with mtic alone
	// (t2img's main loop at 0x883003a4, whose handler at 0x8838c880 sets the command flag it waits for).
	// That loop relies on mtic 0 keeping them out, so with both clear they aren't taken.
	const bool enabled = (status & STATUS_IE) || me->interruptEnable;
	if (!enabled || (status & (STATUS_EXL | STATUS_ERL))) {
		return;
	}
	me->waiting = false;
	// Interrupts go straight to EBase (COP0 25), with ExcCode 0.
	cpu.cop0[14] = cpu.pc;
	cpu.cop0[13] &= ~CAUSE_EXCCODE_BD;
	status |= STATUS_EXL;
	cpu.pc = cpu.cop0[25];
	cpu.llBit = 0;
}

// Most ME code that waits for the main CPU polls in a loop like lw t0, flag; beq t0, zero, loop; nop.
// Such a loop stores nothing and reads only what it wrote itself earlier in the same pass (or what
// it doesn't write), so every pass goes the same way while memory stays the same - and it does
// while the ME runs, since the main CPU doesn't. One that loops back once will loop until the end
// of the slice. Sony's firmware waits with halt instead, and homebrew loops that also store (a
// heartbeat) or span several blocks aren't recognized.
static const int MAX_WAIT_LOOP_INSTRUCTIONS = 16;

static bool IsPlainOp(MIPSOpcode op, MIPSInfo info) {
	const u64 notPlain = BAD_INSTRUCTION | OUT_MEM | OUT_OTHER | IN_OTHER | IS_SYSCALL | IS_FPU | IS_VFPU |
		IN_FPUFLAG | OUT_FPUFLAG | IN_VFPU_CC | OUT_VFPU_CC | IN_LO | IN_HI | OUT_LO | OUT_HI | OUT_RA |
		IN_FS | IN_FT | OUT_FD | OUT_FS | OUT_FT | IN_VS | IN_VT | OUT_VD;
	if (info & notPlain)
		return false;
	switch (op >> 26) {
	case 0x10:  // cop0, eret
	case 0x1C:  // halt, mfic, mtic
	case 0x22:  // lwl
	case 0x26:  // lwr
	case 0x2F:  // cache
	case 0x30:  // ll
		return false;
	default:
		return true;
	}
}

// Where a conditional branch or j goes, or 0 if it isn't one (or is a jr, jal or similar).
static u32 BranchTarget(u32 pc, MIPSOpcode op) {
	const u32 code = op >> 26;
	const bool regimm = code == 1 && ((op >> 16) & 0x1C) == 0;  // bltz, bgez and their likely forms
	if ((code >= 4 && code <= 7) || (code >= 0x14 && code <= 0x17) || regimm)
		return pc + 4 + ((s32)(s16)(op & 0xFFFF) << 2);
	if (code == 2)
		return ((pc + 4) & 0xF0000000) | ((op & 0x03FFFFFF) << 2);
	return 0;
}

// The end (after the delay slot) of a wait loop starting at start, or 0 if it isn't one.
static u32 FindWaitLoop(const Memory::AddressSpace &space, u32 start) {
	MIPSOpcode ops[MAX_WAIT_LOOP_INSTRUCTIONS + 1];
	int count = 0;
	bool loops = false;
	for (u32 pc = start; count < MAX_WAIT_LOOP_INSTRUCTIONS; pc += 4) {
		if (!space.IsValid4AlignedAddress(pc))
			return 0;
		const MIPSOpcode op(space.ReadUnchecked_U32(pc));
		const MIPSInfo info = MIPSGetInfo(op);
		ops[count++] = op;
		if (loops) {
			// The back edge's delay slot.
			if ((info & (IS_CONDBRANCH | IS_JUMP)) || !IsPlainOp(op, info))
				return 0;
			break;
		}
		if (info & (IS_CONDBRANCH | IS_JUMP)) {
			const u32 target = BranchTarget(pc, op);
			// An exit is fine, but likely branches skip their delay slot when not taken.
			if (target == 0 || ((info & LIKELY) && target != start))
				return 0;
			loops = target == start;
		} else if (!IsPlainOp(op, info)) {
			return 0;
		}
	}
	if (!loops)
		return 0;

	// Every register the loop writes has to be written before it's read in a pass.
	u32 written = 0;
	for (int i = 0; i < count; ++i) {
		const MIPSGPReg out = MIPSAnalyst::GetOutGPReg(ops[i]);
		if (out != MIPS_REG_INVALID && out != MIPS_REG_ZERO)
			written |= 1U << out;
	}
	u32 writtenSoFar = 0;
	for (int i = 0; i < count; ++i) {
		for (int r = 1; r < 32; ++r) {
			if ((written & (1U << r)) && !(writtenSoFar & (1U << r)) && MIPSAnalyst::ReadsFromGPReg(ops[i], (MIPSGPReg)r))
				return 0;
		}
		const MIPSGPReg out = MIPSAnalyst::GetOutGPReg(ops[i]);
		if (out != MIPS_REG_INVALID && out != MIPS_REG_ZERO)
			writtenSoFar |= 1U << out;
	}
	return start + count * 4;
}

// At the top of a wait loop, runs a pass of it. If that comes back to the top, it would only go on
// doing so: true, and the ME waits out the rest of the slice.
static bool WaitsForMainCPU(MEState *me) {
	MIPSState &cpu = me->cpu;
	const u32 start = cpu.pc;
	auto it = me->waitLoopEnds.find(start);
	if (it == me->waitLoopEnds.end())
		it = me->waitLoopEnds.emplace(start, FindWaitLoop(*cpu.space, start)).first;
	const u32 end = it->second;
	if (end == 0 || cpu.inDelaySlot)
		return false;
	for (int i = 0; i <= MAX_WAIT_LOOP_INSTRUCTIONS; ++i) {
		cpu.downcount -= MIPS_InterpretSingleStep(&cpu);
		if (cpu.inDelaySlot)
			continue;
		if (cpu.pc == start)
			return true;
		if (cpu.pc < start || cpu.pc >= end)
			return false;
	}
	return false;
}

static void RunCPU(MIPSState &cpu) {
	if (!cpu.jit) {
		MIPSInterpret_RunDowncount(&cpu);
		return;
	}
	// Blocks can't start in a delay slot.
	while (cpu.inDelaySlot && cpu.downcount >= 0) {
		cpu.downcount -= MIPS_InterpretSingleStep(&cpu);
	}
	cpu.ProcessPendingInvalidates();
	cpu.jit->RunDowncount();
}

static void RunUntilIdle(MEState *me);

void RunFor(int cycles) {
	MEState *me = g_meState;
	if (cycles <= 0) {
		return;
	}
	me->tickBase += cycles;
	if (me->inReset) {
		return;
	}
	MIPSState &cpu = me->cpu;
	if (me->booting) {
		me->booting = false;
		RunUntilIdle(me);
	}
	cpu.downcount += cycles;
	if (!cpu.pendingInvalidates_.empty() || cpu.invalidateAll_) {
		me->waitLoopEnds.clear();
	}
	// The main CPU may have given a waiting loop what it waits for.
	me->waiting = false;
	while (cpu.downcount >= 0 && coreState == CORE_RUNNING_CPU) {
		MeMMIO::AudioUpdate(GetTicks());
		CheckInterrupts(me);
		if (!me->halted && !me->waiting)
			me->waiting = WaitsForMainCPU(me);
		if (me->halted) {
			// Asleep until the audio FIFO next has room, if that's within this slice: POPS's ME
			// loop sleeps for each sample it plays.
			const s64 wake = MeMMIO::AudioNextEvent();
			cpu.downcount = wake <= me->tickBase ? (int)(me->tickBase - wake) : -1;
		} else if (me->waiting) {
			cpu.downcount = -1;
		} else {
			// Run a part, with the rest set aside so the clock stays right.
			const int part = std::min(cpu.downcount, INTERRUPT_CHECK_CYCLES);
			const int rest = cpu.downcount - part;
			cpu.downcount = part;
			me->tickBase -= rest;
			RunCPU(cpu);
			cpu.downcount += rest;
			me->tickBase += rest;
		}
	}
}

// On a PSP the ME's firmware has booted long before a game starts, and homebrew that takes over
// the ME (MIST's restart) relies on the eDRAM it leaves. So it runs to its idle halt the first time
// the ME runs (the JIT only runs then), outside emulated time. Bounded, in case it never gets there.
static void RunUntilIdle(MEState *me) {
	MIPSState &cpu = me->cpu;
	for (int i = 0; i < 1024; ++i) {
		CheckInterrupts(me);
		if (me->halted || me->inReset)
			break;
		cpu.downcount = 1 << 20;
		RunCPU(cpu);
	}
	cpu.downcount = 0;
	if (me->halted) {
		INFO_LOG(Log::ME, "ME firmware idle at %08x", cpu.pc);
	} else {
		WARN_LOG(Log::ME, "ME firmware didn't reach its idle halt, at %08x", cpu.pc);
	}
}

bool SyscallDoesNothing(MIPSOpcode op, u32 pc) {
	static const char *const cacheFunctions[] = {
		"sceKernelDcacheWritebackAll",
		"sceKernelDcacheWritebackInvalidateAll",
		"sceKernelDcacheWritebackRange",
		"sceKernelDcacheWritebackInvalidateRange",
		"sceKernelDcacheInvalidateRange",
	};
	const HLEFunction *func = GetSyscallFunctionData(op, pc);
	if (!func)
		return true;
	for (const char *name : cacheFunctions) {
		if (!strcmp(func->name, name))
			return true;
	}
	ERROR_LOG(Log::ME, "syscall to %s on the Media Engine at %08x", func->name, pc);
	return false;
}

void SetReset(bool asserted) {
	MEState *me = g_meState;
	if (asserted) {
		me->inReset = true;
	} else if (me->inReset) {
		ResetCPU(me);
		me->inReset = false;
	}
}

u64 GetTicks() {
	return (u64)(g_meState->tickBase - g_meState->cpu.downcount);
}

u32 ReadCop0(int reg) {
	MEState *me = g_meState;
	switch (reg) {
	case 13:
		return (me->cpu.cop0[13] & ~CAUSE_IP_MASK) | PendingInterrupts(me);
	default:
		return me->cpu.cop0[reg];
	}
}

void WriteCop0(int reg, u32 value) {
	MEState *me = g_meState;
	switch (reg) {
	case 11:
		me->cpu.cop0[11] = value;
		me->cpu.cop0[13] &= ~CAUSE_IP7;
		break;
	case 13:
		me->cpu.cop0[13] = (me->cpu.cop0[13] & ~CAUSE_SOFT_IP) | (value & CAUSE_SOFT_IP);
		break;
	default:
		me->cpu.cop0[reg] = value;
		break;
	}
}

void Halt() {
	g_meState->halted = true;
	// Ends the run, the rest of it spent halted.
	g_meState->cpu.downcount = std::min(g_meState->cpu.downcount, -1);
}

void SetInterruptEnable(u32 value) {
	MEState *me = g_meState;
	me->interruptEnable = value & 1;
	// Sony's firmware enables them for only a few instructions at a time, so a pending one has to be
	// taken now: ends the run, which checks for them next.
	if (me->interruptEnable && (PendingInterrupts(me) & me->cpu.cop0[12] & CAUSE_IP_MASK))
		me->cpu.downcount = std::min(me->cpu.downcount, -1);
}

void RaiseInterrupt(u32 sources) {
	g_meState->intcRaised |= sources;
}

void InterruptMainCPU() {
	CoreTiming::ScheduleEvent(0, meIntrEvent);
}

}  // namespace MeCore
