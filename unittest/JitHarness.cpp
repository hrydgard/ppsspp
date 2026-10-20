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
#include <map>
#include <string>
#include <cfloat>
#include <cstring>
#include <vector>

#include "ppsspp_config.h"

#include "Common/CPUDetect.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/Interpreter.h"
#include "Core/MIPS/MeCore.h"
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
#include "Core/HW/MMIO.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Common/Serialize/Serializer.h"
#if PPSSPP_ARCH(RISCV64)
#include "Core/MIPS/RiscV/RiscVVfpuDot.h"
#elif PPSSPP_ARCH(LOONGARCH64)
#include "Core/MIPS/LoongArch64/LoongArch64VfpuDot.h"
#endif
#include "unittest/UnitTest.h"

// The clock as the terminating syscall sees it, so the cores can be compared on cycles too.
static u64 g_terminatorTicks;

void UnitTestTerminator(MIPSState *mips) {
	g_terminatorTicks = CoreTiming::GetTicks(mips);
	// Bails out of jit so we can time things.
	coreState = CORE_POWERDOWN;
	hleSkipDeadbeef();
}

// What the host FPU does in HLE code: 1/3 (0x3EAAAAAB when rounding to nearest) and FLT_MIN / 2
// (0x00400000 unless flushed to zero). HLE must always see the host's own mode.
static std::vector<u32> g_roundingProbes;

void UnitTestRoundingProbe(MIPSState *mips) {
	volatile float one = 1.0f, three = 3.0f, fltMin = FLT_MIN, half = 0.5f;
	float third = one / three;
	float denormal = fltMin * half;
	u32 bits;
	memcpy(&bits, &third, 4);
	g_roundingProbes.push_back(bits);
	memcpy(&bits, &denormal, 4);
	g_roundingProbes.push_back(bits);
	hleSkipDeadbeef();
}

HLEFunction UnitTestFakeSyscalls[] = {
	{0x1234BEEF, &UnitTestTerminator, "UnitTestTerminator"},
	{0x1234BEF0, &UnitTestRoundingProbe, "UnitTestRoundingProbe"},
};

double ExecCPUTest(bool clearCache = true) {
	int blockTicks = 1000000;

	if (mipsr4k.jit) {
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

	if (mipsr4k.jit) {
		JitBlockCacheDebugInterface *cache = mipsr4k.jit->GetBlockCacheDebugInterface();
		if (cache) {
			JitBlockDebugInfo block = cache->GetBlockDebugInfo(0);
			WARN_LOG(Log::JIT, "Executed %d target instrs, %d IR, for %d orig", (int)block.targetDisasm.size(), (int)block.irDisasm.size(), (int)block.origDisasm.size());
		}

		if (clearCache)
			mipsr4k.jit->ClearCache();
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
		if (mipsr4k.jit && Memory::ReadBlockShadow(PSP_GetUserMemoryBase()) == 0) {
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

	double jit_speed = 0.0, ir_speed = 0.0, interp_speed = 0.0;
	if (compileSuccess) {
		interp_speed = ExecCPUTest();
		checkMemoryClean("Interpreter");
		mipsr4k.UpdateCore(CPUCore::IR_INTERPRETER);
		ir_speed = ExecCPUTest(false);
		checkMemoryClean("IR interpreter");
		mipsr4k.jit->ClearCache();
		mipsr4k.UpdateCore(CPUCore::JIT);
		jit_speed = ExecCPUTest(false);  // not clearing, so the below can do things.
		checkMemoryClean("JIT");

		// The native JIT keeps no IR, and compiles it again for the debugger.
		MIPSComp::JitStats stats{};
		mipsr4k.jit->GetStats(&stats);
		if (stats.irArenaUsed != 0) {
			printf("JIT kept %d IR instructions\n", (int)stats.irArenaUsed);
			memoryClean = false;
		}

		// Disassemble
		JitBlockCacheDebugInterface *cache = mipsr4k.jit->GetBlockCacheDebugInterface();
		if (cache) {
			JitBlockDebugInfo block = cache->GetBlockDebugInfo(0);  // Should only be one block.
			if (block.irDisasm.empty()) {
				printf("JIT block has no IR to show\n");
				memoryClean = false;
			}
			std::vector<std::string> &lines = block.targetDisasm;
			// Cut off at 25 due to the repetition above. Might need tweaking for large instructions.
			const int cutoff = 50;
			for (int i = 0; i < std::min((int)lines.size(), cutoff); i++) {
				printf("%s\n", lines[i].c_str());
			}
			if (lines.size() > cutoff)
				printf("...\n");
		}
		printf("Jit was %fx faster than interp, IR was %fx faster.\n\n", jit_speed / interp_speed, ir_speed / interp_speed);
	}

	printf("\n");

	DestroyJitHarness();

	return memoryClean && jit_speed >= interp_speed;
}

// Times the JIT on the VFPU dot product shapes games use most, for working on their routines. Prints
// nanoseconds per dot product; the operands are ordinary floats. Each group stores its results, or
// the IR would drop all but the last as dead.
bool TestJitDotBench() {
	SetupJitHarness();
	g_Config.bFastMemory = true;

	struct Sequence {
		const char *name;
		int dots;
		std::vector<const char *> lines;
	};
	const Sequence sequences[] = {
		{ "vdot.q alone", 4, { "vdot.q S000, C100, C200", "vdot.q S001, C110, C210", "vdot.q S002, C120, C220", "vdot.q S003, C130, C230", "sv.q C000, 0(a0)" } },
		{ "vtfm4.q", 4, { "vtfm4.q C000, M100, C200", "sv.q C000, 0(a0)" } },
		{ "vmmul.q", 16, { "vmmul.q M000, M100, M200", "sv.q C000, 0(a0)", "sv.q C010, 16(a0)", "sv.q C020, 32(a0)", "sv.q C030, 48(a0)" } },
		{ "vdot.t run", 4, { "vdot.t S300, C400, C000", "vdot.t S301, C400, C010", "vdot.t S302, C400, C020", "vdot.t S303, C400, C030", "sv.q C300, 0(a0)" } },
		{ "vdot.q rows", 3, { "vdot.q S200, R100, C000", "vdot.q S201, R101, C000", "vdot.q S202, R102, C000", "sv.q C200, 0(a0)" } },
	};

	bool ok = true;
	for (const Sequence &seq : sequences) {
		currentMIPS->pc = PSP_GetUserMemoryBase();
		currentMIPS->r[MIPS_REG_A0] = PSP_GetUserMemoryBase() + 0x100000;
		u32 addr = currentMIPS->pc;
		const int groups = 50;
		for (int i = 0; i < groups && ok; ++i) {
			for (const char *line : seq.lines) {
				std::string error;
				if (!MipsAssembleOpcode(line, currentDebugMIPS, addr, &error)) {
					printf("ERROR: %s: %s\n", line, error.c_str());
					ok = false;
				}
				addr += 4;
			}
		}
		Memory::WriteUnchecked_U32(MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator"), addr);
		Memory::WriteUnchecked_U32(MIPS_MAKE_BREAK(1), addr + 4);
		Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), addr + 8);

		for (int i = 0; i < 128; ++i)
			currentMIPS->v[i] = 0.75f + (float)((i * 37) % 101) * 0.37f * ((i & 1) ? -1.0f : 1.0f);
		mipsr4k.UpdateCore(CPUCore::JIT);
		const double callsPerSecond = ExecCPUTest(false);
		if (getenv("PPSSPP_JITBENCH_DISASM")) {
			JitBlockDebugInfo block = mipsr4k.jit->GetBlockCacheDebugInterface()->GetBlockDebugInfo(0);
			for (size_t i = 0; i < block.targetDisasm.size() && i < 80; i++)
				printf("  %s\n", block.targetDisasm[i].c_str());
		}
		mipsr4k.jit->ClearCache();
		printf("%-14s %6.2f ns per dot\n", seq.name, 1e9 / (callsPerSecond * groups * seq.dots));
	}

	DestroyJitHarness();
	return ok;
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

	// The disassembly shows the hook, with the instruction under it.
	char disasm[256];
	DisAsm(func, disasm, sizeof(disasm));
	EXPECT_TRUE(strstr(disasm, "replacement") != nullptr && strstr(disasm, "addiu") != nullptr);
	DisAsm(func + 4, disasm, sizeof(disasm));
	EXPECT_TRUE(strstr(disasm, "replacement") == nullptr);

	static const CPUCore cores[] = {
		CPUCore::JIT,
		CPUCore::IR_INTERPRETER,
	};
	for (CPUCore core : cores) {
		mipsr4k.UpdateCore(core);
		const u32 hook = mipsr4k.jit->GetBlockShadowHookValue();
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), hook);

		// Compiling replaces the hook value with the block's entry.
		compile();
		const u32 block = Memory::ReadBlockShadow(func);
		EXPECT_TRUE(block != 0 && block != hook);

		// Destroying the block puts it back, whichever way it goes.
		mipsr4k.jit->InvalidateCacheAt(func, 4);
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), hook);
		compile();
		mipsr4k.jit->ClearCache();
		EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), mipsr4k.jit->GetBlockShadowHookValue());

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
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(func), mipsr4k.jit->GetBlockShadowHookValue());
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

// eret goes to EPC, or ErrorEPC with Status.ERL set, on every core, and ends the block: what follows
// it doesn't run.
bool TestCPUEret() {
	SetupJitHarness();

	const u32 base = PSP_GetUserMemoryBase();
	const u32 target = base + 0x100;
	const u32 terminator = MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator");
	Memory::WriteUnchecked_U32(MIPS_MAKE_ADDIU(MIPS_REG_V0, MIPS_REG_ZERO, 1), base);
	Memory::WriteUnchecked_U32(0x42000018, base + 4);  // eret
	Memory::WriteUnchecked_U32(MIPS_MAKE_ADDIU(MIPS_REG_V0, MIPS_REG_ZERO, 2), base + 8);
	Memory::WriteUnchecked_U32(terminator, base + 12);
	Memory::WriteUnchecked_U32(MIPS_MAKE_ADDIU(MIPS_REG_V1, MIPS_REG_ZERO, 3), target);
	Memory::WriteUnchecked_U32(terminator, target + 4);
	Memory::WriteUnchecked_U32(MIPS_MAKE_BREAK(1), target + 8);

	static const CPUCore cores[] = {
		CPUCore::INTERPRETER,
		CPUCore::IR_INTERPRETER,
		CPUCore::JIT,
	};
	bool success = true;
	for (CPUCore core : cores) {
		mipsr4k.UpdateCore(core);
		for (bool erl : { false, true }) {
			if (mipsr4k.jit)
				mipsr4k.jit->ClearCache();
			currentMIPS->r[MIPS_REG_V0] = 0;
			currentMIPS->r[MIPS_REG_V1] = 0;
			currentMIPS->cop0[12] = erl ? 0x6 : 0x2;
			currentMIPS->cop0[14] = erl ? base + 8 : target;
			currentMIPS->cop0[30] = erl ? target : base + 8;
			currentMIPS->pc = base;
			coreState = CORE_RUNNING_CPU;
			while (coreState == CORE_RUNNING_CPU)
				mipsr4k.RunLoopUntil(CoreTiming::GetTicks(currentMIPS) + 1000000);

			const u32 status = erl ? 0x2 : 0x0;
			if (currentMIPS->r[MIPS_REG_V0] != 1 || currentMIPS->r[MIPS_REG_V1] != 3 || currentMIPS->cop0[12] != status) {
				printf("eret on core %d (ERL %d): v0=%d v1=%d status=%08x, expected 1, 3, %08x\n", (int)core, (int)erl,
					currentMIPS->r[MIPS_REG_V0], currentMIPS->r[MIPS_REG_V1], currentMIPS->cop0[12], status);
				success = false;
			}
		}
	}
	mipsr4k.UpdateCore(CPUCore::INTERPRETER);

	DestroyJitHarness();
	return success;
}

