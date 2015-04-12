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
#include "Common/ArmCommon.h"
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/ARM64/Arm64Asm.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/ARM64/Arm64RegCacheFPU.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

namespace MIPSComp
{

class Arm64Jit : public Arm64Gen::ARM64CodeBlock
{
public:
	Arm64Jit(MIPSState *mips);
	virtual ~Arm64Jit();

	void DoState(PointerWrap &p);
	static void DoDummyState(PointerWrap &p);

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op);

	void RunLoopUntil(u64 globalticks);

	void Compile(u32 em_address);	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	bool DescribeCodePtr(const u8 *ptr, std::string &name);

	void Comp_RunBlock(MIPSOpcode op);
	void Comp_ReplacementFunc(MIPSOpcode op);

	// Ops
	void Comp_ITypeMem(MIPSOpcode op);
	void Comp_Cache(MIPSOpcode op);

	void Comp_RelBranch(MIPSOpcode op);
	void Comp_RelBranchRI(MIPSOpcode op);
	void Comp_FPUBranch(MIPSOpcode op);
	void Comp_FPULS(MIPSOpcode op);
	void Comp_FPUComp(MIPSOpcode op);
	void Comp_Jump(MIPSOpcode op);
	void Comp_JumpReg(MIPSOpcode op);
	void Comp_Syscall(MIPSOpcode op);
	void Comp_Break(MIPSOpcode op);

	void Comp_IType(MIPSOpcode op);
	void Comp_RType2(MIPSOpcode op);
	void Comp_RType3(MIPSOpcode op);
	void Comp_ShiftType(MIPSOpcode op);
	void Comp_Allegrex(MIPSOpcode op);
	void Comp_Allegrex2(MIPSOpcode op);
	void Comp_VBranch(MIPSOpcode op);
	void Comp_MulDivType(MIPSOpcode op);
	void Comp_Special3(MIPSOpcode op);

	void Comp_FPU3op(MIPSOpcode op);
	void Comp_FPU2op(MIPSOpcode op);
	void Comp_mxc1(MIPSOpcode op);

	void Comp_DoNothing(MIPSOpcode op);

	void Comp_SV(MIPSOpcode op);
	void Comp_SVQ(MIPSOpcode op);
	void Comp_VPFX(MIPSOpcode op);
	void Comp_VVectorInit(MIPSOpcode op);
	void Comp_VMatrixInit(MIPSOpcode op);
	void Comp_VDot(MIPSOpcode op);
	void Comp_VecDo3(MIPSOpcode op);
	void Comp_VV2Op(MIPSOpcode op);
	void Comp_Mftv(MIPSOpcode op);
	void Comp_Vmfvc(MIPSOpcode op);
	void Comp_Vmtvc(MIPSOpcode op);
	void Comp_Vmmov(MIPSOpcode op);
	void Comp_VScl(MIPSOpcode op);
	void Comp_Vmmul(MIPSOpcode op);
	void Comp_Vmscl(MIPSOpcode op);
	void Comp_Vtfm(MIPSOpcode op);
	void Comp_VHdp(MIPSOpcode op);
	void Comp_VCrs(MIPSOpcode op);
	void Comp_VDet(MIPSOpcode op);
	void Comp_Vi2x(MIPSOpcode op);
	void Comp_Vx2i(MIPSOpcode op);
	void Comp_Vf2i(MIPSOpcode op);
	void Comp_Vi2f(MIPSOpcode op);
	void Comp_Vh2f(MIPSOpcode op);
	void Comp_Vcst(MIPSOpcode op);
	void Comp_Vhoriz(MIPSOpcode op);
	void Comp_VRot(MIPSOpcode op);
	void Comp_VIdt(MIPSOpcode op);
	void Comp_Vcmp(MIPSOpcode op);
	void Comp_Vcmov(MIPSOpcode op);
	void Comp_Viim(MIPSOpcode op);
	void Comp_Vfim(MIPSOpcode op);
	void Comp_VCrossQuat(MIPSOpcode op);
	void Comp_Vsgn(MIPSOpcode op);
	void Comp_Vocp(MIPSOpcode op);
	void Comp_ColorConv(MIPSOpcode op);
	void Comp_Vbfy(MIPSOpcode op);

	// Non-NEON: VPFX

