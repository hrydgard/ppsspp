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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Common/Math/math_util.h"

#include "ABI.h"
#include "x64Emitter.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Common/MemoryUtil.h"

#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/Jit.h"

using namespace Gen;
using namespace X64JitConstants;

extern volatile CoreState coreState;

namespace MIPSComp
{

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

void ImHere() {
	DEBUG_LOG(Log::CPU, "JIT Here: %08x", currentMIPS->pc);
}

void Jit::GenerateFixedCode(JitOptions &jo) {
	BeginWrite(GetMemoryProtectPageSize());
	AlignCodePage();

	restoreRoundingMode = AlignCode16(); {
		STMXCSR(MIPSSTATE_VAR(temp));
		// Clear the rounding mode and flush-to-zero bits back to 0.
		AND(32, MIPSSTATE_VAR(temp), Imm32(~(7 << 13)));
		LDMXCSR(MIPSSTATE_VAR(temp));
		RET();
	}

	applyRoundingMode = AlignCode16(); {
		MOV(32, R(EAX), MIPSSTATE_VAR(fcr31));
		AND(32, R(EAX), Imm32(0x01000003));

		// If it's 0 (nearest + no flush0), we don't actually bother setting - we cleared the rounding
		// mode out in restoreRoundingMode anyway. This is the most common.
		FixupBranch skip = J_CC(CC_Z);
		STMXCSR(MIPSSTATE_VAR(temp));

		// The MIPS bits don't correspond exactly, so we have to adjust.
		// 0 -> 0 (skip2), 1 -> 3, 2 -> 2 (skip2), 3 -> 1
		TEST(8, R(AL), Imm8(1));
		FixupBranch skip2 = J_CC(CC_Z);
		XOR(32, R(EAX), Imm8(2));
		SetJumpTarget(skip2);

		// Adjustment complete, now reconstruct MXCSR
		SHL(32, R(EAX), Imm8(13));
		// Before setting new bits, we must clear the old ones.
		AND(32, MIPSSTATE_VAR(temp), Imm32(~(7 << 13)));   // Clearing bits 13-14 (rounding mode) and 15 (flush to zero)
		OR(32, MIPSSTATE_VAR(temp), R(EAX));

		TEST(32, MIPSSTATE_VAR(fcr31), Imm32(1 << 24));
		FixupBranch skip3 = J_CC(CC_Z);
		OR(32, MIPSSTATE_VAR(temp), Imm32(1 << 15));
		SetJumpTarget(skip3);

		LDMXCSR(MIPSSTATE_VAR(temp));
		SetJumpTarget(skip);
		RET();
	}

	enterDispatcher = AlignCode16();
	ABI_PushAllCalleeSavedRegsAndAdjustStack();
#if PPSSPP_ARCH(AMD64)
	// Two statically allocated registers.
	MOV(64, R(MEMBASEREG), ImmPtr(Memory::base));
	uintptr_t jitbase = (uintptr_t)GetBasePtr();
	if (jitbase > 0x7FFFFFFFULL) {
		MOV(64, R(JITBASEREG), ImmPtr(GetBasePtr()));
		jo.reserveR15ForAsm = true;
	}
#endif
	// From the start of the FP reg, a single byte offset can reach all GPR + all FPR (but no VFPUR)
	MOV(PTRBITS, R(CTXREG), ImmPtr(&mips_->f[0]));

	outerLoop = GetCodePtr();
		RestoreRoundingMode(true);
		ABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		ApplyRoundingMode(true);
		FixupBranch skipToCoreStateCheck = J();  //skip the downcount check

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = J_CC(CC_S, true);

		SetJumpTarget(skipToCoreStateCheck);
		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm32(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm32(0));
		}
		FixupBranch badCoreState = J_CC(CC_NZ, true);
		FixupBranch skipToRealDispatch2 = J(); //skip the sync and compare first time

		dispatcher = GetCodePtr();

			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			FixupBranch bail = J_CC(CC_S, true);

			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			MOV(32, R(EAX), MIPSSTATE_VAR(pc));
			dispatcherInEAXNoCheck = GetCodePtr();

#ifdef MASKED_PSP_MEMORY
			AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
#endif
			dispatcherFetch = GetCodePtr();
#if PPSSPP_ARCH(X86)
			_assert_msg_( Memory::base != 0, "Memory base bogus");
			MOV(32, R(EAX), MDisp(EAX, (u32)Memory::base));
#elif PPSSPP_ARCH(AMD64)
			MOV(32, R(EAX), MComplex(MEMBASEREG, RAX, SCALE_1, 0));
#endif
			MOV(32, R(EDX), R(EAX));
			_assert_msg_(MIPS_JITBLOCK_MASK == 0xFF000000, "Hardcoded assumption of emuhack mask");
			SHR(32, R(EDX), Imm8(24));
			CMP(32, R(EDX), Imm8(MIPS_EMUHACK_OPCODE >> 24));
			FixupBranch notfound = J_CC(CC_NE);
				if (enableDebug) {
					ADD(32, MIPSSTATE_VAR(debugCount), Imm8(1));
				}
				//grab from list and jump to it
				AND(32, R(EAX), Imm32(MIPS_EMUHACK_VALUE_MASK));
#if PPSSPP_ARCH(X86)
				ADD(32, R(EAX), ImmPtr(GetBasePtr()));
#elif PPSSPP_ARCH(AMD64)
				if (jo.reserveR15ForAsm) {
					ADD(64, R(RAX), R(JITBASEREG));
				} else {
					// See above, reserveR15ForAsm is used when above 0x7FFFFFFF.
					ADD(64, R(EAX), Imm32((u32)jitbase));
				}
#endif
				JMPptr(R(EAX));
			SetJumpTarget(notfound);

			//Ok, no block, let's jit
			RestoreRoundingMode(true);
			ABI_CallFunction(&MIPSComp::JitAt);
			ApplyRoundingMode(true);
			JMP(dispatcherNoCheck, true); // Let's just dispatch again, we'll enter the block since we know it's there.

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm32(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm32(0));
		}
		J_CC(CC_Z, outerLoop, true);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);
	RestoreRoundingMode(true);
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();

	crashHandler = GetCodePtr();
	if (RipAccessible((const void *)&coreState)) {
		MOV(32, M(&coreState), Imm32(CORE_RUNTIME_ERROR));
	} else {
		MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
		MOV(32, MatR(RAX), Imm32(CORE_RUNTIME_ERROR));
	}
	JMP(quitLoop, true);

	// Let's spare the pre-generated code from unprotect-reprotect.
	endOfPregeneratedCode = AlignCodePage();
	EndWrite();
}

}  // namespace

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