// Kernel code reaching the GPIO block's registers on every core: a write, an acknowledge clearing a
// status bit, reads back through lw and lwc1, and poison from a register nothing handles. Kernel
// RAM too, at a constant address (which the IR makes a plain access) and through a register. Then
// the registers through a savestate.
bool TestMMIO() {
	SetupJitHarness();
	MMIO::Init();

	// Kernel code, which may touch hardware registers.
	const u32 base = 0x88000100;
	auto sw = [](int rt, int rs, int imm) { return (0x2Bu << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); };
	auto lwc1 = [](int ft, int rs, int imm) { return (0x31u << 26) | (rs << 21) | (ft << 16) | (imm & 0xFFFF); };
	auto sb = [](int rt, int rs, int imm) { return (0x28u << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); };
	auto lb = [](int rt, int rs, int imm) { return (0x20u << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); };
	const u32 ram = 0x88001000;
	const u32 code[] = {
		MIPS_MAKE_LUI(MIPS_REG_T0, 0xBE24),  // GPIO
		MIPS_MAKE_ORI(MIPS_REG_T1, MIPS_REG_ZERO, 0x30),
		sw(MIPS_REG_T1, MIPS_REG_T0, 0x20),  // Interrupt status.
		MIPS_MAKE_ORI(MIPS_REG_T1, MIPS_REG_ZERO, 0x10),
		sw(MIPS_REG_T1, MIPS_REG_T0, 0x24),  // Acknowledge, clears the bit.
		MIPS_MAKE_LW(MIPS_REG_V0, MIPS_REG_T0, 0x20),
		lwc1(0, MIPS_REG_T0, 0x20),
		MIPS_MAKE_LUI(MIPS_REG_T2, 0xBC10),
		MIPS_MAKE_LW(MIPS_REG_V1, MIPS_REG_T2, 0),
		MIPS_MAKE_LUI(MIPS_REG_T3, ram >> 16),
		sw(MIPS_REG_T1, MIPS_REG_T3, ram & 0xFFFF),
		MIPS_MAKE_LW(MIPS_REG_A0, MIPS_REG_T3, ram & 0xFFFF),
		MIPS_MAKE_ORI(MIPS_REG_T4, MIPS_REG_ZERO, 0x80),
		sb(MIPS_REG_T4, MIPS_REG_A1, 4),  // a1 is ram, unknown to the IR.
		lb(MIPS_REG_A2, MIPS_REG_A1, 4),
		MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator"),
		MIPS_MAKE_BREAK(1),
	};
	for (size_t i = 0; i < ARRAY_SIZE(code); ++i)
		Memory::WriteUnchecked_U32(code[i], base + (u32)i * 4);

	static const CPUCore cores[] = {
		CPUCore::INTERPRETER,
		CPUCore::IR_INTERPRETER,
		CPUCore::JIT,
	};
	bool success = true;
	for (CPUCore core : cores) {
		mipsr4k.UpdateCore(core);
		MMIO::Init();
		currentMIPS->r[MIPS_REG_V0] = 0;
		currentMIPS->r[MIPS_REG_V1] = 0;
		currentMIPS->r[MIPS_REG_A0] = 0;
		currentMIPS->r[MIPS_REG_A1] = ram;
		currentMIPS->r[MIPS_REG_A2] = 0;
		currentMIPS->fi[0] = 0;
		currentMIPS->pc = base;
		coreState = CORE_RUNNING_CPU;
		while (coreState == CORE_RUNNING_CPU)
			mipsr4k.RunLoopUntil(CoreTiming::GetTicks(currentMIPS) + 1000000);

		if (currentMIPS->r[MIPS_REG_V0] != 0x20 || currentMIPS->fi[0] != 0x20 || currentMIPS->r[MIPS_REG_V1] != MMIO::UNHANDLED_READ ||
			currentMIPS->r[MIPS_REG_A0] != 0x10 || currentMIPS->r[MIPS_REG_A2] != 0xFFFFFF80) {
			printf("MMIO on core %d: v0=%08x f0=%08x v1=%08x a0=%08x a2=%08x, expected 00000020, 00000020, %08x, 00000010, ffffff80\n", (int)core,
				currentMIPS->r[MIPS_REG_V0], currentMIPS->fi[0], currentMIPS->r[MIPS_REG_V1], currentMIPS->r[MIPS_REG_A0], currentMIPS->r[MIPS_REG_A2], MMIO::UNHANDLED_READ);
			success = false;
		}

		if (mipsr4k.jit) {
			// The registers stay checked accesses, the constant RAM ones become plain.
			JitBlockCacheDebugInterface *blocks = mipsr4k.jit->GetBlockCacheDebugInterface();
			const JitBlockDebugInfo info = blocks->GetBlockDebugInfo(blocks->GetBlockNumberFromStartAddress(base));
			int kernelOps = 0, plainOps = 0;
			for (const std::string &line : info.irDisasm) {
				if (line.find("LoadKernel") != std::string::npos || line.find("StoreKernel") != std::string::npos)
					kernelOps++;
				else if (line.find("Load32 ") != std::string::npos || line.find("Store32 ") != std::string::npos)
					plainOps++;
			}
			// sw/sw/lw/lw to registers, sb/lb through a1. lwc1 is left to the interpreter. The RAM store
			// is plain, and the load after it plain too, or gone (the value forwarded).
			if (kernelOps != 6 || plainOps < 1) {
				printf("MMIO on core %d: %d kernel and %d plain loads/stores, expected 6 and at least 1\n", (int)core, kernelOps, plainOps);
				for (const std::string &line : info.irDisasm)
					printf("  %s\n", line.c_str());
				success = false;
			}
		}
	}
	mipsr4k.UpdateCore(CPUCore::INTERPRETER);

	struct MMIOState {
		void DoState(PointerWrap &p) { MMIO::DoState(p); }
	} state;
	std::vector<u8> saved;
	EXPECT_EQ_INT(CChunkFileReader::MeasureAndSavePtr(state, &saved), CChunkFileReader::ERROR_NONE);
	MMIO::Init();
	std::string error;
	EXPECT_EQ_INT(CChunkFileReader::LoadPtr(saved.data(), saved.size(), state, &error), CChunkFileReader::ERROR_NONE);
	currentMIPS->pc = base;
	EXPECT_EQ_HEX(MMIO::Read(currentMIPS, 0xBE240020, 4), 0x20);

	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

// The Media Engine's address space: the main CPU's RAM and boot page, its own eDRAM (with its mirrors)
// where the main CPU has its scratchpad, its own memory where the main CPU has VRAM, its own block
// shadow. Then the interpreter running a
// second CPU in it.
bool TestMESpace() {
	Memory::g_PSPModel = PSP_MODEL_FAT;
	SetupJitHarness();
	MMIO::Init();
	bool success = true;

	Memory::AddressSpace *space = Memory::CreateMESpace();
	if (!space) {
		printf("No Media Engine address space on this platform, skipping\n");
		MMIO::Shutdown();
		DestroyJitHarness();
		return true;
	}
	EXPECT_EQ_HEX(space->edramSize, 0x00200000);

	Memory::WriteUnchecked_U32(0x12345678, 0x08800000);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0x88800000), 0x12345678);
	// Physical memory, uncached, through 0xA0000000.
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0xA8800000), 0x12345678);
	// Not VRAM: memory of its own, the same through 0x44000000 and 0xA4000000.
	Memory::WriteUnchecked_U32(0x0BADF00D, 0x04000100);
	space->WriteUnchecked_U32(0xFEEDF00D, 0xA4000100);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0x44000100), 0xFEEDF00D);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x44000100), 0x0BADF00D);
	// Without the depth layout the software renderer gives VRAM's mirrors.
	const GPUCore gpuCore = PSP_CoreParameter().gpuCore;
	PSP_CoreParameter().gpuCore = GPUCORE_SOFTWARE;
	space->WriteUnchecked_U32(0, 0x04200100);
	space->WriteUnchecked_U16(0x1234, 0x04200102);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0x04200100), 0x12340000);
	EXPECT_EQ_HEX(space->ReadUnchecked_U16(0x04200102), 0x1234);
	PSP_CoreParameter().gpuCore = gpuCore;
	EXPECT_TRUE(space->IsValid4AlignedAddress(0xA8800000));
	EXPECT_FALSE(Memory::g_mainSpace.IsValid4AlignedAddress(0xA8800000));

	space->WriteUnchecked_U32(0xCAFE0001, 0x00010000);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0x80010000), 0xCAFE0001);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0xA0010000), 0xCAFE0001);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x00010000), 0);

	// The main CPU reaches the boot page through MMIO.
	MMIO::Write(currentMIPS, 0xBFC00600, 4, 0x187);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0xBFC00600), 0x187);
	space->WriteUnchecked_U32(0xFFFFFFFE, 0xBFC00700);
	EXPECT_EQ_HEX(MMIO::Read(currentMIPS, 0xBFC00700, 4), 0xFFFFFFFE);

	EXPECT_TRUE(space->IsValid4AlignedAddress(0x00000000));
	EXPECT_TRUE(space->IsValid4AlignedAddress(0xA01FFFFC));
	EXPECT_FALSE(space->IsValid4AlignedAddress(0x00200000));
	EXPECT_FALSE(space->IsValid4AlignedAddress(0x40010000));
	EXPECT_FALSE(space->IsValid4AlignedAddress(0x00000002));
	EXPECT_TRUE(space->IsValid4AlignedAddress(0xBFC00FFC));
	EXPECT_FALSE(space->IsValid4AlignedAddress(0xBFC01000));
	EXPECT_TRUE(space->IsValid4AlignedAddress(0x88300000));
	EXPECT_FALSE(Memory::g_mainSpace.IsValid4AlignedAddress(0x00000000));
	EXPECT_FALSE(Memory::g_mainSpace.IsValid4AlignedAddress(0xBFC00000));

	space->WriteBlockShadow(0x08800000, 5);
	EXPECT_EQ_HEX(space->ReadBlockShadow(0x88800000), 5);
	EXPECT_EQ_HEX(Memory::ReadBlockShadow(0x08800000), 0);
	space->WriteBlockShadow(0x00000100, 6);
	EXPECT_EQ_HEX(space->ReadBlockShadow(0x80000100), 6);
	space->WriteBlockShadow(0x08800000, 0);
	space->WriteBlockShadow(0x00000100, 0);

	// From eDRAM: copy a word from shared RAM into eDRAM, and read it back through the uncached mirror.
	auto sw = [](int rt, int rs, int imm) { return (0x2Bu << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); };
	const u32 code[] = {
		MIPS_MAKE_LUI(MIPS_REG_T0, 0x0880),
		MIPS_MAKE_LW(MIPS_REG_V0, MIPS_REG_T0, 0),
		sw(MIPS_REG_V0, MIPS_REG_ZERO, 0x200),
		MIPS_MAKE_LUI(MIPS_REG_T1, 0xA000),
		MIPS_MAKE_LW(MIPS_REG_V1, MIPS_REG_T1, 0x200),
	};
	for (size_t i = 0; i < ARRAY_SIZE(code); ++i)
		space->WriteUnchecked_U32(code[i], 0x80000100 + (u32)i * 4);
	{
		MIPSState me;
		me.space = space;
		me.InitRegisters();
		me.pc = 0x80000100;
		for (size_t i = 0; i < ARRAY_SIZE(code); ++i)
			MIPS_InterpretSingleStep(&me);
		EXPECT_EQ_HEX(me.pc, 0x80000114);
		EXPECT_EQ_HEX(me.r[MIPS_REG_V1], 0x12345678);
		EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x00010000), 0);
	}

	Memory::DestroyMESpace();
	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

