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

#include "Common/CPUDetect.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

namespace MIPSComp {

// TODO : Use arena allocators. For now let's just malloc.
class IRBlock {
public:
	IRBlock() : instr_(nullptr), const_(nullptr), numInstructions_(0), numConstants_(0), origAddr_(0) {}
	IRBlock(u32 emAddr) : instr_(nullptr), const_(nullptr), origAddr_(emAddr), numInstructions_(0) {}
	IRBlock(IRBlock &&b) {
		instr_ = b.instr_;
		const_ = b.const_;
		numInstructions_ = b.numInstructions_;
		numConstants_ = b.numConstants_;
		origAddr_ = b.origAddr_;
		b.instr_ = nullptr;
		b.const_ = nullptr;
	}

	~IRBlock() {
		delete[] instr_;
		delete[] const_;
	}

	void SetInstructions(const std::vector<IRInst> &inst, const std::vector<u32> &constants) {
		instr_ = new IRInst[inst.size()];
		numInstructions_ = (u16)inst.size();
		memcpy(instr_, inst.data(), sizeof(IRInst) * inst.size());
		const_ = new u32[constants.size()];
		numConstants_ = (u16)constants.size();
		memcpy(const_, constants.data(), sizeof(u32) * constants.size());
	}

	const IRInst *GetInstructions() const { return instr_; }
	const u32 *GetConstants() const { return const_; }
	int GetNumInstructions() const { return numInstructions_; }
	MIPSOpcode GetOriginalFirstOp() const { return origFirstOpcode_; }

	void Finalize(int number);

private:
	IRInst *instr_;
	u32 *const_;
	u16 numInstructions_;
	u16 numConstants_;
	u32 origAddr_;
	MIPSOpcode origFirstOpcode_;
};

class IRBlockCache {
public:
	void Clear();
	void InvalidateICache(u32 addess, u32 length);
	int GetNumBlocks() const { return (int)blocks_.size(); }
	int AllocateBlock(int emAddr) {
		blocks_.emplace_back(IRBlock(emAddr));
		return (int)blocks_.size() - 1;
	}
	IRBlock *GetBlock(int i) {
		return &blocks_[i];
	}
private:
	std::vector<IRBlock> blocks_;
};

class IRJit : public JitInterface {
public:
	IRJit(MIPSState *mips);
	virtual ~IRJit();

	void DoState(PointerWrap &p) override;
	void DoDummyState(PointerWrap &p) override;

	const JitOptions &GetJitOptions() { return jo; }

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op) override;

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC
	void DoJit(u32 em_address, IRBlock *b);

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	void Comp_RunBlock(MIPSOpcode op) override;
	void Comp_ReplacementFunc(MIPSOpcode op) override;

	// Ops
	void Comp_ITypeMem(MIPSOpcode op) override;
	void Comp_Cache(MIPSOpcode op) override;

	void Comp_RelBranch(MIPSOpcode op) override;
	void Comp_RelBranchRI(MIPSOpcode op) override;
	void Comp_FPUBranch(MIPSOpcode op) override;
	void Comp_FPULS(MIPSOpcode op) override;
	void Comp_FPUComp(MIPSOpcode op) override;
	void Comp_Jump(MIPSOpcode op) override;
	void Comp_JumpReg(MIPSOpcode op) override;
	void Comp_Syscall(MIPSOpcode op) override;
	void Comp_Break(MIPSOpcode op) override;

	void Comp_IType(MIPSOpcode op) override;
	void Comp_RType2(MIPSOpcode op) override;
	void Comp_RType3(MIPSOpcode op) override;
	void Comp_ShiftType(MIPSOpcode op) override;
	void Comp_Allegrex(MIPSOpcode op) override;
	void Comp_Allegrex2(MIPSOpcode op) override;
	void Comp_VBranch(MIPSOpcode op) override;
	void Comp_MulDivType(MIPSOpcode op) override;
	void Comp_Special3(MIPSOpcode op) override;

	void Comp_FPU3op(MIPSOpcode op) override;
	void Comp_FPU2op(MIPSOpcode op) override;
	void Comp_mxc1(MIPSOpcode op) override;

	void Comp_DoNothing(MIPSOpcode op) override;

