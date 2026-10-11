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

#include "ppsspp_config.h"

#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSAsm.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MemMap.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "unittest/UnitTest.h"

void UnitTestTerminator() {
	// Bails out of jit so we can time things.
	coreState = CORE_POWERDOWN;
	hleSkipDeadbeef();
}

HLEFunction UnitTestFakeSyscalls[] = {
	{0x1234BEEF, &UnitTestTerminator, "UnitTestTerminator"},
};

double ExecCPUTest(bool clearCache = true) {
	int blockTicks = 1000000;

	if (MIPSComp::jit) {
		currentMIPS->pc = PSP_GetUserMemoryBase();
		MIPSComp::JitAt(currentMIPS);
	}

	const double callsPerSecond = CallsPerSecond([&] {
		currentMIPS->pc = PSP_GetUserMemoryBase();
		coreState = CORE_RUNNING_CPU;

		while (coreState == CORE_RUNNING_CPU) {
			mipsr4k.RunLoopUntil(blockTicks);
		}
	}, 0.5, 1000);

	if (MIPSComp::jit) {
		JitBlockCacheDebugInterface *cache = MIPSComp::jit->GetBlockCacheDebugInterface();
		if (cache) {
			JitBlockDebugInfo block = cache->GetBlockDebugInfo(0);
			WARN_LOG(Log::JIT, "Executed %d target instrs, %d IR, for %d orig", (int)block.targetDisasm.size(), (int)block.irDisasm.size(), (int)block.origDisasm.size());
		}

		if (clearCache)
			MIPSComp::jit->ClearCache();
	}

	return callsPerSecond;
}

static void SetupJitHarness() {
	// We register a syscall so we have an easy way to finish the test.
	RegisterHLEModule("UnitTestFakeSyscalls", ARRAY_SIZE(UnitTestFakeSyscalls), UnitTestFakeSyscalls);

	// This is pretty much the bare minimum required to setup jit.
	coreState = CORE_RUNNING_CPU;
	currentMIPS = &mipsr4k;
	g_symbolMap = new SymbolMap();
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE;
	PSP_CoreParameter().cpuCore = CPUCore::INTERPRETER;
	PSP_CoreParameter().fastForward = true;

	Memory::Init(Memory::MemMapSetupFlags::Default);
	mipsr4k.Reset();
	CoreTiming::Init(currentMIPS);
}

static void DestroyJitHarness() {
	// Clear our custom module out to be safe.
	HLEShutdown();
	CoreTiming::Shutdown();
	mipsr4k.Shutdown();
	Memory::Shutdown();
	coreState = CORE_POWERDOWN;
	currentMIPS = nullptr;
	delete g_symbolMap;
}