static bool TestMeCoreOn(CPUCore core) {
	Memory::g_PSPModel = PSP_MODEL_FAT;
	SetupJitHarness();
	PSP_CoreParameter().cpuCore = core;
	MMIO::Init();
	bool success = true;

	if (!MeCore::Init()) {
		printf("No Media Engine on this platform, skipping\n");
		MMIO::Shutdown();
		DestroyJitHarness();
		return true;
	}
	MeCore::MEState *me = MeCore::g_meState;
	EXPECT_EQ_INT(me->cpu.jit != nullptr, core != CPUCore::INTERPRETER);

	// The classic homebrew ME code at 0xBFC00040 (pspsdk me/basic): count at 0xBFC00060, forever.
	const u32 code[] = {
		MIPS_MAKE_LUI(MIPS_REG_T1, 0xBFC0),
		MIPS_MAKE_LW(MIPS_REG_T0, MIPS_REG_T1, 0x60),
		MIPS_MAKE_ADDIU(MIPS_REG_T0, MIPS_REG_T0, 1),
		(0x2Bu << 26) | (MIPS_REG_T1 << 21) | (MIPS_REG_T0 << 16) | 0x60,  // sw t0, 0x60(t1)
		MIPS_MAKE_B(-4),
		MIPS_MAKE_NOP(),
	};
	u8 *page = Memory::GetBootPage();
	memcpy(page + 0x40, code, sizeof(code));
	auto counter = [&]() { return *(u32_le *)(page + 0x60); };

	// Held in reset, its clock still runs.
	MeCore::RunFor(1000);
	EXPECT_EQ_INT(counter(), 0);
	EXPECT_EQ_INT((int)MeCore::GetTicks(), 1000);

	// Released, through the reset stub to 0x40.
	MeCore::SetReset(false);
	EXPECT_EQ_HEX(me->cpu.pc, 0xBFC00000);
	MeCore::RunFor(1000);
	const u32 count = counter();
	EXPECT_TRUE(count > 100);
	EXPECT_TRUE(me->cpu.pc >= 0xBFC00040 && me->cpu.pc < 0xBFC00058);
	EXPECT_TRUE(MeCore::GetTicks() >= 2000 && MeCore::GetTicks() < 2010);

	// Saved and loaded.
	struct State {
		void DoState(PointerWrap &p) {
			CoreTiming::DoState(p);
			MeCore::DoState(p);
		}
	} state;
	std::vector<u8> saved;
	me->cpu.space->WriteUnchecked_U32(0xCAFE0001, 0x80001000);
	const u32 pc = me->cpu.pc;
	EXPECT_EQ_INT(CChunkFileReader::MeasureAndSavePtr(state, &saved), CChunkFileReader::ERROR_NONE);
	me->cpu.space->WriteUnchecked_U32(0, 0x80001000);
	MeCore::RunFor(100);
	std::string error;
	EXPECT_EQ_INT(CChunkFileReader::LoadPtr(saved.data(), saved.size(), state, &error), CChunkFileReader::ERROR_NONE);
	me = MeCore::g_meState;
	EXPECT_EQ_HEX(me->cpu.pc, pc);
	EXPECT_EQ_HEX(me->cpu.space->ReadUnchecked_U32(0x80001000), 0xCAFE0001);

	// COP0's control registers, which the ME's exception code keeps state in.
	me->cpu.r[MIPS_REG_V0] = 0x12345678;
	me->cpu.space->WriteUnchecked_U32(0x40C27000, 0x80002000);  // ctc0 v0, $14
	me->cpu.space->WriteUnchecked_U32(0x40437000, 0x80002004);  // cfc0 v1, $14
	me->cpu.pc = 0x80002000;
	MIPS_InterpretSingleStep(&me->cpu);
	MIPS_InterpretSingleStep(&me->cpu);
	EXPECT_EQ_HEX(me->cpu.r[MIPS_REG_V1], 0x12345678);

	// Back in reset, it stops.
	MeCore::SetReset(true);
	const u32 stopped = counter();
	MeCore::RunFor(1000);
	EXPECT_EQ_INT(counter(), stopped);

	MeCore::Shutdown();
	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

bool TestMeCore() {
	// The ME runs on the interpreter, the IR interpreter, or the native JIT where it can.
	return TestMeCoreOn(CPUCore::INTERPRETER) && TestMeCoreOn(CPUCore::IR_INTERPRETER) && TestMeCoreOn(CPUCore::JIT);
}

// The main CPU releases the ME, which waits in halt for the main CPU's soft interrupt, and answers
// each one with its own.
static bool TestMeInterruptsOn(CPUCore core) {
	Memory::g_PSPModel = PSP_MODEL_FAT;
	SetupJitHarness();
	PSP_CoreParameter().cpuCore = core;
	MMIO::Init();
	bool success = true;

	if (!MeCore::Init()) {
		printf("No Media Engine on this platform, skipping\n");
		MMIO::Shutdown();
		DestroyJitHarness();
		return true;
	}
	MeCore::MEState *me = MeCore::g_meState;
	EXPECT_EQ_INT(me->cpu.jit != nullptr, core != CPUCore::INTERPRETER);

	auto assemble = [&](u32 addr, const std::vector<const char *> &lines) {
		for (const char *line : lines) {
			std::string error;
			if (!MipsAssembleOpcode(line, currentDebugMIPS, addr, &error)) {
				printf("ERROR: %s: %s\n", line, error.c_str());
				success = false;
			}
			addr += 4;
		}
	};
	// Shared RAM, which the ME runs from too.
	const u32 code = 0x08900000;
	assemble(code, {
		"lui k0, 0x0890",
		"addiu k0, k0, 0x100",
		"mtc0 k0, $25",          // EBase: the handler
		"lui k1, 0xBC30",
		"lui k0, 0x8000",
		"sw k0, 8(k1)",          // the soft interrupt reaches the ME
		"ori k0, zero, 0x401",
		"mtc0 k0, $12",          // IM2, IE
		"nop",
		"nop",                   // 0x08900024: halt, which armips doesn't know
		"j 0x08900024",
		"nop",
	});
	assemble(code + 0x100, {
		"lui k1, 0xBC30",
		"lui k0, 0x8000",
		"sw k0, 0(k1)",          // acknowledged
		"lui k1, 0x0890",
		"lw k0, 0x200(k1)",
		"addiu k0, k0, 1",
		"sw k0, 0x200(k1)",
		"lui k1, 0xBC10",
		"ori k0, zero, 1",
		"sw k0, 0x44(k1)",       // interrupts the main CPU
		"eret",
	});
	Memory::WriteUnchecked_U32(0x70000000, code + 0x24);
	// From the reset vector's 0x40.
	u8 *page = Memory::GetBootPage();
	const u32 jump[] = { MIPS_MAKE_LUI(MIPS_REG_T0, 0x0890), 0x01000008 /* jr t0 */, MIPS_MAKE_NOP() };
	memcpy(page + 0x40, jump, sizeof(jump));
	auto counter = [&]() { return Memory::ReadUnchecked_U32(code + 0x200); };

	MMIO::Write(&mipsr4k, 0xBC10004C, 4, 0x04);
	MMIO::Write(&mipsr4k, 0xBC10004C, 4, 0x00);
	EXPECT_FALSE(me->inReset);
	MeCore::RunFor(2000);
	EXPECT_TRUE(me->halted);
	EXPECT_EQ_HEX(me->cpu.pc, code + 0x28);
	EXPECT_EQ_INT(counter(), 0);

	for (int i = 1; i <= 2; ++i) {
		MMIO::Write(&mipsr4k, 0xBC100044, 4, 1);
		MeCore::RunFor(2000);
		EXPECT_EQ_INT(counter(), i);
		EXPECT_TRUE(me->halted);
		EXPECT_EQ_HEX(me->intcRaised, 0);
		EXPECT_EQ_HEX(me->cpu.cop0[12] & 2, 0);
	}
	EXPECT_TRUE(CoreTiming::GetScheduledEventsSummary().find("MeIntr") != std::string::npos);

	// The mutex: each CPU only gets it while the other doesn't have it.
	MMIO::Write(&mipsr4k, 0xBC100048, 4, 1);
	MMIO::Write(&me->cpu, 0xBC100048, 4, 2);
	EXPECT_EQ_HEX(MMIO::Read(&me->cpu, 0xBC100048, 4), 1);
	MMIO::Write(&mipsr4k, 0xBC100048, 4, 0);
	MMIO::Write(&me->cpu, 0xBC100048, 4, 2);
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC100048, 4), 2);

	// The main CPU has its own interrupt controller at the same address.
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC300008, 4), MMIO::UNHANDLED_READ);
	EXPECT_EQ_HEX(MMIO::Read(&me->cpu, 0xBC300008, 4), 0x80000000);

	// Count doesn't run on the ME.
	MeCore::WriteCop0(9, 1234);
	MeCore::RunFor(1000);
	EXPECT_EQ_INT(MeCore::ReadCop0(9), 1234);
	EXPECT_TRUE(me->halted);

	// Reset again.
	MMIO::Write(&mipsr4k, 0xBC10004C, 4, 0x04);
	EXPECT_TRUE(me->inReset);

	MeCore::Shutdown();
	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

bool TestMeInterrupts() {
	// The ME runs on the interpreter, the IR interpreter, or the native JIT where it can.
	return TestMeInterruptsOn(CPUCore::INTERPRETER) && TestMeInterruptsOn(CPUCore::IR_INTERPRETER) && TestMeInterruptsOn(CPUCore::JIT);
}

