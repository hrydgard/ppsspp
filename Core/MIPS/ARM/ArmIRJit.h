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

#pragma once

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include <string>
#include <vector>
#include "Common/ArmEmitter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

namespace MIPSComp {

class ArmJitBackend : public ArmGen::ARMXCodeBlock, public IRNativeBackend {
public:
	ArmJitBackend(JitOptions &jo, IRBlockCache &blocks);
	~ArmJitBackend();

	bool DescribeCodePtr(const u8 *ptr, std::string &name) const override;

	void GenerateFixedCode(MIPSState *mipsState) override;
	bool CompileBlock(IRBlockCache *irBlockCache, int block_num) override;
	void ClearAllBlocks() override;
	void InvalidateBlock(IRBlockCache *irBlockCache, int block_num) override;

protected:
	void GetFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const override;

	const CodeBlockCommon &CodeBlock() const override {
		return *this;
	}

private:
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode(bool force = false);
	void MovFromPC(ArmGen::ARMReg r);
	void MovToPC(ArmGen::ARMReg r);
	// These destroy SCRATCH2 and R12 (the uint32_t one), so they're only for when R12 isn't
	// mapped: at block entry, in the dispatcher, or after a flush. SCRATCH1 (R0) is left alone,
	// since it holds the PC or a return value around them.
	void WriteDebugPC(uint32_t pc);
	void WriteDebugPC(ArmGen::ARMReg r);
	void WriteDebugProfilerStatus(IRProfilerStatus status);

	void SaveStaticRegisters();
	void LoadStaticRegisters();

	// Note: destroys SCRATCH1.
	void FlushAll();

	void WriteConstExit(uint32_t pc);
	void OverwriteExit(int srcOffset, int len, int block_num) override;

	void CompIR_Arith(IRInst inst) override;
	void CompIR_Assign(IRInst inst) override;
	void CompIR_Basic(IRInst inst) override;
	void CompIR_Bits(IRInst inst) override;
	void CompIR_Breakpoint(IRInst inst) override;
	void CompIR_Compare(IRInst inst) override;
	void CompIR_CondAssign(IRInst inst) override;
	void CompIR_CondStore(IRInst inst) override;
	void CompIR_Div(IRInst inst) override;
	void CompIR_Exit(IRInst inst) override;
	void CompIR_ExitIf(IRInst inst) override;
	void CompIR_FArith(IRInst inst) override;
	void CompIR_FAssign(IRInst inst) override;
	void CompIR_FCompare(IRInst inst) override;
	void CompIR_FCondAssign(IRInst inst) override;
	void CompIR_FCvt(IRInst inst) override;
	void CompIR_FLoad(IRInst inst) override;
	void CompIR_FRound(IRInst inst) override;
	void CompIR_FSat(IRInst inst) override;
	void CompIR_FSpecial(IRInst inst) override;
	void CompIR_FStore(IRInst inst) override;
	void CompIR_Generic(IRInst inst) override;
	void CompIR_HiLo(IRInst inst) override;
	void CompIR_Interpret(IRInst inst) override;
	void CompIR_Load(IRInst inst) override;
	void CompIR_LoadShift(IRInst inst) override;
	void CompIR_Logic(IRInst inst) override;
	void CompIR_Mult(IRInst inst) override;
	void CompIR_RoundingMode(IRInst inst) override;
	void CompIR_Shift(IRInst inst) override;
	void CompIR_Store(IRInst inst) override;
	void CompIR_StoreShift(IRInst inst) override;
	void CompIR_System(IRInst inst) override;
	void CompIR_Transfer(IRInst inst) override;
	void CompIR_VecArith(IRInst inst) override;
	void CompIR_VecAssign(IRInst inst) override;
	void CompIR_VecHoriz(IRInst inst) override;
	void CompIR_VecLoad(IRInst inst) override;
	void CompIR_VecPack(IRInst inst) override;
	void CompIR_VecStore(IRInst inst) override;
	void CompIR_ValidateAddress(IRInst inst) override;

	// rmode is an ARM rounding mode, or -1 for FPSCR's. Destroys SCRATCH1 and SCRATCH2.
	void CompFloatToIntWithRounding(ArmGen::ARMReg dest, ArmGen::ARMReg src, int rmode);
	// Puts the masked address in SCRATCH1, as an offset from MEMBASEREG. Destroys SCRATCH2.
	void PrepareSrc1Address(IRInst inst);
	// Vec4VfpuDot with NEON (see ArmVfpuDot.h).
	void CompVfpuDot(IRInst inst);

	JitOptions &jo;
	ArmIRRegCache regs_;

	const u8 *outerLoop_ = nullptr;
	const u8 *outerLoopPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherCheckCoreState_ = nullptr;
	const u8 *dispatcherPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherNoCheck_ = nullptr;
	const u8 *restoreRoundingMode_ = nullptr;
	const u8 *applyRoundingMode_ = nullptr;
	// See GenerateArmVfpuDotCall().
	const u8 *vfpuDot_ = nullptr;

	int jitStartOffset_ = 0;
	int compilingBlockNum_ = -1;
	int logBlocks_ = 0;
	// Only useful in breakpoints, where it's set immediately prior.
	uint32_t lastConstPC_ = 0;
};

class ArmIRJit : public IRNativeJit {
public:
	ArmIRJit(MIPSState *mipsState)
		: IRNativeJit(mipsState), armBackend_(jo, blocks_) {
		Init(armBackend_);
	}

private:
	ArmJitBackend armBackend_;
};

} // namespace MIPSComp

#endif
