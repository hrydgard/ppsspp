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
		ERROR_LOG(Log::JIT, "[%08x] ShowPC  Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
	} else {
		ERROR_LOG(Log::JIT, "Universe corrupt?");
	}
	//if (count > 2000)
	//	exit(0);
	count++;
}

void X64JitBackend::GenerateFixedCode(MIPSState *mipsState) {
	// This will be used as a writable scratch area, always 32-bit accessible.
	const u8 *start = AlignCodePage();
	if (DebugProfilerEnabled()) {
		ProtectMemoryPages(start, GetMemoryProtectPageSize(), MEM_PROT_READ | MEM_PROT_WRITE);
		hooks_.profilerPC = (uint32_t *)GetWritableCodePtr();
		Write32(0);
		hooks_.profilerStatus = (IRProfilerStatus *)GetWritableCodePtr();
		Write32(0);
	}

	EmitFPUConstants();
	EmitVecConstants();

	const u8 *disasmStart = AlignCodePage();
	BeginWrite(GetMemoryProtectPageSize());

	jo.downcountInRegister = false;
#if PPSSPP_ARCH(AMD64)
	bool jitbaseInR15 = false;
	int jitbaseCtxDisp = 0;
	// We pre-bake the MIPS_EMUHACK_OPCODE subtraction into our jitbase value.
	intptr_t jitbase = (intptr_t)GetBasePtr() - MIPS_EMUHACK_OPCODE;
	if ((jitbase < -0x80000000LL || jitbase > 0x7FFFFFFFLL) && !Accessible((const u8 *)&mipsState->f[0], (const u8 *)jitbase)) {
		jo.reserveR15ForAsm = true;
		jitbaseInR15 = true;
	} else {
		jo.downcountInRegister = true;
		jo.reserveR15ForAsm = true;
		if (jitbase < -0x80000000LL || jitbase > 0x7FFFFFFFLL) {
			jitbaseCtxDisp = (int)(jitbase - (intptr_t)&mipsState->f[0]);
		}
	}
#endif

	if (jo.useStaticAlloc && false) {
		saveStaticRegisters_ = AlignCode16();
		if (jo.downcountInRegister)
			MOV(32, MDisp(CTXREG, downcountOffset), R(DOWNCOUNTREG));
		//regs_.EmitSaveStaticRegisters();
		RET();

		// Note: needs to not modify EAX, or to save it if it does.
		loadStaticRegisters_ = AlignCode16();
		//regs_.EmitLoadStaticRegisters();
		if (jo.downcountInRegister)
			MOV(32, R(DOWNCOUNTREG), MDisp(CTXREG, downcountOffset));
		RET();
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
	if (jitbaseInR15)
		MOV(64, R(JITBASEREG), ImmPtr((const void *)jitbase));
#endif
	// From the start of the FP reg, a single byte offset can reach all GPR + all FPR (but not VFPR.)
	MOV(PTRBITS, R(CTXREG), ImmPtr(&mipsState->f[0]));

	LoadStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	MovFromPC(SCRATCH1);
	WriteDebugPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop_ = GetCodePtr();
		// Advance can change the downcount (or thread), so must save/restore around it.
		SaveStaticRegisters();
		RestoreRoundingMode(true);
		WriteDebugProfilerStatus(IRProfilerStatus::TIMER_ADVANCE);
		ABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		ApplyRoundingMode(true);
		LoadStaticRegisters();

		dispatcherCheckCoreState_ = GetCodePtr();
		// TODO: See if we can get the slice decrement to line up with IR.

		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm8(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm8(0));
		}
		FixupBranch badCoreState = J_CC(CC_NZ, true);

		if (jo.downcountInRegister) {
			TEST(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG));
		} else {
			CMP(32, MDisp(CTXREG, downcountOffset), Imm8(0));
		}
		J_CC(CC_S, outerLoop_);
		FixupBranch skipToRealDispatch = J();

		dispatcherPCInSCRATCH1_ = GetCodePtr();
		MovToPC(SCRATCH1);

		hooks_.dispatcher = GetCodePtr();

			// TODO: See if we can get the slice decrement to line up with IR.
			if (jo.downcountInRegister) {
				TEST(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG));
			} else {
				CMP(32, MDisp(CTXREG, downcountOffset), Imm8(0));
			}
			FixupBranch bail = J_CC(CC_S, true);
			SetJumpTarget(skipToRealDispatch);

			dispatcherNoCheck_ = GetCodePtr();

			// Debug
			if (enableDebug) {
#if PPSSPP_ARCH(AMD64)
				if (jitbaseInR15) {
					ABI_CallFunctionAA(reinterpret_cast<void *>(&ShowPC), R(MEMBASEREG), R(JITBASEREG));
				} else if (jitbaseCtxDisp != 0) {
					LEA(64, SCRATCH1, MDisp(CTXREG, jitbaseCtxDisp));
					ABI_CallFunctionAA(reinterpret_cast<void *>(&ShowPC), R(MEMBASEREG), R(SCRATCH1));
				} else {
					ABI_CallFunctionAC(reinterpret_cast<void *>(&ShowPC), R(MEMBASEREG), (u32)jitbase);
				}
#else
				ABI_CallFunctionCC(reinterpret_cast<void *>(&ShowPC), (u32)Memory::base, (u32)GetBasePtr());
#endif
			}

			MovFromPC(SCRATCH1);
			WriteDebugPC(SCRATCH1);
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
			_assert_msg_(MIPS_JITBLOCK_MASK == 0xFF000000, "Hardcoded assumption of emuhack mask");
			if (cpu_info.bBMI2) {
				RORX(32, EDX, R(SCRATCH1), 24);
				CMP(8, R(EDX), Imm8(MIPS_EMUHACK_OPCODE >> 24));
			} else {
				MOV(32, R(EDX), R(SCRATCH1));
				SHR(32, R(EDX), Imm8(24));
				CMP(32, R(EDX), Imm8(MIPS_EMUHACK_OPCODE >> 24));
			}
			FixupBranch needsCompile = J_CC(CC_NE);
				// We don't mask here - that's baked into jitbase.