// The ME skips the rest of a slice spent polling for the main CPU, and only then.
static bool TestMeWaitOn(CPUCore core) {
	Memory::g_PSPModel = PSP_MODEL_FAT;
	SetupJitHarness();
	PSP_CoreParameter().cpuCore = core;
	MMIO::Init();
	bool success = true;

	if (!MeCore::Init()) {
		printf("No Media Engine on this platform, skipping\n");
		MMIO::Shutdown();
		DestroyJitHarness();
		return true;
	}
	MeCore::MEState *me = MeCore::g_meState;

	const u32 code = 0x08900000;
	const u32 flag = code + 0x200;
	const u32 marker = code + 0x204;
	const u32 heartbeat = code + 0x208;
	const char *lines[] = {
		"lui t0, 0x0890",
		"lw t1, 0x200(t0)",      // 0x08900004: waits for the flag
		"beq t1, zero, 0x08900004",
		"nop",
		"ori t2, zero, 0x1234",
		"sw t2, 0x204(t0)",
		"lui t3, 0x0010",        // then counts down, which isn't waiting
		"addiu t3, t3, -1",      // 0x0890001C
		"bne t3, zero, 0x0890001C",
		"nop",
		"lw t4, 0x208(t0)",      // 0x08900028: and beats, which isn't either
		"addiu t4, t4, 1",
		"sw t4, 0x208(t0)",
		"j 0x08900028",
		"nop",
	};
	u32 addr = code;
	for (const char *line : lines) {
		std::string error;
		if (!MipsAssembleOpcode(line, currentDebugMIPS, addr, &error)) {
			printf("ERROR: %s: %s\n", line, error.c_str());
			success = false;
		}
		addr += 4;
	}
	const u32 jump[] = { MIPS_MAKE_LUI(MIPS_REG_T0, 0x0890), 0x01000008 /* jr t0 */, MIPS_MAKE_NOP() };
	memcpy(Memory::GetBootPage() + 0x40, jump, sizeof(jump));

	MeCore::SetReset(false);
	MeCore::RunFor(100000);
	EXPECT_TRUE(me->waiting);
	EXPECT_EQ_HEX(me->cpu.pc, code + 4);
	EXPECT_TRUE(MeCore::GetTicks() >= 100000 && MeCore::GetTicks() < 100010);

	Memory::WriteUnchecked_U32(1, flag);
	MeCore::RunFor(100000);
	EXPECT_FALSE(me->waiting);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(marker), 0x1234);
	const u32 left = me->cpu.r[MIPS_REG_T3];
	EXPECT_TRUE(left > 0 && left < 0x100000 - 1000);

	MeCore::RunFor(10000000);
	EXPECT_EQ_INT(me->cpu.r[MIPS_REG_T3], 0);
	EXPECT_FALSE(me->waiting);
	EXPECT_TRUE(Memory::ReadUnchecked_U32(heartbeat) > 1000);

	MeCore::Shutdown();
	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

bool TestMeWait() {
	return TestMeWaitOn(CPUCore::INTERPRETER) && TestMeWaitOn(CPUCore::IR_INTERPRETER) && TestMeWaitOn(CPUCore::JIT);
}

// DMACplus moves data between the ME's eDRAM and main memory, following linked list items.
bool TestMeDmacplus() {
	Memory::g_PSPModel = PSP_MODEL_FAT;
	SetupJitHarness();
	MMIO::Init();
	bool success = true;

	if (!MeCore::Init()) {
		printf("No Media Engine on this platform, skipping\n");
		MMIO::Shutdown();
		DestroyJitHarness();
		return true;
	}
	const Memory::AddressSpace *space = MeCore::g_meState->cpu.space;
	for (u32 i = 0; i < 0x2000; i += 4)
		space->WriteUnchecked_U32(0xA0000000 | i, 0x80000000 + i);

	// ME to main CPU (channel 5): 0x1000 bytes in 16-byte units, then 0x1000 more from the list.
	auto control = [](bool srcME, bool dstME, u32 widthBit, u32 count) {
		return (dstME ? 1U << 25 : 0) | (srcME ? 1U << 24 : 0) | (widthBit << 21) | (widthBit << 18) | (1U << 15) | (1U << 12) | count;
	};
	const u32 lli = 0x08900000;
	const u32 item[5] = { 0x1000, 0x08A01000, 0, control(true, false, 2, 0x400), 1 };
	for (int i = 0; i < 5; ++i)
		Memory::WriteUnchecked_U32(item[i], lli + i * 4);
	MMIO::Write(&mipsr4k, 0xBC8001B0, 4, 0);
	MMIO::Write(&mipsr4k, 0xBC8001A0, 4, 0);
	MMIO::Write(&mipsr4k, 0xBC8001A4, 4, 0x48A00000);
	MMIO::Write(&mipsr4k, 0xBC8001A8, 4, lli);
	MMIO::Write(&mipsr4k, 0xBC8001AC, 4, control(true, false, 4, 0x100) | 0x80000000);
	MMIO::Write(&mipsr4k, 0xBC8001B0, 4, 1);
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC8001B0, 4), 0);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x08A00000), 0xA0000000);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x08A01FFC), 0xA0001FFC);
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC800004, 4), 1 << 5);
	MMIO::Write(&mipsr4k, 0xBC800008, 4, 1 << 5);
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC800004, 4), 0);

	// And back (channel 4), to further into the eDRAM.
	MMIO::Write(&mipsr4k, 0xBC800180, 4, 0x08A00000);
	MMIO::Write(&mipsr4k, 0xBC800184, 4, 0x10000);
	MMIO::Write(&mipsr4k, 0xBC800188, 4, 0);
	MMIO::Write(&mipsr4k, 0xBC80018C, 4, control(false, true, 2, 0x800));
	MMIO::Write(&mipsr4k, 0xBC800190, 4, 1);
	EXPECT_EQ_HEX(space->ReadUnchecked_U32(0x80011FFC), 0xA0001FFC);

	// Colour space conversion: a planar (VME) 32x32 grey frame to 8888, with the default matrix.
	for (u32 i = 0; i < 32 * 32; ++i)
		space->WriteUnchecked_U8(128, 0x80020000 + i);
	for (u32 i = 0; i < 16 * 16 * 2; ++i)
		space->WriteUnchecked_U8(128, 0x80030000 + i);
	const u32 csc[] = {
		0x120, 0x20000, 0x128, 0x20000 + 32 * 16, 0x130, 0x30000, 0x134, 0x30000 + 16 * 8,
		0x138, 0x30000 + 16 * 16, 0x13C, 0x30000 + 16 * 24,
		0x140, (2 << 16) | (2 << 8) | (1 << 1) | 1,
		0x144, 0x48B00000, 0x148, 0x48B00000 + 64 * 16 * 4,
		0x14C, (64 << 8) | 1,
		0x150, (0x0CC << 20) | 0x095, 0x154, (0x398 << 20) | (0x3CE << 10) | 0x095, 0x158, (0x102 << 10) | 0x095,
	};
	for (size_t i = 0; i < ARRAY_SIZE(csc); i += 2)
		MMIO::Write(&mipsr4k, 0xBC800000 + csc[i], 4, csc[i + 1]);
	MMIO::Write(&mipsr4k, 0xBC800160, 4, 1);
	EXPECT_EQ_HEX(MMIO::Read(&mipsr4k, 0xBC800160, 4) & 1, 0);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x08B00000), 0x828282);
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x08B00000 + 64 * 31 * 4 + 31 * 4), 0x828282);

	MeCore::Shutdown();
	MMIO::Shutdown();
	DestroyJitHarness();
	return success;
}

// Differential fuzzing of the CPU cores. Random straight-line code (ALU, FPU, VFPU, loads and
// stores) runs from a random register state on each core, and every core has to end up with the
// interpreter's state, NaN payloads aside.
//
// Environment variables:
//   PPSSPP_CPUFUZZ_COUNT    number of cases (default 2000)
//   PPSSPP_CPUFUZZ_SEED     first case's seed (default 1). Case i always uses seed + i.
//   PPSSPP_CPUFUZZ_DUMP     file to write every core's end state of every case to, for diffing
//                           the results of different host architectures.
//   PPSSPP_CPUFUZZ_CLASSES  instruction classes to generate, as a weighted string of "aifvm" (alu,
//                           imm, fpu, vfpu, memory).
//   PPSSPP_CPUFUZZ_VERBOSE  print each case before running it, to find one that crashes.
//   PPSSPP_CPUFUZZ_CODE     comma-separated hex instructions to run in place of the generated
//                           code, from the generated start state.
//   PPSSPP_CPUFUZZ_CODE_SEED  apply PPSSPP_CPUFUZZ_CODE to this seed only.
//   PPSSPP_CPUFUZZ_SKIP     cores not to run, by digit: 1 IR interpreter, 2 JIT, 3 JIT IR.
//   PPSSPP_CPUFUZZ_REPORTS  how many differences to minimize and print (default 20).
//   PPSSPP_CPUFUZZ_HOSTEXT  + or -: turn the host's optional extensions on or off for the JIT
//                           (RISC-V Zba/Zbb/Zbs/Zcb/Zfa/Zfh/Zicond, and V with -, LoongArch
//                           LSX/LASX). VFPUDotJit follows it too.
//   PPSSPP_CPUFUZZ_UNKNOWNPREFIX  compile as after a block left a prefix uneaten. The first case
//                           that leaves one does this for the rest of the run, so this reproduces
//                           a later case on its own.
//   PPSSPP_CPUFUZZ_STRICT   fail the test on any difference, not only on a crash.
//   PPSSPP_CPUFUZZ_BLOCKS   print the IR and host code of every block the JITs ran, after each case.
//   PPSSPP_CPUFUZZ_OPS      comma-separated instruction name prefixes to generate only, for fast
//                           runs on the instructions being worked on. "dot" is the dot products
//                           and the prefixes. Without PPSSPP_CPUFUZZ_CLASSES, the class is "v"
//                           ("vr" for "dot": r is a run of vdots the IR batches). b adds forward
//                           conditional branches.

