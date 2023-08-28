// Copyright (c) 2023- PPSSPP Project.

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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Common/Log.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/System.h"

namespace MIPSComp {

using namespace Gen;
using namespace X64IRJitConstants;

static const bool enableDebug = false;
static const bool enableDisasm = false;

static void ShowPC(void *membase, void *jitbase) {
	static int count = 0;
	if (currentMIPS) {
		u32 downcount = currentMIPS->downcount;
		ERROR_LOG(JIT, "[%08x] ShowPC  Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
	} else {
		ERROR_LOG(JIT, "Universe corrupt?");
	}
	//if (count > 2000)
	//	exit(0);
	count++;
}

void X64JitBackend::GenerateFixedCode(MIPSState *mipsState) {
	BeginWrite(GetMemoryProtectPageSize());
	const u8 *start = AlignCodePage();

	if (jo.useStaticAlloc && false) {
		saveStaticRegisters_ = AlignCode16();
		//MOV(32, MDisp(CTXREG, downcountOffset), R(DOWNCOUNTREG));
		//regs_.EmitSaveStaticRegisters();
		RET();

		// Note: needs to not modify EAX, or to save it if it does.
		loadStaticRegisters_ = AlignCode16();
		//regs_.EmitLoadStaticRegisters();
		//MOV(32, R(DOWNCOUNTREG), MDisp(CTXREG, downcountOffset));
		RET();

		start = saveStaticRegisters_;
	} else {
		saveStaticRegisters_ = nullptr;
		loadStaticRegisters_ = nullptr;
	}

	restoreRoundingMode_ = AlignCode16();
	{
		STMXCSR(MDisp(CTXREG, tempOffset));
		// Clear the rounding mode and flush-to-zero bits back to 0.
		AND(32, MDisp(CTXREG, tempOffset), Imm32(~(7 << 13)));
		LDMXCSR(MDisp(CTXREG, tempOffset));
		RET();
	}

	applyRoundingMode_ = AlignCode16();
	{
		MOV(32, R(SCRATCH1), MDisp(CTXREG, fcr31Offset));
		AND(32, R(SCRATCH1), Imm32(0x01000003));

		// If it's 0 (nearest + no flush0), we don't actually bother setting - we cleared the rounding
		// mode out in restoreRoundingMode anyway. This is the most common.
		FixupBranch skip = J_CC(CC_Z);
		STMXCSR(MDisp(CTXREG, tempOffset));

		// The MIPS bits don't correspond exactly, so we have to adjust.
		// 0 -> 0 (skip2), 1 -> 3, 2 -> 2 (skip2), 3 -> 1
		TEST(8, R(AL), Imm8(1));
		FixupBranch skip2 = J_CC(CC_Z);
		XOR(32, R(SCRATCH1), Imm8(2));
		SetJumpTarget(skip2);

		// Adjustment complete, now reconstruct MXCSR
		SHL(32, R(SCRATCH1), Imm8(13));
		// Before setting new bits, we must clear the old ones.
		// Clearing bits 13-14 (rounding mode) and 15 (flush to zero.)
		AND(32, MDisp(CTXREG, tempOffset), Imm32(~(7 << 13)));
		OR(32, MDisp(CTXREG, tempOffset), R(SCRATCH1));

		TEST(32, MDisp(CTXREG, fcr31Offset), Imm32(1 << 24));
		FixupBranch skip3 = J_CC(CC_Z);
		OR(32, MDisp(CTXREG, tempOffset), Imm32(1 << 15));
		SetJumpTarget(skip3);

		LDMXCSR(MDisp(CTXREG, tempOffset));
		SetJumpTarget(skip);
		RET();
	}

	hooks_.enterDispatcher = (IRNativeFuncNoArg)AlignCode16();

	ABI_PushAllCalleeSavedRegsAndAdjustStack();
#if PPSSPP_ARCH(AMD64)
	// Two x64-specific statically allocated registers.
	MOV(64, R(MEMBASEREG), ImmPtr(Memory::base));
	uintptr_t jitbase = (uintptr_t)GetBasePtr();
	if (jitbase > 0x7FFFFFFFULL) {
		MOV(64, R(JITBASEREG), ImmPtr(GetBasePtr()));
		jo.reserveR15ForAsm = true;
	}
#endif
	// From the start of the FP reg, a single byte offset can reach all GPR + all FPR (but not VFPR.)
	MOV(PTRBITS, R(CTXREG), ImmPtr(&mipsState->f[0]));

	LoadStaticRegisters();
	MovFromPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop_ = GetCodePtr();
		// Advance can change the downcount (or thread), so must save/restore around it.
		SaveStaticRegisters();
		RestoreRoundingMode(true);
		ABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		ApplyRoundingMode(true);
		LoadStaticRegisters();

		dispatcherCheckCoreState_ = GetCodePtr();
		// TODO: See if we can get the slice decrement to line up with IR.

		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm32(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm32(0));
		}
		FixupBranch badCoreState = J_CC(CC_NZ, true);

		//CMP(32, R(DOWNCOUNTREG), Imm32(0));
		CMP(32, MDisp(CTXREG, downcountOffset), Imm32(0));
		J_CC(CC_S, outerLoop_);
		FixupBranch skipToRealDispatch = J();

		dispatcherPCInSCRATCH1_ = GetCodePtr();
		MovToPC(SCRATCH1);

		hooks_.dispatcher = GetCodePtr();

			// TODO: See if we can get the slice decrement to line up with IR.
			//CMP(32, R(DOWNCOUNTREG), Imm32(0));
			CMP(32, MDisp(CTXREG, downcountOffset), Imm32(0));
			FixupBranch bail = J_CC(CC_S, true);
			SetJumpTarget(skipToRealDispatch);

			dispatcherNoCheck_ = GetCodePtr();

			// Debug
			if (enableDebug) {
#if PPSSPP_ARCH(AMD64)
				if (jo.reserveR15ForAsm)
					ABI_CallFunctionAA(reinterpret_cast<void *>(&ShowPC), R(MEMBASEREG), R(JITBASEREG));
				else
					ABI_CallFunctionAC(reinterpret_cast<void *>(&ShowPC), R(MEMBASEREG), (u32)jitbase);
#else
				ABI_CallFunctionCC(reinterpret_cast<void *>(&ShowPC), (u32)Memory::base, (u32)GetBasePtr());
#endif
			}

			MovFromPC(SCRATCH1);
#ifdef MASKED_PSP_MEMORY
			AND(32, R(SCRATCH1), Imm32(Memory::MEMVIEW32_MASK));
#endif
			hooks_.dispatchFetch = GetCodePtr();
#if PPSSPP_ARCH(X86)
			_assert_msg_( Memory::base != 0, "Memory base bogus");
			MOV(32, R(SCRATCH1), MDisp(SCRATCH1, (u32)Memory::base));
#elif PPSSPP_ARCH(AMD64)
			MOV(32, R(SCRATCH1), MComplex(MEMBASEREG, SCRATCH1, SCALE_1, 0));
#endif
			MOV(32, R(EDX), R(SCRATCH1));
			_assert_msg_(MIPS_JITBLOCK_MASK == 0xFF000000, "Hardcoded assumption of emuhack mask");
			SHR(32, R(EDX), Imm8(24));
			CMP(32, R(EDX), Imm8(MIPS_EMUHACK_OPCODE >> 24));
			FixupBranch needsCompile = J_CC(CC_NE);
				// Mask by 0x00FFFFFF and extract the block jit offset.
				AND(32, R(SCRATCH1), Imm32(MIPS_EMUHACK_VALUE_MASK));
#if PPSSPP_ARCH(X86)
				ADD(32, R(SCRATCH1), ImmPtr(GetBasePtr()));
#elif PPSSPP_ARCH(AMD64)
				if (jo.reserveR15ForAsm) {
					ADD(64, R(SCRATCH1), R(JITBASEREG));
				} else {
					// See above, reserveR15ForAsm is used when above 0x7FFFFFFF.
					ADD(64, R(SCRATCH1), Imm32((u32)jitbase));
				}
#endif
				JMPptr(R(SCRATCH1));
			SetJumpTarget(needsCompile);

			// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
			RestoreRoundingMode(true);
			ABI_CallFunction(&MIPSComp::JitAt);
			ApplyRoundingMode(true);
			// Let's just dispatch again, we'll enter the block since we know it's there.
			JMP(dispatcherNoCheck_, true);

		SetJumpTarget(bail);

		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm32(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm32(0));
		}
		J_CC(CC_Z, outerLoop_, true);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	SaveStaticRegisters();
	RestoreRoundingMode(true);
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();

	hooks_.crashHandler = GetCodePtr();
	if (RipAccessible((const void *)&coreState)) {
		MOV(32, M(&coreState), Imm32(CORE_RUNTIME_ERROR));
	} else {
		MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
		MOV(32, MatR(RAX), Imm32(CORE_RUNTIME_ERROR));
	}
	JMP(quitLoop, true);


	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
#if PPSSPP_ARCH(AMD64)
		std::vector<std::string> lines = DisassembleX86(start, (int)(GetCodePtr() - start));
		for (auto s : lines) {
			INFO_LOG(JIT, "%s", s.c_str());
		}
#endif
	}

	// Let's spare the pre-generated code from unprotect-reprotect.
	AlignCodePage();
	jitStartOffset_ = (int)(GetCodePtr() - start);
	EndWrite();
}

} // namespace MIPSComp

#endif