#if PPSSPP_ARCH(X86)
				LEA(32, SCRATCH1, MDisp(SCRATCH1, (u32)GetBasePtr() - MIPS_EMUHACK_OPCODE));
#elif PPSSPP_ARCH(AMD64)
				if (jitbaseInR15) {
					ADD(64, R(SCRATCH1), R(JITBASEREG));
				} else if (jitbaseCtxDisp) {
					LEA(64, SCRATCH1, MComplex(CTXREG, SCRATCH1, SCALE_1, jitbaseCtxDisp));
				} else {
					// See above, reserveR15ForAsm is used when above 0x7FFFFFFF.
					LEA(64, SCRATCH1, MDisp(SCRATCH1, (s32)jitbase));
				}
#endif
				JMPptr(R(SCRATCH1));
			SetJumpTarget(needsCompile);

			// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
			RestoreRoundingMode(true);
			WriteDebugProfilerStatus(IRProfilerStatus::COMPILING);
			ABI_CallFunction(&MIPSComp::JitAt);
			WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
			ApplyRoundingMode(true);
			// Let's just dispatch again, we'll enter the block since we know it's there.
			JMP(dispatcherNoCheck_, true);

		SetJumpTarget(bail);

		if (RipAccessible((const void *)&coreState)) {
			CMP(32, M(&coreState), Imm8(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(RAX), ImmPtr((const void *)&coreState));
			CMP(32, MatR(RAX), Imm8(0));
		}
		J_CC(CC_Z, outerLoop_, true);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	WriteDebugProfilerStatus(IRProfilerStatus::NOT_RUNNING);
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
		std::vector<std::string> lines = DisassembleX86(disasmStart, (int)(GetCodePtr() - disasmStart));
		for (auto s : lines) {
			INFO_LOG(Log::JIT, "%s", s.c_str());
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