namespace {

struct FuzzRng {
	u64 s;
	explicit FuzzRng(u64 seed) : s(seed) {
		// Otherwise seed + 1 gives the same stream, one step on.
		s = Next64();
	}
	u64 Next64() {
		u64 z = (s += 0x9E3779B97F4A7C15ULL);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return z ^ (z >> 31);
	}
	u32 Next() { return (u32)(Next64() >> 32); }
	u32 Below(u32 n) { return (u32)(((u64)Next() * n) >> 32); }
	bool Chance(u32 percent) { return Below(100) < percent; }
};

struct FuzzState {
	u32 r[32];
	u32 f[32];
	u32 v[128];
	u32 vfpuCtrl[16];
	u32 hi, lo, fcr31, fpcond;
	int llBit;
	u8 mem[1024];
	u32 cycles;
};

// The loads and stores hit scratch memory through k0, which points at its middle.
static const u32 FUZZ_CODE_OFFSET = 0;
static const u32 FUZZ_MEM_OFFSET = 0x10000;
static const int FUZZ_BASE_REG = MIPS_REG_K0;
static const int FUZZ_MAX_INSTRS = 24;

static u32 FuzzFloatBits(FuzzRng &rng) {
	static const u32 specials[] = {
		0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x3F000000, 0xBF000000, 0x40000000, 0x3FC00000,
		0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001, 0x7FBFFFFF, 0xFF812345,
		0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000, 0x7F7FFFFF, 0xFF7FFFFF,
		0x4F000000, 0xCF000000, 0x4EFFFFFF, 0xCF000001, 0x4F800000, 0x3EFFFFFF, 0x3F000001, 0x3FBFFFFF,
		0x40400000, 0x3F7FFFFF, 0x34000000, 0x33800000, 0x47000000, 0x46FFFE00, 0x477FFF00, 0x3FC90FDB,
	};
	switch (rng.Below(8)) {
	case 0:
	case 1:
		return specials[rng.Below(ARRAY_SIZE(specials))];
	case 2: {
		// Small integers, some with a half.
		float f = (float)((int)rng.Below(2001) - 1000);
		if (rng.Chance(50)) {
			f += 0.5f;
		}
		u32 bits;
		memcpy(&bits, &f, 4);
		return bits;
	}
	case 3:
		// Denormals.
		return rng.Next() & 0x807FFFFF;
	case 4:
		// Near one.
		return (rng.Next() & 0x80000000) | ((rng.Below(16) + 119) << 23) | (rng.Next() & 0x7FFFFF);
	case 5:
		// Finite, in a wide range.
		return (rng.Next() & 0x80000000) | ((rng.Below(80) + 87) << 23) | (rng.Next() & 0x7FFFFF);
	default:
		return rng.Next();
	}
}

static u32 FuzzIntBits(FuzzRng &rng) {
	static const u32 specials[] = {
		0, 1, 2, 3, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x0000FFFF, 0x00008000,
		0xFFFF8000, 0x000000FF, 0x00000080, 0xFFFFFF80, 31, 32, 33, 0x12345678,
	};
	switch (rng.Below(6)) {
	case 0:
		return specials[rng.Below(ARRAY_SIZE(specials))];
	case 1:
		return rng.Below(64) - 32;
	case 2:
		return FuzzFloatBits(rng);
	default:
		return rng.Next();
	}
}

static bool IsFuzzableName(const char *name) {
	static const char *const denied[] = {
		"cache", "vflush", "sync", "halt", "mfic", "mtic", "rdhwr", "synci", "mfc0", "mtc0", "rdpgpr",
		"mfmc0", "wrpgpr", "tlbr", "tlbwi", "tlbwr", "tlbp", "eret", "iack", "deret", "wait", "break",
		"syscall", "mfc2", "cfc2", "mtc2", "ctc2", "dis.int", "tge", "tgeu", "tlt", "tltu", "teq", "tne",
		"tgei", "tgeiu", "tlti", "tltiu", "teqi", "tnei", "??",
	};
	if (!name || !name[0]) {
		return false;
	}
	for (const char *d : denied) {
		if (!strcmp(name, d)) {
			return false;
		}
	}
	return true;
}

static bool IsVFPUMemOp(u32 op) {
	switch (op >> 26) {
	case 0x32: case 0x35: case 0x36: case 0x3A: case 0x3D: case 0x3E:
		return true;
	default:
		return false;
	}
}

// Points a generated load or store at the scratch memory.
static u32 FixupMemOp(u32 op, FuzzRng &rng) {
	op = (op & ~(0x1F << 21)) | (FUZZ_BASE_REG << 21);
	s32 offset = (s32)rng.Below(512) - 256;
	if (IsVFPUMemOp(op)) {
		const u32 top = op >> 26;
		offset &= (top == 0x36 || top == 0x3E) ? ~15 : ~3;
		return (op & ~0xFFFC) | ((u32)offset & 0xFFFC);
	}
	const char *name = MIPSGetName(MIPSOpcode(op));
	const bool unaligned = !strcmp(name, "lwl") || !strcmp(name, "lwr") || !strcmp(name, "swl") || !strcmp(name, "swr");
	if (!unaligned) {
		const int size = MIPSGetMemoryAccessSize(MIPSOpcode(op));
		if (size > 1) {
			offset &= ~(size - 1);
		}
	}
	return (op & ~0xFFFF) | ((u32)offset & 0xFFFF);
}

// From PPSSPP_CPUFUZZ_OPS. Empty means every instruction.
static std::vector<std::string> g_fuzzOps;

static bool MatchesFuzzOps(const char *name) {
	if (g_fuzzOps.empty()) {
		return true;
	}
	for (const std::string &prefix : g_fuzzOps) {
		if (!strncmp(name, prefix.c_str(), prefix.size())) {
			return true;
		}
	}
	return false;
}

static u32 GenerateFuzzOp(FuzzRng &rng, const char *classes) {
	static const u8 aluOps[] = { 0x00, 0x1C, 0x1F };
	static const u8 immOps[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
	static const u8 vfpuOps[] = { 0x12, 0x18, 0x19, 0x1B, 0x34, 0x37, 0x3C, 0x3F };
	static const u8 memOps[] = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x30, 0x31, 0x32, 0x35, 0x36, 0x38, 0x39, 0x3A, 0x3D, 0x3E };

	const u32 numClasses = (u32)strlen(classes);
	for (int tries = 0; tries < 100000; ++tries) {
		u32 top;
		switch (classes[rng.Below(numClasses)]) {
		case 'a': top = aluOps[rng.Below(ARRAY_SIZE(aluOps))]; break;
		case 'i': top = immOps[rng.Below(ARRAY_SIZE(immOps))]; break;
		case 'f': top = 0x11; break;
		case 'v':
		case 'r':
			top = vfpuOps[rng.Below(ARRAY_SIZE(vfpuOps))];
			break;
		default: top = memOps[rng.Below(ARRAY_SIZE(memOps))]; break;
		}
		u32 op = (top << 26) | (rng.Next() & 0x03FFFFFF);
		// Most COP1 formats are invalid, so mostly pick S or W.
		if (top == 0x11 && rng.Chance(80)) {
			op = (op & ~(0x1F << 21)) | ((rng.Chance(80) ? 16 : 20) << 21);
		}

		const MIPSInfo info = MIPSGetInfo(MIPSOpcode(op));
		if (info & (BAD_INSTRUCTION | IS_CONDBRANCH | IS_JUMP | IS_SYSCALL | DELAYSLOT)) {
			continue;
		}
		if (!IsFuzzableName(MIPSGetName(MIPSOpcode(op))) || !MatchesFuzzOps(MIPSGetName(MIPSOpcode(op)))) {
			continue;
		}
		// Not by MEMTYPE, which movz and movn alias with their CONDTYPE.
		if (std::find(std::begin(memOps), std::end(memOps), top) != std::end(memOps)) {
			op = FixupMemOp(op, rng);
		}
		// Keep k0 pointing at the scratch memory.
		if ((info & OUT_RT) && ((op >> 16) & 0x1F) == FUZZ_BASE_REG) {
			continue;
		}
		if ((info & OUT_RD) && ((op >> 11) & 0x1F) == FUZZ_BASE_REG) {
			continue;
		}
		return op;
	}
	return 0;
}

struct FuzzCase {
	std::vector<u32> code;
	FuzzState init;
};

// Turns some instructions into conditional branches forward, to a later instruction or the end, so
// every case still finishes. Never two in a row, so each delay slot is an ordinary instruction.
static void AddFuzzBranches(FuzzRng &rng, std::vector<u32> &code) {
	const int n = (int)code.size();
	for (int i = 0; i + 1 < n; i++) {
		if (!rng.Chance(20))
			continue;
		const int target = i + 2 + (int)rng.Below(n - i - 1);  // up to n, the terminator
		const u32 offset = (u32)(target - (i + 1)) & 0xFFFF;
		const u32 rs = 1 + rng.Below(31), rt = rng.Chance(30) ? 0 : 1 + rng.Below(31);
		u32 op;
		switch (rng.Below(6)) {
		case 0: op = (0x04 << 26) | (rs << 21) | (rt << 16); break;  // beq
		case 1: op = (0x05 << 26) | (rs << 21) | (rt << 16); break;  // bne
		case 2: op = ((rng.Chance(50) ? 0x06 : 0x07) << 26) | (rs << 21); break;  // blez, bgtz
		case 3: op = (0x01 << 26) | (rs << 21) | (rng.Below(2) << 16); break;  // bltz, bgez
		case 4: op = ((rng.Chance(50) ? 0x14 : 0x15) << 26) | (rs << 21) | (rt << 16); break;  // beql, bnel
		default: op = (0x01 << 26) | (rs << 21) | ((rng.Chance(50) ? 0x10 : 0x11) << 16); break;  // bltzal, bgezal
		}
		code[i] = op | offset;
		// The delay slot mustn't be a branch.
		if (MIPSGetInfo(MIPSOpcode(code[i + 1])) & (IS_CONDBRANCH | IS_JUMP))
			code[i + 1] = 0;
		i++;
	}
}

// A run of 2-4 vdots of one size against one vector, the others rows or columns of one matrix: the
// shape the IR frontend turns into a Vec4VfpuMultiDot. The results go to lanes of one vector or
// anywhere, sometimes over the operands.
static void AppendVdotRun(FuzzRng &rng, std::vector<u32> &code) {
	static const VectorSize sizes[] = { V_Pair, V_Triple, V_Quad };
	static const u32 sizeBits[] = { 0x0080, 0x8000, 0x8080 };
	const int si = rng.Below(3);
	const VectorSize sz = sizes[si];
	const int n = GetNumVectorElements(sz);
	auto regsOf = [&](int field, VectorSize s, u8 regs[4]) { GetVectorRegs(regs, s, field); };
	auto singleField = [&](int voffset) {
		for (int f = 0; f < 128; f++) {
			u8 r[4];
			regsOf(f, V_Single, r);
			if (r[0] == voffset)
				return f;
		}
		return (int)rng.Below(128);
	};

	const int shared = rng.Below(128);
	const bool transposed = rng.Chance(50);
	const int base = 16 * rng.Below(8);
	const int count = 2 + rng.Below(3);
	const bool laneDests = rng.Chance(50);
	const int destBase = 4 * rng.Below(32);
	const bool sharedIsT = rng.Chance(50);
	int lanesUsed = 0;
	for (int k = 0; k < count; k++) {
		// A free lane, and a vector field reading the block's row or Vec4 at that lane.
		int lane = rng.Below(4);
		while (lanesUsed & (1 << lane))
			lane = (lane + 1) & 3;
		lanesUsed |= 1 << lane;
		int field = -1;
		for (int f = 0; f < 128 && field < 0; f++) {
			u8 r[4];
			regsOf(f, sz, r);
			bool ok = true;
			for (int i = 0; i < n; i++)
				ok = ok && r[i] == (transposed ? base + 4 * lane + i : base + 4 * i + lane);
			if (ok)
				field = f;
		}
		if (field < 0)
			field = rng.Below(128);
		const int dest = laneDests ? singleField(destBase + lane) : (int)rng.Below(128);
		const int vs = sharedIsT ? field : shared;
		const int vt = sharedIsT ? shared : field;
		code.push_back(0x64800000 | sizeBits[si] | (vt << 16) | (vs << 8) | dest);
	}
}

static FuzzCase GenerateFuzzCase(u64 seed, const char *classes, const std::vector<u32> &codeOverride) {
	FuzzRng rng(seed);
	FuzzCase c;
	memset(&c.init, 0, sizeof(c.init));
	for (int i = 1; i < 32; ++i) {
		c.init.r[i] = FuzzIntBits(rng);
	}
	for (int i = 0; i < 32; ++i) {
		c.init.f[i] = rng.Chance(80) ? FuzzFloatBits(rng) : FuzzIntBits(rng);
	}
	for (int i = 0; i < 128; ++i) {
		c.init.v[i] = rng.Chance(90) ? FuzzFloatBits(rng) : FuzzIntBits(rng);
	}
	for (int i = 0; i < (int)sizeof(c.init.mem); i += 4) {
		const u32 w = FuzzFloatBits(rng);
		memcpy(&c.init.mem[i], &w, 4);
	}
	c.init.hi = FuzzIntBits(rng);
	c.init.lo = FuzzIntBits(rng);
	// The default rounding mode. Like games, the code changes it with ctc1, which the JITs need to
	// see compiled before they use the mode at all.
	c.init.fcr31 = 0;
	if (rng.Chance(50)) {
		c.init.fcr31 |= 0x00800000;
	}
	c.init.fpcond = (c.init.fcr31 >> 23) & 1;
	c.init.vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xE4;
	c.init.vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xE4;
	c.init.vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
	c.init.vfpuCtrl[VFPU_CTRL_CC] = rng.Below(64);
	c.init.vfpuCtrl[VFPU_CTRL_REV] = 0x7772CEAB;
	c.init.vfpuCtrl[VFPU_CTRL_RCX0] = 0x3F800001;
	c.init.vfpuCtrl[VFPU_CTRL_RCX1] = 0x3F800002;
	c.init.vfpuCtrl[VFPU_CTRL_RCX2] = 0x3F800004;
	c.init.vfpuCtrl[VFPU_CTRL_RCX3] = 0x3F800008;
	for (int i = VFPU_CTRL_RCX4; i <= VFPU_CTRL_RCX7; ++i) {
		c.init.vfpuCtrl[i] = 0x3F800000;
	}

	const int count = 1 + rng.Below(FUZZ_MAX_INSTRS);
	const bool runs = strchr(classes, 'r') != nullptr;
	while ((int)c.code.size() < count) {
		if (runs && rng.Chance(25) && (int)c.code.size() + 4 <= FUZZ_MAX_INSTRS)
			AppendVdotRun(rng, c.code);
		else
			c.code.push_back(GenerateFuzzOp(rng, classes));
	}
	if (strchr(classes, 'b'))
		AddFuzzBranches(rng, c.code);
	if (!codeOverride.empty()) {
		c.code = codeOverride;
	}
	return c;
}

// keepCache reruns blocks compiled by an earlier run of the same code.
// From PPSSPP_CPUFUZZ_BLOCKS.
static bool g_fuzzPrintBlocks = false;

static void RunFuzzCase(const std::vector<u32> &code, const FuzzState &init, FuzzState *out, bool keepCache = false) {
	const u32 codeAddr = PSP_GetUserMemoryBase() + FUZZ_CODE_OFFSET;
	const u32 memAddr = PSP_GetUserMemoryBase() + FUZZ_MEM_OFFSET;
	u32 addr = codeAddr;
	for (u32 op : code) {
		Memory::WriteUnchecked_U32(op, addr);
		addr += 4;
	}
	Memory::WriteUnchecked_U32(MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestTerminator"), addr);
	Memory::WriteUnchecked_U32(MIPS_MAKE_BREAK(1), addr + 4);
	Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), addr + 8);
	// The frontends compile and look ahead past the syscall, so don't leave an older case's code there.
	for (int i = 0; i < FUZZ_MAX_INSTRS; ++i) {
		Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), addr + 12 + i * 4);
	}
	if (mipsr4k.jit && !keepCache) {
		mipsr4k.jit->ClearCache();
	}

	MIPSState *mips = currentMIPS;
	memcpy(mips->r, init.r, sizeof(init.r));
	mips->r[FUZZ_BASE_REG] = memAddr + 512;
	memcpy(mips->fi, init.f, sizeof(init.f));
	memcpy(mips->vi, init.v, sizeof(init.v));
	memcpy(mips->vfpuCtrl, init.vfpuCtrl, sizeof(init.vfpuCtrl));
	mips->hi = init.hi;
	mips->lo = init.lo;
	mips->fcr31 = init.fcr31;
	// As a thread switch does, or a JIT keeps converting in the previous case's rounding mode.
	if (mipsr4k.jit) {
		mipsr4k.jit->UpdateFCR31();
	}
	mips->fpcond = init.fpcond;
	mips->llBit = 0;
	mips->inDelaySlot = false;
	Memory::MemcpyUnchecked(memAddr, init.mem, sizeof(init.mem));

	mips->pc = codeAddr;
	coreState = CORE_RUNNING_CPU;
	const u64 startTicks = CoreTiming::GetTicks(mips);
	while (coreState == CORE_RUNNING_CPU) {
		mips->RunLoopUntil(CoreTiming::GetTicks(mips) + 1000000);
	}
	out->cycles = (u32)(g_terminatorTicks - startTicks);

	if (g_fuzzPrintBlocks && mipsr4k.jit) {
		JitBlockCacheDebugInterface *blocks = mipsr4k.jit->GetBlockCacheDebugInterface();
		for (int i = 0; i < blocks->GetNumBlocks(); i++) {
			if (!blocks->IsValidBlock(i))
				continue;
			JitBlockDebugInfo info = blocks->GetBlockDebugInfo(i);
			printf("Block %08x:\n", info.originalAddress);
			for (const std::string &line : info.irDisasm)
				printf("  %s\n", line.c_str());
			for (const std::string &line : info.targetDisasm)
				printf("    %s\n", line.c_str());
		}
	}
	memcpy(out->r, mips->r, sizeof(out->r));
	memcpy(out->f, mips->fi, sizeof(out->f));
	memcpy(out->v, mips->vi, sizeof(out->v));
	memcpy(out->vfpuCtrl, mips->vfpuCtrl, sizeof(out->vfpuCtrl));
	out->hi = mips->hi;
	out->lo = mips->lo;
	out->fcr31 = mips->fcr31;
	out->fpcond = mips->fpcond;
	out->llBit = mips->llBit;
	Memory::MemcpyUnchecked(out->mem, memAddr, sizeof(out->mem));
}