	void Comp_SV(MIPSOpcode op) override;
	void Comp_SVQ(MIPSOpcode op) override;
	void Comp_VPFX(MIPSOpcode op) override;
	void Comp_VVectorInit(MIPSOpcode op) override;
	void Comp_VMatrixInit(MIPSOpcode op) override;
	void Comp_VDot(MIPSOpcode op) override;
	void Comp_VecDo3(MIPSOpcode op) override;
	void Comp_VV2Op(MIPSOpcode op) override;
	void Comp_Mftv(MIPSOpcode op) override;
	void Comp_Vmfvc(MIPSOpcode op) override;
	void Comp_Vmtvc(MIPSOpcode op) override;
	void Comp_Vmmov(MIPSOpcode op) override;
	void Comp_VScl(MIPSOpcode op) override;
	void Comp_Vmmul(MIPSOpcode op) override;
	void Comp_Vmscl(MIPSOpcode op) override;
	void Comp_Vtfm(MIPSOpcode op) override;
	void Comp_VHdp(MIPSOpcode op) override;
	void Comp_VCrs(MIPSOpcode op) override;
	void Comp_VDet(MIPSOpcode op) override;
	void Comp_Vi2x(MIPSOpcode op) override;
	void Comp_Vx2i(MIPSOpcode op) override;
	void Comp_Vf2i(MIPSOpcode op) override;
	void Comp_Vi2f(MIPSOpcode op) override;
	void Comp_Vh2f(MIPSOpcode op) override;
	void Comp_Vcst(MIPSOpcode op) override;
	void Comp_Vhoriz(MIPSOpcode op) override;
	void Comp_VRot(MIPSOpcode op) override;
	void Comp_VIdt(MIPSOpcode op) override;
	void Comp_Vcmp(MIPSOpcode op) override;
	void Comp_Vcmov(MIPSOpcode op) override;
	void Comp_Viim(MIPSOpcode op) override;
	void Comp_Vfim(MIPSOpcode op) override;
	void Comp_VCrossQuat(MIPSOpcode op) override;
	void Comp_Vsgn(MIPSOpcode op) override;
	void Comp_Vocp(MIPSOpcode op) override;
	void Comp_ColorConv(MIPSOpcode op) override;
	void Comp_Vbfy(MIPSOpcode op) override;

	int Replace_fabsf();

	// Not using a regular block cache.
	JitBlockCache *GetBlockCache() override { return nullptr; }
	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

	void ClearCache();
	void InvalidateCache();
	void InvalidateCacheAt(u32 em_address, int length = 4);

	void EatPrefix() { js.EatPrefix(); }

	const u8 *GetDispatcher() const override {
		return dispatcher;
	}

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

private:
	void FlushAll();
	void FlushPrefixV();

	u32 GetCompilerPC();
	void CompileDelaySlot();
	void EatInstruction(MIPSOpcode op);
	MIPSOpcode GetOffsetInstruction(int offset);

	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode();

	bool ReplaceJalTo(u32 dest);

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, IRComparison cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, IRComparison cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, IRComparison cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, IRComparison cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, IROp op);
	void CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, IROp op, IROp constOp, bool symmetric = false);
	void CompShiftImm(MIPSOpcode op, IROp shiftType, int sa);
	void CompShiftVar(MIPSOpcode op, IROp shiftType, IROp shiftTypeConst);

	void ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz);
	void ApplyPrefixD(const u8 *vregs, VectorSize sz);
	void GetVectorRegsPrefixS(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixSFlag & JitState::PREFIX_KNOWN);
		GetVectorRegs(regs, sz, vectorReg);
		ApplyPrefixST(regs, js.prefixS, sz);
	}
	void GetVectorRegsPrefixT(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixTFlag & JitState::PREFIX_KNOWN);
		GetVectorRegs(regs, sz, vectorReg);
		ApplyPrefixST(regs, js.prefixT, sz);
	}
	void GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg);

	// Utils
	void Comp_ITypeMemLR(MIPSOpcode op, bool load);

	JitOptions jo;
	JitState js;

	IRBlockCache blocks_;

	IRRegCache gpr;
	// Arm64RegCacheFPU fpr;

	MIPSState *mips_;

	int dontLogBlocks;
	int logBlocks;

	IRWriter ir;

	// where to write branch-likely trampolines
	u32 blTrampolines_;
	int blTrampolineCount_;

public:
	// Code pointers
	const u8 *enterDispatcher;

	const u8 *outerLoop;
	const u8 *outerLoopPCInSCRATCH1;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherPCInSCRATCH1;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *breakpointBailout;

	const u8 *saveStaticRegisters;
	const u8 *loadStaticRegisters;

	const u8 *restoreRoundingMode;
	const u8 *applyRoundingMode;
	const u8 *updateRoundingMode;

	// Indexed by FPCR FZ:RN bits for convenience.  Uses SCRATCH2.
	const u8 *convertS0ToSCRATCH1[8];
};

}	// namespace MIPSComp