bool TestJit() {
	SetupJitHarness();

	g_Config.bFastMemory = true;
	currentMIPS->pc = PSP_GetUserMemoryBase();
	u32 *p = (u32 *)Memory::GetPointerOrException(currentMIPS->pc);

	// TODO: Smarter way of seeding in the code sequence.
	static const char *lines[] = {
		//"vcrsp.t C000, C100, C200",
		//"vdot.q C000, C100, C200",
		//"vmmul.q M000, M100, M200",
		"lui r1, 0x8910",
		"vmmul.q M000, M100, M200",
		"sv.q C000, 0(r1)",
		"sv.q C000, 16(r1)",
		"sv.q C000, 32(r1)",
		"sv.q C000, 48(r1)",
		/*
		"abs.s f1, f1",
		"cvt.w.s f1, f1",
		"cvt.w.s f3, f1",
		"cvt.w.s f0, f2",
		"cvt.w.s f5, f1",
		"cvt.w.s f6, f5",
		*/
	};

	bool compileSuccess = true;
	u32 addr = currentMIPS->pc;
	DebugInterface *dbg = currentDebugMIPS;
	for (int i = 0; i < 100; ++i) {
		std::string error;
		for (size_t j = 0; j < ARRAY_SIZE(lines); ++j) {
			p++;
			if (!MipsAssembleOpcode(lines[j], currentDebugMIPS, addr, &error)) {
				printf("ERROR: %s\n", error.c_str());
				compileSuccess = false;
			}
			addr += 4;
		}
	}

	*p++ = MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator");
	*p++ = MIPS_MAKE_BREAK(1);
	*p++ = MIPS_MAKE_JR_RA();

	// The cores keep their block lookup and hooks out of PSP memory, so the code has to read back
	// as assembled while its blocks are live.
	const u8 *code = Memory::GetPointerOrException(currentMIPS->pc);
	const std::vector<u8> assembled(code, (const u8 *)p);
	bool memoryClean = true;
	auto checkMemoryClean = [&](const char *core) {
		if (memcmp(code, assembled.data(), assembled.size()) != 0) {
			printf("%s modified the code in PSP memory\n", core);
			memoryClean = false;
		}
		if (MIPSComp::jit && Memory::ReadBlockShadow(PSP_GetUserMemoryBase()) == 0) {
			printf("%s didn't enter its block in the block shadow\n", core);
			memoryClean = false;
		}
	};

	// Dogfood.
	addr = currentMIPS->pc;
	for (size_t j = 0; j < ARRAY_SIZE(lines); ++j) {
		char line[512];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, line, sizeof(line), true);
		addr += 4;
		printf("%s\n", line);
	}

	printf("\n");

	double jit_speed = 0.0, jit_ir_speed = 0.0, ir_speed = 0.0, interp_speed = 0.0;
	if (compileSuccess) {
		interp_speed = ExecCPUTest();
		checkMemoryClean("Interpreter");
		mipsr4k.UpdateCore(CPUCore::IR_INTERPRETER);
		ir_speed = ExecCPUTest(false);
		checkMemoryClean("IR interpreter");
		MIPSComp::jit->ClearCache();
		mipsr4k.UpdateCore(CPUCore::JIT);
		jit_speed = ExecCPUTest(false);
		checkMemoryClean("JIT");
		MIPSComp::jit->ClearCache();
#if !PPSSPP_PLATFORM(MAC)
		mipsr4k.UpdateCore(CPUCore::JIT_IR);
		jit_ir_speed = ExecCPUTest(false);  // not clearing, so the below can do things.
		checkMemoryClean("JIT IR");
#endif

		// Disassemble
		JitBlockCacheDebugInterface *cache = MIPSComp::jit->GetBlockCacheDebugInterface();
		if (cache) {
			JitBlockDebugInfo block = cache->GetBlockDebugInfo(0);  // Should only be one block.
			std::vector<std::string> &lines = block.targetDisasm;
			// Cut off at 25 due to the repetition above. Might need tweaking for large instructions.
			const int cutoff = 50;
			for (int i = 0; i < std::min((int)lines.size(), cutoff); i++) {
				printf("%s\n", lines[i].c_str());
			}
			if (lines.size() > cutoff)
				printf("...\n");
		}
		printf("Jit was %fx faster than interp, IR was %fx faster, JIT IR %fx.\n\n", jit_speed / interp_speed, ir_speed / interp_speed, jit_ir_speed / interp_speed);
	}

	printf("\n");

	DestroyJitHarness();

	return memoryClean && jit_speed >= interp_speed;
}