static bool IsNaNBits(u32 bits) {
	return (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0;
}

static bool FloatBitsMatch(u32 a, u32 b) {
	return a == b || (IsNaNBits(a) && IsNaNBits(b));
}

// Appends a line per difference to out, if given. Returns the number of differences.
static int DiffFuzzStates(const FuzzState &ref, const FuzzState &other, std::string *out) {
	int diffs = 0;
	auto report = [&](const std::string &what, u32 a, u32 b) {
		++diffs;
		if (out) {
			float fa, fb;
			memcpy(&fa, &a, 4);
			memcpy(&fb, &b, 4);
			char buf[256];
			snprintf(buf, sizeof(buf), "    %s: interp %08x (%g)  core %08x (%g)\n", what.c_str(), a, fa, b, fb);
			*out += buf;
		}
	};
	for (int i = 0; i < 32; ++i) {
		if (ref.r[i] != other.r[i]) {
			report("r" + std::to_string(i), ref.r[i], other.r[i]);
		}
	}
	for (int i = 0; i < 32; ++i) {
		if (!FloatBitsMatch(ref.f[i], other.f[i])) {
			report("f" + std::to_string(i), ref.f[i], other.f[i]);
		}
	}
	for (int i = 0; i < 128; ++i) {
		if (!FloatBitsMatch(ref.v[i], other.v[i])) {
			report(GetVectorNotation(fromvoffset[i], V_Single), ref.v[i], other.v[i]);
		}
	}
	for (int i = 0; i < 16; ++i) {
		if (ref.vfpuCtrl[i] != other.vfpuCtrl[i]) {
			report("vfpuCtrl" + std::to_string(i), ref.vfpuCtrl[i], other.vfpuCtrl[i]);
		}
	}
	if (ref.hi != other.hi) {
		report("hi", ref.hi, other.hi);
	}
	if (ref.lo != other.lo) {
		report("lo", ref.lo, other.lo);
	}
	// Bit 23 is fpcond, which some cores copy into fcr31 when read and some don't.
	if ((ref.fcr31 & ~0x00800000) != (other.fcr31 & ~0x00800000)) {
		report("fcr31", ref.fcr31, other.fcr31);
	}
	if (ref.fpcond != other.fpcond) {
		report("fpcond", ref.fpcond, other.fpcond);
	}
	if (ref.llBit != other.llBit) {
		report("llBit", ref.llBit, other.llBit);
	}
	if (ref.cycles != other.cycles) {
		report("cycles", ref.cycles, other.cycles);
	}
	for (int i = 0; i < (int)sizeof(ref.mem); i += 4) {
		u32 a, b;
		memcpy(&a, &ref.mem[i], 4);
		memcpy(&b, &other.mem[i], 4);
		if (!FloatBitsMatch(a, b)) {
			report("mem+" + std::to_string(i), a, b);
		}
	}
	return diffs;
}

static void DumpFuzzState(FILE *f, const char *core, u64 seed, const FuzzState &s) {
	// All NaNs print the same, so their payloads don't show up as differences.
	auto fl = [](u32 v) { return IsNaNBits(v) ? 0x7FFFFFFFU : v; };
	fprintf(f, "%s %llu r", core, (unsigned long long)seed);
	for (int i = 0; i < 32; ++i) {
		fprintf(f, " %08x", s.r[i]);
	}
	fprintf(f, " f");
	for (int i = 0; i < 32; ++i) {
		fprintf(f, " %08x", fl(s.f[i]));
	}
	fprintf(f, " v");
	for (int i = 0; i < 128; ++i) {
		fprintf(f, " %08x", fl(s.v[i]));
	}
	fprintf(f, " c");
	for (int i = 0; i < 16; ++i) {
		fprintf(f, " %08x", s.vfpuCtrl[i]);
	}
	fprintf(f, " %08x %08x %08x %08x %d m", s.hi, s.lo, s.fcr31, s.fpcond, s.llBit);
	for (int i = 0; i < (int)sizeof(s.mem); i += 4) {
		u32 w;
		memcpy(&w, &s.mem[i], 4);
		fprintf(f, " %08x", fl(w));
	}
	fprintf(f, "\n");
}

// Nops are left out, as the minimizer leaves a lot of them in code with branches.
static void PrintFuzzCode(const std::vector<u32> &code) {
	u32 addr = PSP_GetUserMemoryBase() + FUZZ_CODE_OFFSET;
	for (u32 op : code) {
		if (op != 0) {
			char line[512];
			MIPSDisAsm(MIPSOpcode(op), addr, line, sizeof(line), true);
			printf("    %08x  %s\n", op, line);
		}
		addr += 4;
	}
}

// The start values of the registers the code reads (VFPU ones only as vectors of the op's size).
static void PrintFuzzInputs(const std::vector<u32> &code, const FuzzState &init) {
	std::string line;
	char buf[64];
	auto add = [&](const std::string &name, u32 value) {
		float f;
		memcpy(&f, &value, 4);
		snprintf(buf, sizeof(buf), " %s=%08x (%g)", name.c_str(), value, f);
		if (line.find(buf) == std::string::npos) {
			line += buf;
		}
	};
	for (u32 op : code) {
		const MIPSInfo info = MIPSGetInfo(MIPSOpcode(op));
		if (info & IS_VFPU) {
			for (int vreg : { (int)((op >> 8) & 0x7F), (int)((op >> 16) & 0x7F) }) {
				u8 regs[4];
				const VectorSize sz = GetVecSize(MIPSOpcode(op));
				GetVectorRegs(regs, sz, vreg);
				for (int i = 0; i < GetNumVectorElements(sz); i++) {
					add(GetVectorNotation(regs[i], V_Single), init.v[voffset[regs[i]]]);
				}
			}
			continue;
		}
		if (info & IN_RS) {
			add("r" + std::to_string((op >> 21) & 0x1F), init.r[(op >> 21) & 0x1F]);
		}
		if (info & IN_RT) {
			add("r" + std::to_string((op >> 16) & 0x1F), init.r[(op >> 16) & 0x1F]);
		}
		if (info & IN_FS) {
			add("f" + std::to_string((op >> 11) & 0x1F), init.f[(op >> 11) & 0x1F]);
		}
		if (info & IN_FT) {
			add("f" + std::to_string((op >> 16) & 0x1F), init.f[(op >> 16) & 0x1F]);
		}
	}
	if (!line.empty()) {
		printf("    inputs:%s\n", line.c_str());
	}
}

static const char *CoreName(CPUCore core) {
	switch (core) {
	case CPUCore::INTERPRETER: return "Interpreter";
	case CPUCore::IR_INTERPRETER: return "IR interpreter";
	case CPUCore::JIT: return "JIT";
	case CPUCore::JIT_IR: return "JIT IR";
	default: return "?";
	}
}

static bool g_fuzzUnknownPrefix = false;

// With PPSSPP_CPUFUZZ_UNKNOWNPREFIX, a block first leaves a prefix uneaten, after which the IR
// frontend stops assuming that blocks start with the default prefixes (startDefaultPrefix).
static void SwitchFuzzCore(CPUCore core) {
	mipsr4k.UpdateCore(core);
	if (g_fuzzUnknownPrefix && core != CPUCore::INTERPRETER) {
		FuzzState init{}, out;
		// vpfxd with lane 0 saturated, then the terminator.
		RunFuzzCase({ 0xDE000001 }, init, &out);
	}
}

static bool CoreMismatches(CPUCore core, const std::vector<u32> &code, const FuzzState &init) {
	FuzzState ref, other;
	mipsr4k.UpdateCore(CPUCore::INTERPRETER);
	RunFuzzCase(code, init, &ref);
	SwitchFuzzCore(core);
	RunFuzzCase(code, init, &other);
	return DiffFuzzStates(ref, other, nullptr) != 0;
}

// Drops instructions while the core still disagrees with the interpreter. With branches in the
// code, they become nops instead, so no branch changes its target or lands in a delay slot.
static std::vector<u32> MinimizeFuzzCase(CPUCore core, std::vector<u32> code, const FuzzState &init) {
	bool branches = false;
	for (u32 op : code)
		branches = branches || (MIPSGetInfo(MIPSOpcode(op)) & (IS_CONDBRANCH | IS_JUMP)) != 0;
	for (size_t i = 0; i < code.size(); ) {
		std::vector<u32> shorter = code;
		if (branches && code[i] == 0) {
			++i;
			continue;
		}
		if (branches)
			shorter[i] = 0;
		else
			shorter.erase(shorter.begin() + i);
		if (!shorter.empty() && CoreMismatches(core, shorter, init)) {
			code = shorter;
		} else {
			++i;
		}
	}
	return code;
}

}  // namespace

