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
#include "Common/ArmEmitter.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

namespace MIPSComp {

class ArmJit : public ArmGen::ARMXCodeBlock, public JitInterface, public MIPSFrontendInterface {
public:
	ArmJit(MIPSState *mips);
	virtual ~ArmJit();

	void DoState(PointerWrap &p) override;

	const JitOptions &GetJitOptions() { return jo; }

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op) override;

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC

	const u8 *GetCrashHandler() const override { return crashHandler; }
	bool CodeInRange(const u8 *ptr) const override { return IsInSpace(ptr); }
	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

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

	int Replace_fabsf() override;

	JitBlockCache *GetBlockCache() override { return &blocks; }
	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override { return &blocks; }

	std::vector<u32> SaveAndClearEmuHackOps() override { return blocks.SaveAndClearEmuHackOps(); }
	void RestoreSavedEmuHackOps(std::vector<u32> saved) override { blocks.RestoreSavedEmuHackOps(saved); }

	void ClearCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override;
	void UpdateFCR31() override;

	void EatPrefix() override { js.EatPrefix(); }

	const u8 *GetDispatcher() const override {
		return dispatcher;
	}

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

private:
	const u8 *DoJit(u32 em_address, JitBlock *b);

	void GenerateFixedCode();
	void FlushAll();
	void FlushPrefixV();

	u32 GetCompilerPC();
	void CompileDelaySlot(int flags);
	void EatInstruction(MIPSOpcode op);
	void AddContinuedBlock(u32 dest);
	MIPSOpcode GetOffsetInstruction(int offset);

	void WriteDownCount(int offset = 0);
	void WriteDownCountR(ArmGen::ARMReg reg);
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode(u32 fcr31 = -1);
	void MovFromPC(ArmGen::ARMReg r);
	void MovToPC(ArmGen::ARMReg r);

	bool ReplaceJalTo(u32 dest);

	void SaveDowncount();
	void RestoreDowncount();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(ArmGen::ARMReg Reg);
	void WriteSyscallExit();
	bool CheckJitBreakpoint(u32 addr, int downcountOffset);
	bool CheckMemoryBreakpoint(int instructionOffset = 0);

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, CCFlags cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, CCFlags cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, CCFlags cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARMXEmitter::*arith)(ArmGen::ARMReg dst, ArmGen::ARMReg src, ArmGen::Operand2 op2), bool (ARMXEmitter::*tryArithI2R)(ArmGen::ARMReg dst, ArmGen::ARMReg src, u32 val), u32 (*eval)(u32 a, u32 b));
	void CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARMXEmitter::*arithOp2)(ArmGen::ARMReg dst, ArmGen::ARMReg rm, ArmGen::Operand2 rn), bool (ARMXEmitter::*tryArithI2R)(ArmGen::ARMReg dst, ArmGen::ARMReg rm, u32 val), u32 (*eval)(u32 a, u32 b), bool symmetric = false);

	void CompShiftImm(MIPSOpcode op, ArmGen::ShiftType shiftType, int sa);
	void CompShiftVar(MIPSOpcode op, ArmGen::ShiftType shiftType);
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


	// For NEON mappings, it will be easier to deal directly in ARM registers.

	ArmGen::ARMReg NEONMapPrefixST(int vfpuReg, VectorSize sz, u32 prefix, int mapFlags);
	ArmGen::ARMReg NEONMapPrefixS(int vfpuReg, VectorSize sz, int mapFlags) {
		return NEONMapPrefixST(vfpuReg, sz, js.prefixS, mapFlags);
	}
	ArmGen::ARMReg NEONMapPrefixT(int vfpuReg, VectorSize sz, int mapFlags) {
		return NEONMapPrefixST(vfpuReg, sz, js.prefixT, mapFlags);
	}

	struct DestARMReg {
		ArmGen::ARMReg rd;
		ArmGen::ARMReg backingRd;
		VectorSize sz;

		operator ArmGen::ARMReg() const { return rd; }
	};

	struct MappedRegs {
		ArmGen::ARMReg vs;
		ArmGen::ARMReg vt;
		DestARMReg vd;
		bool overlap;
	};

	MappedRegs NEONMapDirtyInIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize, VectorSize tsize, bool applyPrefixes = true);
	MappedRegs NEONMapInIn(MIPSOpcode op, VectorSize ssize, VectorSize tsize, bool applyPrefixes = true);
	MappedRegs NEONMapDirtyIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize, bool applyPrefixes = true);

	DestARMReg NEONMapPrefixD(int vfpuReg, VectorSize sz, int mapFlags);
	void NEONApplyPrefixD(DestARMReg dest);

	// NEON utils
	void NEONMaskToSize(ArmGen::ARMReg vs, VectorSize sz);
	void NEONTranspose4x4(ArmGen::ARMReg cols[4]);

	// Utils
	void SetR0ToEffectiveAddress(MIPSGPReg rs, s16 offset);
	void SetCCAndR0ForSafeAddress(MIPSGPReg rs, s16 offset, ArmGen::ARMReg tempReg, bool reverse = false);
	void Comp_ITypeMemLR(MIPSOpcode op, bool load);

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

	ArmRegCache gpr;
	ArmRegCacheFPU fpr;

	MIPSState *mips_;

	int dontLogBlocks;
	int logBlocks;

public:
	// Code pointers
	const u8 *enterDispatcher;

	const u8 *outerLoop;
	const u8 *outerLoopPCInR0;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherPCInR0;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *restoreRoundingMode;
	const u8 *applyRoundingMode;

	const u8 *crashHandler;
};

}	// namespace MIPSComp

