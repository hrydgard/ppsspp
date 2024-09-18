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

#include "Common/Profiler/Profiler.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for basic PC/downcount accounting, syscalls, debug funcs, etc.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Gen;
using namespace X64IRJitConstants;

void X64JitBackend::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Downcount:
		// As long as we don't care about flags, just use LEA.
		if (jo.downcountInRegister)
			LEA(32, DOWNCOUNTREG, MDisp(DOWNCOUNTREG, -(s32)inst.constant));
		else
			SUB(32, MDisp(CTXREG, downcountOffset), SImmAuto((s32)inst.constant));
		break;

	case IROp::SetConst:
		regs_.SetGPRImm(inst.dest, inst.constant);
		break;

	case IROp::SetConstF:
		regs_.Map(inst);
		if (inst.constant == 0) {
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		} else if (inst.constant == 0x7FFFFFFF) {
			MOVSS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible
		} else if (inst.constant == 0x80000000) {
			MOVSS(regs_.FX(inst.dest), M(constants.signBitAll));  // rip accessible
		} else if (inst.constant == 0x7F800000) {
			MOVSS(regs_.FX(inst.dest), M(constants.positiveInfinity));  // rip accessible
		} else if (inst.constant == 0x7FC00000) {
			MOVSS(regs_.FX(inst.dest), M(constants.qNAN));  // rip accessible
		} else if (inst.constant == 0x3F800000) {
			MOVSS(regs_.FX(inst.dest), M(constants.positiveOnes));  // rip accessible
		} else if (inst.constant == 0xBF800000) {
			MOVSS(regs_.FX(inst.dest), M(constants.negativeOnes));  // rip accessible
		} else if (inst.constant == 0x4EFFFFFF) {
			MOVSS(regs_.FX(inst.dest), M(constants.maxIntBelowAsFloat));  // rip accessible
		} else {
			MOV(32, R(SCRATCH1), Imm32(inst.constant));
			MOVD_xmm(regs_.FX(inst.dest), R(SCRATCH1));
		}
		break;

	case IROp::SetPC:
		regs_.Map(inst);
		MovToPC(regs_.RX(inst.src1));
		break;

	case IROp::SetPCConst:
		lastConstPC_ = inst.constant;
		MOV(32, R(SCRATCH1), Imm32(inst.constant));
		MovToPC(SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Breakpoint(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Breakpoint:
		FlushAll();
		// Note: the constant could be a delay slot.
		ABI_CallFunctionC((const void *)&IRRunBreakpoint, inst.constant);
		TEST(32, R(EAX), R(EAX));
		J_CC(CC_NZ, dispatcherCheckCoreState_, true);
		break;

	case IROp::MemoryCheck:
		if (regs_.IsGPRImm(inst.src1)) {
			uint32_t iaddr = regs_.GetGPRImm(inst.src1) + inst.constant;
			uint32_t checkedPC = lastConstPC_ + inst.dest;
			int size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			if (size == 0) {
				checkedPC += 4;
				size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			}
			bool isWrite = MIPSAnalyst::IsOpMemoryWrite(checkedPC);

			MemCheck check;
			if (CBreakPoints::GetMemCheckInRange(iaddr, size, &check)) {
				if (!(check.cond & MEMCHECK_READ) && !isWrite)
					break;
				if (!(check.cond & (MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE)) && isWrite)
					break;

				// We need to flush, or conditions and log expressions will see old register values.
				FlushAll();

				ABI_CallFunctionCC((const void *)&IRRunMemCheck, checkedPC, iaddr);
				TEST(32, R(EAX), R(EAX));
				J_CC(CC_NZ, dispatcherCheckCoreState_, true);
			}
		} else {
			uint32_t checkedPC = lastConstPC_ + inst.dest;
			int size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			if (size == 0) {
				checkedPC += 4;
				size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			}
			bool isWrite = MIPSAnalyst::IsOpMemoryWrite(checkedPC);

			const auto memchecks = CBreakPoints::GetMemCheckRanges(isWrite);
			// We can trivially skip if there are no checks for this type (i.e. read vs write.)
			if (memchecks.empty())
				break;

			X64Reg addrBase = regs_.MapGPR(inst.src1);
			LEA(32, SCRATCH1, MDisp(addrBase, inst.constant));

			// We need to flush, or conditions and log expressions will see old register values.
			FlushAll();

			std::vector<FixupBranch> hitChecks;
			for (const auto &it : memchecks) {
				if (it.end != 0) {
					CMP(32, R(SCRATCH1), Imm32(it.start - size));
					FixupBranch skipNext = J_CC(CC_BE);

					CMP(32, R(SCRATCH1), Imm32(it.end));
					hitChecks.push_back(J_CC(CC_B, true));

					SetJumpTarget(skipNext);
				} else {
					CMP(32, R(SCRATCH1), Imm32(it.start));
					hitChecks.push_back(J_CC(CC_E, true));
				}
			}

			FixupBranch noHits = J(true);

			// Okay, now land any hit here.
			for (auto &fixup : hitChecks)
				SetJumpTarget(fixup);
			hitChecks.clear();

			ABI_CallFunctionAA((const void *)&IRRunMemCheck, Imm32(checkedPC), R(SCRATCH1));
			TEST(32, R(EAX), R(EAX));
			J_CC(CC_NZ, dispatcherCheckCoreState_, true);

			SetJumpTarget(noHits);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Syscall:
		FlushAll();
		SaveStaticRegisters();

		WriteDebugProfilerStatus(IRProfilerStatus::SYSCALL);
#ifdef USE_PROFILER
		// When profiling, we can't skip CallSyscall, since it times syscalls.
		ABI_CallFunctionC((const u8 *)&CallSyscall, inst.constant);
#else
		// Skip the CallSyscall where possible.
		{
			MIPSOpcode op(inst.constant);
			void *quickFunc = GetQuickSyscallFunc(op);
			if (quickFunc) {
				ABI_CallFunctionP((const u8 *)quickFunc, (void *)GetSyscallFuncPointer(op));
			} else {
				ABI_CallFunctionC((const u8 *)&CallSyscall, inst.constant);
			}
		}
#endif

		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		WriteDebugProfilerStatus(IRProfilerStatus::REPLACEMENT);
		ABI_CallFunction(GetReplacementFunc(inst.constant)->replaceFunc);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();

		// Since we flushed above, and we're mapping write, EAX should be safe.
		regs_.Map(inst);
		MOV(32, regs_.R(inst.dest), R(EAX));
		NEG(32, R(EAX));
		// Set it back if it negate made it negative.  That's the absolute value.
		CMOVcc(32, EAX, regs_.R(inst.dest), CC_S);

		// Now set the dest to the sign bit status.
		SAR(32, regs_.R(inst.dest), Imm8(31));

		if (jo.downcountInRegister)
			SUB(32, R(DOWNCOUNTREG), R(EAX));
		else
			SUB(32, MDisp(CTXREG, downcountOffset), R(EAX));
		break;

	case IROp::Break:
		FlushAll();
		// This doesn't naturally have restore/apply around it.
		RestoreRoundingMode(true);
		SaveStaticRegisters();
		MovFromPC(SCRATCH1);
		ABI_CallFunctionR((const void *)&Core_Break, SCRATCH1);
		LoadStaticRegisters();
		ApplyRoundingMode(true);
		MovFromPC(SCRATCH1);
		LEA(32, SCRATCH1, MDisp(SCRATCH1, 4));
		JMP(dispatcherPCInSCRATCH1_, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Transfer(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetCtrlVFPU:
		regs_.SetGPRImm(IRREG_VFPU_CTRL_BASE + inst.dest, (int32_t)inst.constant);
		break;

	case IROp::SetCtrlVFPUReg:
		regs_.Map(inst);
		MOV(32, regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.R(inst.src1));
		break;

	case IROp::SetCtrlVFPUFReg:
		regs_.Map(inst);
		MOVD_xmm(regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.FX(inst.src1));
		break;

	case IROp::FpCondFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		MOV(32, regs_.R(IRREG_FPCOND), regs_.R(inst.src1));
		break;

	case IROp::FpCondToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_FPCOND));
		break;

	case IROp::FpCtrlFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		// Mask out the unused bits, and store fcr31 (using fpcond as a temp.)
		MOV(32, regs_.R(IRREG_FPCOND), Imm32(0x0181FFFF));
		AND(32, regs_.R(IRREG_FPCOND), regs_.R(inst.src1));
		MOV(32, MDisp(CTXREG, fcr31Offset), regs_.R(IRREG_FPCOND));

		// With that done, grab bit 23, the actual fpcond.
		SHR(32, regs_.R(IRREG_FPCOND), Imm8(23));
		AND(32, regs_.R(IRREG_FPCOND), Imm32(1));
		break;

	case IROp::FpCtrlToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		// Start by clearing the fpcond bit (might as well mask while we're here.)
		MOV(32, regs_.R(inst.dest), Imm32(0x0101FFFF));
		AND(32, regs_.R(inst.dest), MDisp(CTXREG, fcr31Offset));

		AND(32, regs_.R(IRREG_FPCOND), Imm32(1));
		if (cpu_info.bBMI2) {
			RORX(32, SCRATCH1, regs_.R(IRREG_FPCOND), 32 - 23);
		} else {
			MOV(32, R(SCRATCH1), regs_.R(IRREG_FPCOND));
			SHL(32, R(SCRATCH1), Imm8(23));
		}
		OR(32, regs_.R(inst.dest), R(SCRATCH1));

		// Update fcr31 while we were here, for consistency.
		MOV(32, MDisp(CTXREG, fcr31Offset), regs_.R(inst.dest));
		break;

	case IROp::VfpuCtrlToReg:
		regs_.Map(inst);
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_VFPU_CTRL_BASE + inst.src1));
		break;

	case IROp::FMovFromGPR:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		} else {
			regs_.Map(inst);
			MOVD_xmm(regs_.FX(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		regs_.Map(inst);
		MOVD_xmm(regs_.R(inst.dest), regs_.FX(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_ValidateAddress(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool isWrite = inst.src2 & 1;
	int alignment = 0;
	switch (inst.op) {
	case IROp::ValidateAddress8:
		alignment = 1;
		break;

	case IROp::ValidateAddress16:
		alignment = 2;
		break;

	case IROp::ValidateAddress32:
		alignment = 4;
		break;

	case IROp::ValidateAddress128:
		alignment = 16;
		break;

	default:
		INVALIDOP;
		break;
	}

	if (regs_.IsGPRMappedAsPointer(inst.src1)) {
		LEA(PTRBITS, SCRATCH1, MDisp(regs_.RXPtr(inst.src1), inst.constant));
#if defined(MASKED_PSP_MEMORY)
		SUB(PTRBITS, R(SCRATCH1), ImmPtr(Memory::base));
#else
		SUB(PTRBITS, R(SCRATCH1), R(MEMBASEREG));
#endif
	} else {
		regs_.Map(inst);
		LEA(PTRBITS, SCRATCH1, MDisp(regs_.RX(inst.src1), inst.constant));
	}
	AND(32, R(SCRATCH1), Imm32(0x3FFFFFFF));

	std::vector<FixupBranch> validJumps;

	FixupBranch unaligned;
	if (alignment != 1) {
		TEST(32, R(SCRATCH1), Imm32(alignment - 1));
		unaligned = J_CC(CC_NZ);
	}

	CMP(32, R(SCRATCH1), Imm32(PSP_GetUserMemoryEnd() - alignment));
	FixupBranch tooHighRAM = J_CC(CC_A);
	CMP(32, R(SCRATCH1), Imm32(PSP_GetKernelMemoryBase()));
	validJumps.push_back(J_CC(CC_AE, true));

	CMP(32, R(SCRATCH1), Imm32(PSP_GetVidMemEnd() - alignment));
	FixupBranch tooHighVid = J_CC(CC_A);
	CMP(32, R(SCRATCH1), Imm32(PSP_GetVidMemBase()));
	validJumps.push_back(J_CC(CC_AE, true));

	CMP(32, R(SCRATCH1), Imm32(PSP_GetScratchpadMemoryEnd() - alignment));
	FixupBranch tooHighScratch = J_CC(CC_A);
	CMP(32, R(SCRATCH1), Imm32(PSP_GetScratchpadMemoryBase()));
	validJumps.push_back(J_CC(CC_AE, true));

	if (alignment != 1)
		SetJumpTarget(unaligned);
	SetJumpTarget(tooHighRAM);
	SetJumpTarget(tooHighVid);
	SetJumpTarget(tooHighScratch);

	// If we got here, something unusual and bad happened, so we'll always go back to the dispatcher.
	// Because of that, we can avoid flushing outside this case.
	auto regsCopy = regs_;
	regsCopy.FlushAll();

	// Ignores the return value, always returns to the dispatcher.
	// Otherwise would need a thunk to restore regs.
	ABI_CallFunctionACC((const void *)&ReportBadAddress, R(SCRATCH1), alignment, isWrite);
	JMP(dispatcherCheckCoreState_, true);

	for (FixupBranch &b : validJumps)
		SetJumpTarget(b);
}

} // namespace MIPSComp

#endif