// Follows a replacement hook's block shadow entry through compiling, invalidating, clearing and
// switching cores. The hook is installed the normal way, by naming a function memcpy.
static bool CheckReplacementHooks(u32 func, u32 size, const std::vector<u8> &code) {
	auto compile = [&]() {
		currentMIPS->pc = func;
		MIPSComp::JitAt(currentMIPS);
	};
	auto codeIntact = [&]() {
		return memcmp(Memory::GetPointerOrException(func), code.data(), code.size()) == 0;
	};

	EXPECT_TRUE(Replacement_IsHooked(func));
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), (u32)Memory::BLOCK_SHADOW_HOOK_NO_JIT);
	EXPECT_TRUE(codeIntact());

	static const CPUCore cores[] = {
		CPUCore::JIT,
		CPUCore::IR_INTERPRETER,
#if !PPSSPP_PLATFORM(MAC)
		CPUCore::JIT_IR,
#endif
	};
	for (CPUCore core : cores) {
		mipsr4k.UpdateCore(core);
		const u32 hook = MIPSComp::jit->GetBlockShadowHookValue();
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), hook);

		// Compiling replaces the hook value with the block's entry.
		compile();
		const u32 block = Memory::ReadBlockShadow(func);
		EXPECT_TRUE(block != 0 && block != hook);

		// Destroying the block puts it back, whichever way it goes.
		MIPSComp::jit->InvalidateCacheAt(func, 4);
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), hook);
		compile();
		MIPSComp::jit->ClearCache();
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), MIPSComp::jit->GetBlockShadowHookValue());

		// Leave a block for the core switch to destroy.
		compile();
		EXPECT_TRUE(codeIntact());
	}
	mipsr4k.UpdateCore(CPUCore::INTERPRETER);
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), (u32)Memory::BLOCK_SHADOW_HOOK_NO_JIT);

	// Removing the hook while a block holds the entry clears it once the block is destroyed.
	mipsr4k.UpdateCore(CPUCore::JIT);
	compile();
	RestoreReplacedInstructions(func, func + size);
	EXPECT_FALSE(Replacement_IsHooked(func));
	currentMIPS->ProcessPendingInvalidates();
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), 0);

	// Rewriting the rest of the function drops the hook once the game invalidates the icache over it,
	// with the cache instruction (which the JITs leave to the interpreter).
	MIPSAnalyst::ReplaceFunctions();
	const u32 bodyOp = Memory::ReadUnchecked_U32(func + 8);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), func + 8);
	EXPECT_TRUE(Replacement_IsHooked(func));
	currentMIPS->r[MIPS_REG_A0] = func + 8;
	MIPSInterpret(currentMIPS, MIPSOpcode((0x2F << 26) | (MIPS_REG_A0 << 21) | (8 << 16)));  // cache 0x08, 0(a0)
	EXPECT_FALSE(Replacement_IsHooked(func));
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), 0);
	Memory::WriteUnchecked_U32(bodyOp, func + 8);

	// Cheats and the debugger queue the invalidation before writing. The check happens when it's processed.
	MIPSAnalyst::ReplaceFunctions();
	currentMIPS->InvalidateICacheRangeDeferred(func + 8, 4);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), func + 8);
	EXPECT_TRUE(Replacement_IsHooked(func));
	currentMIPS->ProcessPendingInvalidates();
	EXPECT_FALSE(Replacement_IsHooked(func));
	Memory::WriteUnchecked_U32(bodyOp, func + 8);

	// Changed immediates (relocations) keep the hook, as they don't stop the function from matching.
	MIPSAnalyst::ReplaceFunctions();
	Memory::WriteUnchecked_U32(bodyOp + 1, func + 8);
	currentMIPS->InvalidateICacheRangeDeferred(func + 8, 4);
	currentMIPS->ProcessPendingInvalidates();
	EXPECT_TRUE(Replacement_IsHooked(func));
	Memory::WriteUnchecked_U32(bodyOp, func + 8);

	// The game overwriting the hooked instruction drops the hook.
	MIPSAnalyst::ReplaceFunctions();
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), MIPSComp::jit->GetBlockShadowHookValue());
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), func);
	EXPECT_EQ_HEX(ReadExecutedOp(func).encoding, MIPS_MAKE_NOP());
	EXPECT_FALSE(Replacement_IsHooked(func));
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), 0);
	return true;
}

bool TestJitReplacementHooks() {
	SetupJitHarness();
	Replacement_Init();

	const u32 func = PSP_GetUserMemoryBase() + 0x1000;
	static const char *lines[] = {
		"addiu v0, a0, 0",
		"addiu t0, a1, 0",
		"addiu t1, a2, 0",
		"addiu t2, zero, 1",
		"addiu t3, zero, 2",
		"jr ra",
		"nop",
	};
	u32 addr = func;
	for (const char *line : lines) {
		std::string error;
		if (!MipsAssembleOpcode(line, currentDebugMIPS, addr, &error)) {
			printf("ERROR: %s\n", error.c_str());
			return false;
		}
		addr += 4;
	}
	const u32 size = addr - func;
	const u8 *codePtr = Memory::GetPointerOrException(func);
	const std::vector<u8> code(codePtr, codePtr + size);

	g_symbolMap->AddFunction("memcpy", func, size);
	MIPSAnalyst::RegisterFunction(func, size, "memcpy");
	MIPSAnalyst::UpdateHashMap();
	MIPSAnalyst::ReplaceFunctions();

	const bool success = CheckReplacementHooks(func, size, code);

	RestoreReplacedInstructions(func, func + size);
	Replacement_Shutdown();
	MIPSAnalyst::Reset();
	DestroyJitHarness();
	return success;
}