// Host extensions the JIT may use, from PPSSPP_CPUFUZZ_HOSTEXT: + turns on the ones detection can't
// see (RISC-V only reports them for a few known SoCs), - turns them off to test the fallbacks.
static void ApplyFuzzHostExtensions() {
	const char *ext = getenv("PPSSPP_CPUFUZZ_HOSTEXT");
	if (ext && (ext[0] == '+' || ext[0] == '-')) {
		const bool on = ext[0] == '+';
#if PPSSPP_ARCH(RISCV64)
		cpu_info.RiscV_Zba = on;
		cpu_info.RiscV_Zbb = on;
		cpu_info.RiscV_Zbs = on;
		cpu_info.RiscV_Zcb = on;
		cpu_info.RiscV_Zfa = on;
		cpu_info.RiscV_Zfh = on;
		cpu_info.RiscV_Zfhmin = on;
		cpu_info.RiscV_Zicond = on;
		// V is detected everywhere, but can be turned off.
		if (!on)
			cpu_info.RiscV_V = false;
#elif PPSSPP_ARCH(LOONGARCH64)
		if (!on) {
			cpu_info.LOONGARCH_LSX = false;
			cpu_info.LOONGARCH_LASX = false;
		}
#endif
		(void)on;
	}
}

bool TestCPUEquivalence() {
	SetupJitHarness();
	g_Config.bFastMemory = true;
	// Otherwise the JITs drop the registers a syscall would fill with 0xDEADBEEF, which the
	// terminating syscall doesn't.
	const bool oldSkipDeadbeef = g_Config.bSkipDeadbeefFilling;
	g_Config.bSkipDeadbeefFilling = true;

	const char *countStr = getenv("PPSSPP_CPUFUZZ_COUNT");
	const char *seedStr = getenv("PPSSPP_CPUFUZZ_SEED");
	const char *dumpPath = getenv("PPSSPP_CPUFUZZ_DUMP");
	const char *classes = getenv("PPSSPP_CPUFUZZ_CLASSES");
	const bool verbose = getenv("PPSSPP_CPUFUZZ_VERBOSE") != nullptr;
	const char *skipCores = getenv("PPSSPP_CPUFUZZ_SKIP");
	g_fuzzUnknownPrefix = getenv("PPSSPP_CPUFUZZ_UNKNOWNPREFIX") != nullptr;
	g_fuzzPrintBlocks = getenv("PPSSPP_CPUFUZZ_BLOCKS") != nullptr;
	const int count = countStr ? atoi(countStr) : 2000;
	const u64 firstSeed = seedStr ? strtoull(seedStr, nullptr, 10) : 1;
	g_fuzzOps.clear();
	if (const char *ops = getenv("PPSSPP_CPUFUZZ_OPS")) {
		std::string list = !strcmp(ops, "dot") ? "vdot,vhdp,vfad,vavg,vdet,vcrs,vmmul,v(h)tfm,vpfx" : ops;
		size_t start = 0;
		while (start <= list.size()) {
			size_t end = list.find(',', start);
			if (end == std::string::npos) {
				end = list.size();
			}
			if (end > start) {
				g_fuzzOps.push_back(list.substr(start, end - start));
			}
			start = end + 1;
		}
	}
	if (!classes || !classes[0]) {
		classes = g_fuzzOps.empty() ? "aaiffvvvmmr" : (!strcmp(getenv("PPSSPP_CPUFUZZ_OPS"), "dot") ? "vr" : "v");
	}
	std::vector<u32> codeOverride;
	const char *codeSeedStr = getenv("PPSSPP_CPUFUZZ_CODE_SEED");
	const u64 codeSeed = codeSeedStr ? strtoull(codeSeedStr, nullptr, 10) : 0;
	const std::vector<u32> noOverride;
	auto overrideFor = [&](u64 seed) -> const std::vector<u32> & {
		return codeSeed == 0 || codeSeed == seed ? codeOverride : noOverride;
	};
	if (const char *codeStr = getenv("PPSSPP_CPUFUZZ_CODE")) {
		char *end = nullptr;
		for (const char *p = codeStr; *p; p = end) {
			codeOverride.push_back((u32)strtoul(p, &end, 16));
			if (end == p) {
				break;
			}
			while (*end == ',' || *end == ' ') {
				end++;
			}
		}
	}

	ApplyFuzzHostExtensions();

	static const CPUCore cores[] = {
		CPUCore::INTERPRETER,
		CPUCore::IR_INTERPRETER,
		CPUCore::JIT,
	};
	const int numCores = (int)ARRAY_SIZE(cores);

	std::vector<FuzzState> results((size_t)count * numCores);
	for (int c = 0; c < numCores; ++c) {
		if (c > 0 && skipCores && strchr(skipCores, '0' + c)) {
			for (int i = 0; i < count; ++i) {
				results[(size_t)i * numCores + c] = results[(size_t)i * numCores];
			}
			continue;
		}
		SwitchFuzzCore(cores[c]);
		for (int i = 0; i < count; ++i) {
			FuzzCase fc = GenerateFuzzCase(firstSeed + i, classes, overrideFor(firstSeed + i));
			if (verbose) {
				printf("%s seed %llu\n", CoreName(cores[c]), (unsigned long long)(firstSeed + i));
				PrintFuzzCode(fc.code);
				fflush(stdout);
			}
			RunFuzzCase(fc.code, fc.init, &results[(size_t)i * numCores + c]);
		}
	}

	if (dumpPath) {
		FILE *f = fopen(dumpPath, "w");
		if (f) {
			for (int c = 0; c < numCores; ++c) {
				for (int i = 0; i < count; ++i) {
					DumpFuzzState(f, CoreName(cores[c]), firstSeed + i, results[(size_t)i * numCores + c]);
				}
			}
			fclose(f);
		} else {
			printf("Couldn't write %s\n", dumpPath);
		}
	}

	int failures[ARRAY_SIZE(cores)]{};
	const char *reportsStr = getenv("PPSSPP_CPUFUZZ_REPORTS");
	const int maxReports = reportsStr ? atoi(reportsStr) : 20;
	// Minimized differences by the name of their last instruction, which is usually the culprit.
	std::map<std::string, int> culprits;
	int reports = 0;
	for (int i = 0; i < count; ++i) {
		const FuzzState &ref = results[(size_t)i * numCores];
		for (int c = 1; c < numCores; ++c) {
			if (DiffFuzzStates(ref, results[(size_t)i * numCores + c], nullptr) == 0) {
				continue;
			}
			++failures[c];
			if (reports >= maxReports) {
				continue;
			}
			++reports;
			FuzzCase fc = GenerateFuzzCase(firstSeed + i, classes, overrideFor(firstSeed + i));
			std::vector<u32> code = MinimizeFuzzCase(cores[c], fc.code, fc.init);
			FuzzState a, b;
			mipsr4k.UpdateCore(CPUCore::INTERPRETER);
			RunFuzzCase(code, fc.init, &a);
			SwitchFuzzCore(cores[c]);
			RunFuzzCase(code, fc.init, &b);
			std::string diff;
			DiffFuzzStates(a, b, &diff);
			// The last instruction that isn't a nop left by minimizing.
			u32 last = code.back();
			for (auto it = code.rbegin(); it != code.rend() && last == 0; ++it)
				last = *it;
			culprits[std::string(CoreName(cores[c])) + ": " + MIPSGetName(MIPSOpcode(last))]++;
			printf("Seed %llu: %s differs from the interpreter (fcr31 %08x, cc %02x):\n", (unsigned long long)(firstSeed + i), CoreName(cores[c]), fc.init.fcr31, fc.init.vfpuCtrl[VFPU_CTRL_CC]);
			PrintFuzzCode(code);
			PrintFuzzInputs(code, fc.init);
			printf("%s", diff.c_str());
		}
	}

	if (!culprits.empty()) {
		std::vector<std::pair<int, std::string>> sorted;
		for (const auto &it : culprits) {
			sorted.emplace_back(it.second, it.first);
		}
		std::sort(sorted.rbegin(), sorted.rend());
		printf("Reported differences by last instruction:\n");
		for (const auto &it : sorted) {
			printf("  %4d  %s\n", it.first, it.second.c_str());
		}
	}

	bool success = true;
	for (int c = 1; c < numCores; ++c) {
		printf("%s: %d of %d cases differ from the interpreter\n", CoreName(cores[c]), failures[c], count);
		if (failures[c]) {
			success = false;
		}
	}
	// Known differences remain (see docs/cpu-equivalence.md), so only crashes fail the test by default.
	if (!success && !getenv("PPSSPP_CPUFUZZ_STRICT")) {
		printf("Not failing, set PPSSPP_CPUFUZZ_STRICT to fail on differences.\n");
		success = true;
	}

	mipsr4k.UpdateCore(CPUCore::INTERPRETER);
	DestroyJitHarness();
	g_Config.bSkipDeadbeefFilling = oldSkipDeadbeef;
	return success;
}