	// NEON implementations of the VFPU ops.
	void CompNEON_SV(MIPSOpcode op);
	void CompNEON_SVQ(MIPSOpcode op);
	void CompNEON_VVectorInit(MIPSOpcode op);
	void CompNEON_VMatrixInit(MIPSOpcode op);
	void CompNEON_VDot(MIPSOpcode op);
	void CompNEON_VecDo3(MIPSOpcode op);
	void CompNEON_VV2Op(MIPSOpcode op);
	void CompNEON_Mftv(MIPSOpcode op);
	void CompNEON_Vmfvc(MIPSOpcode op);
	void CompNEON_Vmtvc(MIPSOpcode op);
	void CompNEON_Vmmov(MIPSOpcode op);
	void CompNEON_VScl(MIPSOpcode op);
	void CompNEON_Vmmul(MIPSOpcode op);
	void CompNEON_Vmscl(MIPSOpcode op);
	void CompNEON_Vtfm(MIPSOpcode op);
	void CompNEON_VHdp(MIPSOpcode op);
	void CompNEON_VCrs(MIPSOpcode op);
	void CompNEON_VDet(MIPSOpcode op);
	void CompNEON_Vi2x(MIPSOpcode op);
	void CompNEON_Vx2i(MIPSOpcode op);
	void CompNEON_Vf2i(MIPSOpcode op);
	void CompNEON_Vi2f(MIPSOpcode op);
	void CompNEON_Vh2f(MIPSOpcode op);
	void CompNEON_Vcst(MIPSOpcode op);
	void CompNEON_Vhoriz(MIPSOpcode op);
	void CompNEON_VRot(MIPSOpcode op);
	void CompNEON_VIdt(MIPSOpcode op);
	void CompNEON_Vcmp(MIPSOpcode op);
	void CompNEON_Vcmov(MIPSOpcode op);
	void CompNEON_Viim(MIPSOpcode op);
	void CompNEON_Vfim(MIPSOpcode op);
	void CompNEON_VCrossQuat(MIPSOpcode op);
	void CompNEON_Vsgn(MIPSOpcode op);
	void CompNEON_Vocp(MIPSOpcode op);
	void CompNEON_ColorConv(MIPSOpcode op);
	void CompNEON_Vbfy(MIPSOpcode op);

	int Replace_fabsf();

	JitBlockCache *GetBlockCache() { return &blocks; }

	void ClearCache();
	void InvalidateCache();
	void InvalidateCacheAt(u32 em_address, int length = 4);

	void EatPrefix() { js.EatPrefix(); }

private:
	void GenerateFixedCode();
	void FlushAll();
	void FlushPrefixV();

	u32 GetCompilerPC();
	void CompileDelaySlot(int flags);
	void EatInstruction(MIPSOpcode op);
	void AddContinuedBlock(u32 dest);
	MIPSOpcode GetOffsetInstruction(int offset);

	void WriteDownCount(int offset = 0);
	void WriteDownCountR(Arm64Gen::ARM64Reg reg);
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode();
	void MovFromPC(Arm64Gen::ARM64Reg r);
	void MovToPC(Arm64Gen::ARM64Reg r);

	bool ReplaceJalTo(u32 dest);

	void SaveDowncount();
	void RestoreDowncount();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(Arm64Gen::ARM64Reg Reg);
	void WriteSyscallExit();

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, CCFlags cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, CCFlags cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, CCFlags cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARM64XEmitter::*arith)(Arm64Gen::ARM64Reg dst, Arm64Gen::ARM64Reg src, Arm64Gen::ARM64Reg src2), bool (ARM64XEmitter::*tryArithI2R)(Arm64Gen::ARM64Reg dst, Arm64Gen::ARM64Reg src, u32 val), u32 (*eval)(u32 a, u32 b));
	void CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARM64XEmitter::*arithOp2)(Arm64Gen::ARM64Reg dst, Arm64Gen::ARM64Reg rm, Arm64Gen::ARM64Reg rn), bool (ARM64XEmitter::*tryArithI2R)(Arm64Gen::ARM64Reg dst, Arm64Gen::ARM64Reg rm, u32 val), u32 (*eval)(u32 a, u32 b), bool symmetric = false);
	void CompShiftImm(MIPSOpcode op, Arm64Gen::ShiftType shiftType, int sa);
	void CompShiftVar(MIPSOpcode op, Arm64Gen::ShiftType shiftType);
	void CompVrotShuffle(u8 *dregs, int imm, VectorSize sz, bool negSin);

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
	void SetScratch1ToEffectiveAddress(MIPSGPReg rs, s16 offset);
	void SetCCAndSCRATCH1ForSafeAddress(MIPSGPReg rs, s16 offset, Arm64Gen::ARM64Reg tempReg, bool reverse = false);
	void Comp_ITypeMemLR(MIPSOpcode op, bool load);

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

	Arm64RegCache gpr;
	Arm64RegCacheFPU fpr;

	Arm64Gen::ARM64FloatEmitter fp;
	
	MIPSState *mips_;

	int dontLogBlocks;
	int logBlocks;

public:
	// Code pointers
	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *outerLoopPCInSCRATCH1;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherPCInSCRATCH1;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *breakpointBailout;
};

}	// namespace MIPSComp

