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

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/MIPSTables.h"

namespace MeCore {

// COP0 register 22, which the reset vector tests to send the ME on to 0x40 (the main CPU's reads 0).
// Only known to be nonzero.
static const u32 ME_CPU_ID = 1;

MEState *g_meState = nullptr;

// Booting the VSH counts as homebrew: vshmain.prx loads as an ELF.
bool Wanted(bool homebrew) {
	return homebrew && g_Config.bEmulateMediaEngine;
}

// To the state reset leaves it in, with its clock running on.
static void ResetCPU(MEState *me) {
	const u64 ticks = GetTicks();
	me->cpu.InitRegisters();
	me->cpu.cop0[22] = ME_CPU_ID;
	me->cpu.pc = Memory::BOOT_PAGE_ADDRESS;
	me->tickBase = ticks;
	me->interruptEnable = 0;
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
	g_meState->cpu.noJit = true;
	g_meState->tickBase = CoreTiming::GetTicks(&mipsr4k);
	ResetCPU(g_meState);
	INFO_LOG(Log::ME, "Media Engine created, %d KB eDRAM", space->edramSize / 1024);
	return true;
}

void Shutdown() {
	if (!g_meState) {
		return;
	}
	delete g_meState;
	g_meState = nullptr;
	Memory::DestroyMESpace();
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
		} else if (!g_meState && !Init()) {
			p.SetError(PointerWrap::ERROR_FAILURE);
			return;
		}
	}
	if (!g_meState) {
		return;
	}

	MEState *me = g_meState;
	me->cpu.DoState(p);
	Do(p, me->inReset);
	Do(p, me->tickBase);
	Do(p, me->interruptEnable);
	DoArray(p, me->cpu.space->GetPointerUnchecked(0), me->cpu.space->edramSize);
	DoArray(p, me->cpu.space->GetPointerUnchecked(0x04000000), me->cpu.space->localSize);
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
	me->cpu.downcount += cycles;
	MIPSInterpret_RunDowncount(&me->cpu);
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

}  // namespace MeCore
