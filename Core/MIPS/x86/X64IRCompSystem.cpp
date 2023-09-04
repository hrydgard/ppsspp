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
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
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
	{
		X64Reg addrBase = regs_.MapGPR(inst.src1);
		FlushAll();
		LEA(32, addrBase, MDisp(addrBase, inst.constant));
		MovFromPC(SCRATCH1);
		LEA(32, SCRATCH1, MDisp(SCRATCH1, inst.dest));
		ABI_CallFunctionRR((const void *)&IRRunMemCheck, SCRATCH1, addrBase);
		TEST(32, R(EAX), R(EAX));
		J_CC(CC_NZ, dispatcherCheckCoreState_, true);
		break;
	}

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

		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		ABI_CallFunction(GetReplacementFunc(inst.constant)->replaceFunc);
		LoadStaticRegisters();
		//SUB(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG), R(EAX));
		SUB(32, MDisp(CTXREG, downcountOffset), R(EAX));
		break;

	case IROp::Break:
		CompIR_Generic(inst);
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
	case IROp::FpCtrlToReg:
		CompIR_Generic(inst);
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

int ReportBadAddress(uint32_t addr, uint32_t alignment, uint32_t isWrite) {
	const auto toss = [&](MemoryExceptionType t) {
		Core_MemoryException(addr, alignment, currentMIPS->pc, t);
		return coreState != CORE_RUNNING ? 1 : 0;
	};

	if (!Memory::IsValidRange(addr, alignment)) {
		MemoryExceptionType t = isWrite == 1 ? MemoryExceptionType::WRITE_WORD : MemoryExceptionType::READ_WORD;
		if (alignment > 4)
			t = isWrite ? MemoryExceptionType::WRITE_BLOCK : MemoryExceptionType::READ_BLOCK;
		return toss(t);
	} else if (alignment > 1 && (addr & (alignment - 1)) != 0) {
		return toss(MemoryExceptionType::ALIGNMENT);
	}
	return 0;
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

	// This is unfortunate...
	FlushAll();
	regs_.Map(inst);
	LEA(PTRBITS, SCRATCH1, MDisp(regs_.RX(inst.src1), inst.constant));
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
	validJumps.push_back(J_CC(CC_AE));

	CMP(32, R(SCRATCH1), Imm32(PSP_GetVidMemEnd() - alignment));
	FixupBranch tooHighVid = J_CC(CC_A);
	CMP(32, R(SCRATCH1), Imm32(PSP_GetVidMemBase()));
	validJumps.push_back(J_CC(CC_AE));

	CMP(32, R(SCRATCH1), Imm32(PSP_GetScratchpadMemoryEnd() - alignment));
	FixupBranch tooHighScratch = J_CC(CC_A);
	CMP(32, R(SCRATCH1), Imm32(PSP_GetScratchpadMemoryBase()));
	validJumps.push_back(J_CC(CC_AE));

	SetJumpTarget(tooHighRAM);
	SetJumpTarget(tooHighVid);
	SetJumpTarget(tooHighScratch);

	ABI_CallFunctionACC((const void *)&ReportBadAddress, R(SCRATCH1), alignment, isWrite);
	TEST(32, R(EAX), R(EAX));
	validJumps.push_back(J_CC(CC_Z));
	JMP(dispatcherCheckCoreState_, true);

	for (FixupBranch &b : validJumps)
		SetJumpTarget(b);
}

} // namespace MIPSComp

#endif
