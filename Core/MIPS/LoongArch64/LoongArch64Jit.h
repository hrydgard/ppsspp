// Copyright (c) 2025- PPSSPP Project.

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

#include <string>
#include <vector>
#include "Common/LoongArch64Emitter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"

namespace MIPSComp {

class LoongArch64JitBackend : public LoongArch64Gen::LoongArch64CodeBlock, public IRNativeBackend {
public:
    LoongArch64JitBackend(JitOptions &jo, IRBlockCache &blocks);
    ~LoongArch64JitBackend();

	bool DescribeCodePtr(const u8 *ptr, std::string &name) const override;

    void GenerateFixedCode(MIPSState *mipsState) override;
	bool CompileBlock(IRBlockCache *irBlockCache, int block_num) override;
	void ClearAllBlocks() override;
	void InvalidateBlock(IRBlockCache *irBlockCache, int block_num) override;

protected:
	const CodeBlockCommon &CodeBlock() const override {
		return *this;
	}
private:
    void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void MovFromPC(LoongArch64Gen::LoongArch64Reg r);
	void MovToPC(LoongArch64Gen::LoongArch64Reg r);
	void WriteDebugPC(uint32_t pc);
	void WriteDebugPC(LoongArch64Gen::LoongArch64Reg r);
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
	void CompIR_VecClamp(IRInst inst) override;
	void CompIR_VecHoriz(IRInst inst) override;
	void CompIR_VecLoad(IRInst inst) override;
	void CompIR_VecPack(IRInst inst) override;
	void CompIR_VecStore(IRInst inst) override;
	void CompIR_ValidateAddress(IRInst inst) override;

    void SetScratch1ToSrc1Address(IRReg src1);
	// Modifies SCRATCH regs.
	int32_t AdjustForAddressOffset(LoongArch64Gen::LoongArch64Reg *reg, int32_t constant, int32_t range = 0);
	void NormalizeSrc1(IRInst inst, LoongArch64Gen::LoongArch64Reg *reg, LoongArch64Gen::LoongArch64Reg tempReg, bool allowOverlap);
	void NormalizeSrc12(IRInst inst, LoongArch64Gen::LoongArch64Reg *lhs, LoongArch64Gen::LoongArch64Reg *rhs, LoongArch64Gen::LoongArch64Reg lhsTempReg, LoongArch64Gen::LoongArch64Reg rhsTempReg, bool allowOverlap);
	LoongArch64Gen::LoongArch64Reg NormalizeR(IRReg rs, IRReg rd, LoongArch64Gen::LoongArch64Reg tempReg);

    JitOptions &jo;
	LoongArch64RegCache regs_;

    const u8 *outerLoop_ = nullptr;
	const u8 *outerLoopPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherCheckCoreState_ = nullptr;
	const u8 *dispatcherPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherNoCheck_ = nullptr;
    const u8 *applyRoundingMode_ = nullptr;

	const u8 *saveStaticRegisters_ = nullptr;
	const u8 *loadStaticRegisters_ = nullptr;

	int jitStartOffset_ = 0;
	int compilingBlockNum_ = -1;
	int logBlocks_ = 0;
};

class LoongArch64Jit : public IRNativeJit{
public:
    LoongArch64Jit(MIPSState *mipsState) : IRNativeJit(mipsState), la64Backend_(jo, blocks_) {
        Init(la64Backend_);
    }
private:
    LoongArch64JitBackend la64Backend_;
};

} // namespace MIPSComp