static bool AssembleLines(const std::vector<const char *> &lines, std::vector<u32> *out) {
	// Assembled in scratch memory past the fuzz code and data, then read back.
	const u32 base = PSP_GetUserMemoryBase() + 0x20000;
	u32 addr = base;
	for (const char *line : lines) {
		std::string error;
		if (!MipsAssembleOpcode(line, currentDebugMIPS, addr, &error)) {
			printf("Can't assemble '%s': %s\n", line, error.c_str());
			return false;
		}
		out->push_back(Memory::ReadUnchecked_U32(addr));
		addr += 4;
	}
	return true;
}

// The guest's rounding mode and flush-to-zero apply exactly while emulated code runs, on every
// core, including ops a JIT leaves to the interpreter: HLE always sees the host's own mode, a ctc1
// takes effect at once, and a mode that arrives from outside (a savestate, a thread switch, a core
// switch) also reaches blocks compiled before it.
bool TestCPURoundingMode() {
	SetupJitHarness();
	g_Config.bFastMemory = true;
	const bool oldSkipDeadbeef = g_Config.bSkipDeadbeefFilling;
	g_Config.bSkipDeadbeefFilling = true;
	const u32 oldJitDisable = g_Config.uJitDisableFlags;
	const u32 probe = MIPS_MAKE_SYSCALL("UnitTestFakeSyscalls", "UnitTestRoundingProbe");

	// t0 holds the mode to set. f0/f1 = 1/3, f4 * f5 = FLT_MIN / 2, f7 = 1.5 converted by the mode
	// (cvt.w.s) and by round.w.s, which always rounds to nearest.
	std::vector<u32> setMode, preset;
	// The math comes right after each ctc1, before anything (like a syscall) could reapply the mode.
	bool success = AssembleLines({
		"ctc1 t0, $31",
		"div.s f2, f0, f1",
		"mul.s f3, f4, f5",
		"cvt.w.s f6, f7",
		"round.w.s f8, f7",
		"nop",
		"ctc1 zero, $31",
		"div.s f9, f0, f1",
		"nop",
	}, &setMode) && AssembleLines({
		"nop",
		"div.s f2, f0, f1",
		"mul.s f3, f4, f5",
		"cvt.w.s f6, f7",
		"round.w.s f8, f7",
		"nop",
	}, &preset);
	if (!success) {
		DestroyJitHarness();
		return false;
	}
	// The probes go where the nops are.
	setMode[5] = probe;
	setMode[8] = probe;
	preset[0] = probe;
	preset[5] = probe;

	FuzzState init{};
	auto setFloat = [&](int reg, float f) { memcpy(&init.f[reg], &f, 4); };
	setFloat(0, 1.0f);
	setFloat(1, 3.0f);
	setFloat(4, FLT_MIN);
	setFloat(5, 0.5f);
	setFloat(7, 1.5f);

	static const u32 modes[] = { 0x00000001, 0x00000002, 0x00000003, 0x01000000, 0x01000003 };
	static const CPUCore cores[] = {
		CPUCore::IR_INTERPRETER,
		CPUCore::JIT,
	};
	// No flags, then ctc1 left to the interpreter, then everything the JITs can leave to it.
	static const u32 disableFlags[] = { 0, (u32)MIPSComp::JitDisable::FPU_XFER, 0xFFFF };

	auto checkProbes = [&](const char *what) {
		for (size_t i = 0; i + 1 < g_roundingProbes.size(); i += 2) {
			if (g_roundingProbes[i] != 0x3EAAAAAB || g_roundingProbes[i + 1] != 0x00400000) {
				printf("%s: HLE ran in the guest's mode (probe %d: %08x %08x)\n", what, (int)(i / 2), g_roundingProbes[i], g_roundingProbes[i + 1]);
				success = false;
			}
		}
		g_roundingProbes.clear();
	};

	for (u32 mode : modes) {
		// The reference: the interpreter, which the other cores must match.
		mipsr4k.UpdateCore(CPUCore::INTERPRETER);
		FuzzState refSet, refPreset;
		init.r[MIPS_REG_T0] = mode;
		init.fcr31 = 0;
		RunFuzzCase(setMode, init, &refSet);
		checkProbes("Interpreter");
		init.fcr31 = mode;
		RunFuzzCase(preset, init, &refPreset);
		checkProbes("Interpreter");

		for (CPUCore core : cores) {
			for (u32 flags : disableFlags) {
				char what[128];
				snprintf(what, sizeof(what), "%s, mode %08x, JitDisableFlags %08x", CoreName(core), mode, flags);
				g_Config.uJitDisableFlags = flags;
				// A fresh JIT, so the flags apply and no earlier mode has been seen.
				mipsr4k.UpdateCore(CPUCore::INTERPRETER);
				mipsr4k.UpdateCore(core);

				// Compiled in the default mode first, then rerun after the mode arrives from outside.
				FuzzState state;
				init.fcr31 = 0;
				RunFuzzCase(preset, init, &state);
				g_roundingProbes.clear();
				init.fcr31 = mode;
				RunFuzzCase(preset, init, &state, true);
				std::string diff;
				if (DiffFuzzStates(refPreset, state, &diff) != 0) {
					printf("%s, mode set from outside:\n%s", what, diff.c_str());
					success = false;
				}
				checkProbes(what);

				init.fcr31 = 0;
				RunFuzzCase(setMode, init, &state);
				diff.clear();
				if (DiffFuzzStates(refSet, state, &diff) != 0) {
					printf("%s, mode set by ctc1:\n%s", what, diff.c_str());
					success = false;
				}
				checkProbes(what);
			}
		}
	}

	mipsr4k.UpdateCore(CPUCore::INTERPRETER);
	g_Config.uJitDisableFlags = oldJitDisable;
	g_Config.bSkipDeadbeefFilling = oldSkipDeadbeef;
	DestroyJitHarness();
	return success;
}

// The JITs' own VFPU dot product code against the reference, in every variant the host can run,
// through a C-callable wrapper with the eight operands at the pointer passed. Inf and NaN only go
// through the branch to the reference.
bool TestVFPUDotJit() {
	typedef float (*DotFunc)(const float *operands);
	auto check = [](const char *name, DotFunc fn) {
		auto toBits = [](float f) {
			u32 bits;
			memcpy(&bits, &f, sizeof(bits));
			return bits;
		};
		printf("%s\n", name);
		return ForEachVFPUDotTestCase([&](const float a[4], const float b[4]) {
			float operands[8];
			memcpy(operands, a, sizeof(float) * 4);
			memcpy(operands + 4, b, sizeof(float) * 4);
			const u32 expected = toBits(vfpu_dot_reference(a, b));
			const u32 actual = toBits(fn(operands));
			if (expected != actual) {
				printf("%s(%08x %08x %08x %08x, %08x %08x %08x %08x) = %08x, expected %08x\n", name,
					toBits(a[0]), toBits(a[1]), toBits(a[2]), toBits(a[3]), toBits(b[0]), toBits(b[1]), toBits(b[2]), toBits(b[3]), actual, expected);
				return false;
			}
			return true;
		});
	};
	(void)check;
	ApplyFuzzHostExtensions();
	bool success = true;

#if PPSSPP_ARCH(RISCV64)
	using namespace RiscVGen;
	using namespace MIPSComp;
	const bool hasExt = cpu_info.RiscV_Zbb && cpu_info.RiscV_Zicond;
	const bool oldZbb = cpu_info.RiscV_Zbb;
	const bool oldZicond = cpu_info.RiscV_Zicond;
	struct Variant {
		const char *name;
		bool vector;
		bool ext;
	};
	const Variant variants[] = {
		{ "scalar", false, false },
		{ "scalar Zbb Zicond", false, true },
		{ "vector", true, hasExt },
	};
	RiscVCodeBlock code;
	code.AllocCodeSpace(0x10000);
	// As in the JIT.
	code.SetAutoCompress(true);
	for (const Variant &v : variants) {
		if ((v.ext && !hasExt) || (v.vector && !cpu_info.RiscV_V))
			continue;
		cpu_info.RiscV_Zbb = v.ext;
		cpu_info.RiscV_Zicond = v.ext;
		code.BeginWrite(0x1000);
		const u8 *start = code.AlignCode16();
		code.MV(X12, X10);
		if (v.vector) {
			// Whatever the routine reads before writing is garbage.
			code.VSETIVLI(R_ZERO, 4, VType(VSew::E32, VLMul::M1, VTail::A, VMask::A));
			for (int i = 0; i < 32; i++)
				code.VMV_VI((RiscVReg)(V0 + i), -1);
		}
		const RiscVVfpuDotRegs regs{ X12, 0, { X10, X11, X5, X6, X7, X28, X29, X30, X31, X16, X17 }, F10 };
		FixupBranch special = EmitRiscVVfpuDot(&code, regs, v.vector);
		code.SetJumpTarget(special);
		code.MV(X10, X12);
		code.ADDI(X11, X12, 16);
		code.QuickJ(X5, (const u8 *)&vfpu_dot_reference);
		code.FlushIcache();
		code.EndWrite();
		if (!check(v.name, (DotFunc)start))
			success = false;
	}
	code.FreeCodeSpace();
	cpu_info.RiscV_Zbb = oldZbb;
	cpu_info.RiscV_Zicond = oldZicond;
#elif PPSSPP_ARCH(LOONGARCH64)
	using namespace LoongArch64Gen;
	using namespace MIPSComp;
	LoongArch64CodeBlock code;
	code.AllocCodeSpace(0x10000);
	for (int lsx = 0; lsx < 2; lsx++) {
		if (lsx && !cpu_info.LOONGARCH_LSX)
			continue;
		code.BeginWrite(0x1000);
		const u8 *start = code.AlignCode16();
		const LoongArch64VfpuDotRegs regs{ R4, 0, V0, V1, { V2, V3, V4, V5, V6, V7 },
			{ R12, R13, R14, R15, R16, R17, R18, R19, R20 }, F0, F1 };
		if (lsx) {
			// Whatever the routine reads before writing is garbage.
			code.LI(R12, -1);
			for (int i = 0; i < 24; i++)
				code.VREPLGR2VR_W((LoongArch64Reg)(V0 + i), R12);
			code.VLD(regs.x, R4, 0);
			code.VLD(regs.y, R4, 16);
		}
		FixupBranch special = EmitLoongArch64VfpuDot(&code, regs, lsx != 0);
		code.SetJumpTarget(special);
		code.ADDI_D(R5, R4, 16);
		code.QuickJ(R12, (const u8 *)&vfpu_dot_reference);
		code.FlushIcache();
		code.EndWrite();
		if (!check(lsx ? "LSX" : "scalar", (DotFunc)start))
			success = false;
	}
	code.FreeCodeSpace();
#endif

	return success;
}
