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

#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/ARM/ArmAsm.h"

#if defined(MAEMO)
#include "stddef.h"
#endif

namespace MIPSComp
{

struct ArmJitOptions
{
	ArmJitOptions();

	bool useNEONVFPU;
	bool enableBlocklink;
	bool downcountInRegister;
	bool useBackJump;
	bool useForwardJump;
	bool cachePointers;
	bool immBranches;
	bool continueBranches;
	bool continueJumps;
	int continueMaxInstructions;
};

class Jit : public ArmGen::ARMXCodeBlock
{
public:
	Jit(MIPSState *mips);

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

	void CompileDelaySlot(int flags);
	void EatInstruction(MIPSOpcode op);
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

	void WriteDownCount(int offset = 0);
	void WriteDownCountR(ARMReg reg);
	void MovFromPC(ARMReg r);
	void MovToPC(ARMReg r);

	bool ReplaceJalTo(u32 dest);

	void SaveDowncount();
	void RestoreDowncount();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(ARMReg Reg);
	void WriteSyscallExit();

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, ArmGen::CCFlags cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, ArmGen::CCFlags cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, ArmGen::CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, ArmGen::CCFlags cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg src, Operand2 op2), bool (ARMXEmitter::*tryArithI2R)(ARMReg dst, ARMReg src, u32 val), u32 (*eval)(u32 a, u32 b));
	void CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARMXEmitter::*arithOp2)(ARMReg dst, ARMReg rm, Operand2 rn), bool (ARMXEmitter::*tryArithI2R)(ARMReg dst, ARMReg rm, u32 val), u32 (*eval)(u32 a, u32 b), bool symmetric = false);

	void CompShiftImm(MIPSOpcode op, ArmGen::ShiftType shiftType, int sa);
	void CompShiftVar(MIPSOpcode op, ArmGen::ShiftType shiftType);

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
	void SetR0ToEffectiveAddress(MIPSGPReg rs, s16 offset);
	void SetCCAndR0ForSafeAddress(MIPSGPReg rs, s16 offset, ARMReg tempReg, bool reverse = false);
	void Comp_ITypeMemLR(MIPSOpcode op, bool load);

	JitBlockCache blocks;
	ArmJitOptions jo;
	JitState js;

	ArmRegCache gpr;
	ArmRegCacheFPU fpr;

	MIPSState *mips_;

	int dontLogBlocks;
	int logBlocks;

public:
	// Code pointers
	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *outerLoopPCInR0;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherPCInR0;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *breakpointBailout;
};

typedef void (Jit::*MIPSCompileFunc)(MIPSOpcode opcode);
typedef int (Jit::*MIPSReplaceFunc)();

}	// namespace MIPSComp

