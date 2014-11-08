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

#include "base/timeutil.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MemMap.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"

struct InputState;
// Temporary hacks around annoying linking errors.  Copied from Headless.
void D3D9_SwapBuffers() { }
void GL_SwapBuffers() { }
void NativeUpdate(InputState &input_state) { }
void NativeRender() { }
void NativeResized() { }

void System_SendMessage(const char *command, const char *parameter) {}
bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue) { return false; }

#ifndef _WIN32
InputState input_state;
#endif

void UnitTestTerminator() {
	coreState = CORE_POWERDOWN;
}

HLEFunction UnitTestFakeSyscalls[] = {
	{0x1234BEEF, &UnitTestTerminator, "UnitTestTerminator"},
};

double ExecCPUTest() {
	int blockTicks = 1000000;
	int total = 0;
	double st = real_time_now();
	do {
		for (int j = 0; j < 1000; ++j) {
			currentMIPS->pc = PSP_GetUserMemoryBase();
			coreState = CORE_RUNNING;

			while (coreState == CORE_RUNNING) {
				mipsr4k.RunLoopUntil(blockTicks);
			}
			++total;
		}
	}
	while (real_time_now() - st < 0.5);
	double elapsed = real_time_now() - st;

	return total / elapsed;
}

static void SetupJitHarness() {
	// We register a syscall so we have an easy way to finish the test.
	RegisterModule("UnitTestFakeSyscalls", ARRAY_SIZE(UnitTestFakeSyscalls), UnitTestFakeSyscalls);

	// This is pretty much the bare minimum required to setup jit.
	coreState = CORE_POWERUP;
	currentMIPS = &mipsr4k;
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE;
	PSP_CoreParameter().cpuCore = CPU_INTERPRETER;
	PSP_CoreParameter().unthrottle = true;

	Memory::Init();
	mipsr4k.Reset();
	CoreTiming::Init();
}

static void DestroyJitHarness() {
	// Clear our custom module out to be safe.
	HLEShutdown();
	CoreTiming::Shutdown();
	Memory::Shutdown();
	mipsr4k.Shutdown();
	coreState = CORE_POWERDOWN;
	currentMIPS = nullptr;
}

bool TestJit() {
	SetupJitHarness();

	currentMIPS->pc = PSP_GetUserMemoryBase();
	u32 *p = (u32 *)Memory::GetPointer(currentMIPS->pc);

	// TODO: Smarter way of seeding in the code sequence.
	for (int i = 0; i < 100; ++i) {
		*p++ = 0xD03B0000 | (1 >> 7) | (7 << 8);
		*p++ = 0xD03B0000 | (1 >> 7);
		*p++ = 0xD03B0000 | (7 << 8);
		*p++ = 0xD03B0000 | (1 >> 7) | (7 << 8);
		*p++ = 0xD03B0000 | (1 >> 7) | (7 << 8);
		*p++ = 0xD03B0000 | (1 >> 7) | (7 << 8);
		*p++ = 0xD03B0000 | (1 >> 7) | (7 << 8);
	}

	*p++ = MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator");
	*p++ = MIPS_MAKE_BREAK(1);

	double interp_speed = ExecCPUTest();

	mipsr4k.UpdateCore(CPU_JIT);

	double jit_speed = ExecCPUTest();

	printf("Jit was %fx faster than interp.\n", jit_speed / interp_speed);

	DestroyJitHarness();

	return jit_speed >= interp_speed;
}