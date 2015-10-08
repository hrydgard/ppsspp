// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "math/math_util.h"

#include "ABI.h"
#include "x64Emitter.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Common/MemoryUtil.h"

#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/Asm.h"
#include "Core/MIPS/x86/Jit.h"

using namespace Gen;
using namespace X64JitConstants;

//TODO - make an option
//#if _DEBUG
static bool enableDebug = false; 

//#else
//		bool enableDebug = false; 
//#endif

//static bool enableStatistics = false; //unused?

//GLOBAL STATIC ALLOCATIONS x86
//EAX - ubiquitous scratch register - EVERYBODY scratches this
//EBP - Pointer to fpr/gpr regs

//GLOBAL STATIC ALLOCATIONS x64
//EAX - ubiquitous scratch register - EVERYBODY scratches this
//RBX - Base pointer of memory
//R14 - Pointer to fpr/gpr regs
//R15 - Pointer to array of block pointers

extern volatile CoreState coreState;

void ImHere() {
	DEBUG_LOG(CPU, "JIT Here: %08x", currentMIPS->pc);
}

void AsmRoutineManager::Generate(MIPSState *mips, MIPSComp::Jit *jit, MIPSComp::JitOptions *jo) {
	const u8 *start = AlignCode16();

	restoreRoundingMode = AlignCode16(); {
		STMXCSR(M(&mips->temp));
		// Clear the rounding mode and flush-to-zero bits back to 0.
		AND(32, M(&mips->temp), Imm32(~(7 << 13)));
		LDMXCSR(M(&mips->temp));
		RET();
	}

	applyRoundingMode = AlignCode16(); {
		MOV(32, R(EAX), M(&mips->fcr31));
		AND(32, R(EAX), Imm32(0x1000003));

		// If it's 0, we don't actually bother setting.  This is the most common.
		// We always use nearest as the default rounding mode with
		// flush-to-zero disabled.
		FixupBranch skip = J_CC(CC_Z);

		STMXCSR(M(&mips->temp));

		// The MIPS bits don't correspond exactly, so we have to adjust.
		// 0 -> 0 (skip2), 1 -> 3, 2 -> 2 (skip2), 3 -> 1
		TEST(8, R(AL), Imm8(1));
		FixupBranch skip2 = J_CC(CC_Z);
		XOR(32, R(EAX), Imm8(2));
		SetJumpTarget(skip2);

		SHL(32, R(EAX), Imm8(13));
		OR(32, M(&mips->temp), R(EAX));

		TEST(32, M(&mips->fcr31), Imm32(1 << 24));
		FixupBranch skip3 = J_CC(CC_Z);
		OR(32, M(&mips->temp), Imm32(1 << 15));
		SetJumpTarget(skip3);

		LDMXCSR(M(&mips->temp));
		SetJumpTarget(skip);
		RET();
	}

	updateRoundingMode = AlignCode16(); {
		// If it's only ever 0, we don't actually bother applying or restoring it.
		// This is the most common situation.
		TEST(32, M(&mips->fcr31), Imm32(0x01000003));
		FixupBranch skip = J_CC(CC_Z);
#ifdef _M_X64
		// TODO: Move the hasSetRounding flag somewhere we can reach it through the context pointer, or something.
		MOV(64, R(RAX), Imm64((uintptr_t)&jit->js.hasSetRounding));
		MOV(8, MatR(RAX), Imm8(1));
#else
		MOV(8, M(&jit->js.hasSetRounding), Imm8(1));
#endif
		SetJumpTarget(skip);

		RET();
	}

	enterDispatcher = AlignCode16();
	ABI_PushAllCalleeSavedRegsAndAdjustStack();
#ifdef _M_X64
	// Two statically allocated registers.
	MOV(64, R(MEMBASEREG), ImmPtr(Memory::base));
	uintptr_t jitbase = (uintptr_t)jit->GetBasePtr();
	if (jitbase > 0x7FFFFFFFULL)
	{
		MOV(64, R(JITBASEREG), ImmPtr(jit->GetBasePtr()));
		jo->reserveR15ForAsm = true;
	}
#endif
	// From the start of the FP reg, a single byte offset can reach all GPR + all FPR (but no VFPUR)
	MOV(PTRBITS, R(CTXREG), ImmPtr(&mips->f[0]));

	outerLoop = GetCodePtr();
		jit->RestoreRoundingMode(true, this);
		ABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		jit->ApplyRoundingMode(true, this);
		FixupBranch skipToRealDispatch = J(); //skip the sync and compare first time

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = J_CC(CC_S, true);

		CMP(32, M(&coreState), Imm32(0));
		FixupBranch badCoreState = J_CC(CC_NZ, true);
		FixupBranch skipToRealDispatch2 = J(); //skip the sync and compare first time

		dispatcher = GetCodePtr();

			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			FixupBranch bail = J_CC(CC_S, true);

			SetJumpTarget(skipToRealDispatch);
			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			MOV(32, R(EAX), M(&mips->pc));
			dispatcherInEAXNoCheck = GetCodePtr();

#ifdef _M_IX86
			AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
			_assert_msg_(CPU, Memory::base != 0, "Memory base bogus");
			MOV(32, R(EAX), MDisp(EAX, (u32)Memory::base));
#elif _M_X64
			MOV(32, R(EAX), MComplex(MEMBASEREG, RAX, SCALE_1, 0));
#endif
			MOV(32, R(EDX), R(EAX));
			_assert_msg_(JIT, MIPS_JITBLOCK_MASK == 0xFF000000, "Hardcoded assumption of emuhack mask");
			SHR(32, R(EDX), Imm8(24));
			CMP(32, R(EDX), Imm8(MIPS_EMUHACK_OPCODE >> 24));
			FixupBranch notfound = J_CC(CC_NE);
				if (enableDebug)
				{
					ADD(32, M(&mips->debugCount), Imm8(1));
				}
				//grab from list and jump to it
				AND(32, R(EAX), Imm32(MIPS_EMUHACK_VALUE_MASK));
#ifdef _M_IX86
				ADD(32, R(EAX), ImmPtr(jit->GetBasePtr()));
#elif _M_X64
				if (jo->reserveR15ForAsm)
					ADD(64, R(RAX), R(JITBASEREG));
				else
					ADD(64, R(EAX), Imm32(jitbase));
#endif
				JMPptr(R(EAX));
			SetJumpTarget(notfound);

			//Ok, no block, let's jit
			jit->RestoreRoundingMode(true, this);
			ABI_CallFunction(&MIPSComp::JitAt);
			jit->ApplyRoundingMode(true, this);
			JMP(dispatcherNoCheck, true); // Let's just dispatch again, we'll enter the block since we know it's there.

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		CMP(32, M(&coreState), Imm32(0));
		J_CC(CC_Z, outerLoop, true);

	SetJumpTarget(badCoreState);
	jit->RestoreRoundingMode(true, this);
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();

	breakpointBailout = GetCodePtr();
	jit->RestoreRoundingMode(true, this);
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();
}
