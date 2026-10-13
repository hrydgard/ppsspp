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

	// The disassembly shows the hook, with the instruction under it.
	char disasm[256];
	DisAsm(func, disasm, sizeof(disasm));
	EXPECT_TRUE(strstr(disasm, "replacement") != nullptr && strstr(disasm, "addiu") != nullptr);
	DisAsm(func + 4, disasm, sizeof(disasm));
	EXPECT_TRUE(strstr(disasm, "replacement") == nullptr);

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

// Differential fuzzing of the CPU cores. Random straight-line code (ALU, FPU, VFPU, loads and
// stores) runs from a random register state on each core, and every core has to end up with the
// interpreter's state, NaN payloads aside.
//
// Environment variables:
//   PPSSPP_CPUFUZZ_COUNT    number of cases (default 2000)
//   PPSSPP_CPUFUZZ_SEED     first case's seed (default 1). Case i always uses seed + i.
//   PPSSPP_CPUFUZZ_DUMP     file to write the interpreter's end state of every case to, for diffing
//                           the results of different host architectures.
//   PPSSPP_CPUFUZZ_CLASSES  instruction classes to generate, as a weighted string of "aifvm" (alu,
//                           imm, fpu, vfpu, memory).
//   PPSSPP_CPUFUZZ_VERBOSE  print each case before running it, to find one that crashes.
//   PPSSPP_CPUFUZZ_CODE     comma-separated hex instructions to run in place of the generated
//                           code, from the generated start state.
//   PPSSPP_CPUFUZZ_CODE_SEED  apply PPSSPP_CPUFUZZ_CODE to this seed only.
//   PPSSPP_CPUFUZZ_SKIP     cores not to run, by digit: 1 IR interpreter, 2 JIT, 3 JIT IR.
//   PPSSPP_CPUFUZZ_REPORTS  how many differences to minimize and print (default 20).
//   PPSSPP_CPUFUZZ_STRICT   fail the test on any difference, not only on a crash.

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
		case 'v': top = vfpuOps[rng.Below(ARRAY_SIZE(vfpuOps))]; break;
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
		if (!IsFuzzableName(MIPSGetName(MIPSOpcode(op)))) {
			continue;
		}
		// srl and srlv with other bits than rotr's set in the unused field: the interpreter does
		// nothing, the rest shift. TODO: Test on hardware.
		if (top == 0 && (op & 0x3F) == 2 && ((op >> 21) & 0x1F) > 1) {
			continue;
		}
		if (top == 0 && (op & 0x3F) == 6 && ((op >> 6) & 0x1F) > 1) {
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
	for (int i = 0; i < count; ++i) {
		c.code.push_back(GenerateFuzzOp(rng, classes));
	}
	if (!codeOverride.empty()) {
		c.code = codeOverride;
	}
	return c;
}

static void RunFuzzCase(const std::vector<u32> &code, const FuzzState &init, FuzzState *out) {
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
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
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
	mips->fpcond = init.fpcond;
	mips->llBit = 0;
	mips->inDelaySlot = false;
	Memory::MemcpyUnchecked(memAddr, init.mem, sizeof(init.mem));

	mips->pc = codeAddr;
	coreState = CORE_RUNNING_CPU;
	while (coreState == CORE_RUNNING_CPU) {
		mips->RunLoopUntil(CoreTiming::GetTicks(mips) + 1000000);
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

static void DumpFuzzState(FILE *f, u64 seed, const FuzzState &s) {
	// All NaNs print the same, so their payloads don't show up as differences.
	auto fl = [](u32 v) { return IsNaNBits(v) ? 0x7FFFFFFFU : v; };
	fprintf(f, "%llu r", (unsigned long long)seed);
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

static void PrintFuzzCode(const std::vector<u32> &code) {
	u32 addr = PSP_GetUserMemoryBase() + FUZZ_CODE_OFFSET;
	for (u32 op : code) {
		char line[512];
		MIPSDisAsm(MIPSOpcode(op), addr, line, sizeof(line), true);
		printf("    %08x  %s\n", op, line);
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

static bool CoreMismatches(CPUCore core, const std::vector<u32> &code, const FuzzState &init) {
	FuzzState ref, other;
	mipsr4k.UpdateCore(CPUCore::INTERPRETER);
	RunFuzzCase(code, init, &ref);
	mipsr4k.UpdateCore(core);
	RunFuzzCase(code, init, &other);
	return DiffFuzzStates(ref, other, nullptr) != 0;
}

// Drops instructions while the core still disagrees with the interpreter.
static std::vector<u32> MinimizeFuzzCase(CPUCore core, std::vector<u32> code, const FuzzState &init) {
	for (size_t i = 0; i < code.size(); ) {
		std::vector<u32> shorter = code;
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
	const int count = countStr ? atoi(countStr) : 2000;
	const u64 firstSeed = seedStr ? strtoull(seedStr, nullptr, 10) : 1;
	if (!classes || !classes[0]) {
		classes = "aaiffvvvmm";
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

	static const CPUCore cores[] = {
		CPUCore::INTERPRETER,
		CPUCore::IR_INTERPRETER,
		CPUCore::JIT,
#if !PPSSPP_PLATFORM(MAC)
		CPUCore::JIT_IR,
#endif
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
		mipsr4k.UpdateCore(cores[c]);
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
			for (int i = 0; i < count; ++i) {
				DumpFuzzState(f, firstSeed + i, results[(size_t)i * numCores]);
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
			mipsr4k.UpdateCore(cores[c]);
			RunFuzzCase(code, fc.init, &b);
			std::string diff;
			DiffFuzzStates(a, b, &diff);
			culprits[std::string(CoreName(cores[c])) + ": " + MIPSGetName(MIPSOpcode(code.back()))]++;
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
