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

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/Interpreter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

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

// Booting the VSH counts as homebrew: vshmain.prx loads as an ELF.
bool Wanted(bool homebrew) {
	return homebrew && g_Config.bEmulateMediaEngine;
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
	Memory::g_kernelModeAnywhere = true;
	INFO_LOG(Log::ME, "Media Engine created, %d KB eDRAM", space->edramSize / 1024);
	return true;
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
	DoArray(p, me->memProtect, ARRAY_SIZE(me->memProtect));
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
	// mfic reads 0 after reset, and homebrew takes interrupts without mtic, so it doesn't gate them.
	if ((status & (STATUS_IE | STATUS_EXL | STATUS_ERL)) != STATUS_IE) {
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
	cpu.downcount += cycles;
	if (!cpu.pendingInvalidates_.empty() || cpu.invalidateAll_) {
		me->waitLoopEnds.clear();
	}
	// The main CPU may have given a waiting loop what it waits for.
	me->waiting = false;
	while (cpu.downcount >= 0 && coreState == CORE_RUNNING_CPU) {
		CheckInterrupts(me);
		if (!me->halted && !me->waiting)
			me->waiting = WaitsForMainCPU(me);
		if (me->halted || me->waiting) {
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

void RaiseInterrupt(u32 sources) {
	g_meState->intcRaised |= sources;
}

void InterruptMainCPU() {
	CoreTiming::ScheduleEvent(0, meIntrEvent);
}

}  // namespace MeCore
