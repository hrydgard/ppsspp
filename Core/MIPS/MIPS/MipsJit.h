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

#include "Common/MipsEmitter.h"
using namespace MIPSGen;

#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "../MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

namespace MIPSComp
{

class MipsJit : public MIPSGen::MIPSCodeBlock, public JitInterface, public MIPSFrontendInterface
{
public:
	MipsJit(MIPSState *mipsState);

	void DoState(PointerWrap &p) override;

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op) override;

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	const u8 *GetCrashHandler() const override { return nullptr; }
	bool CodeInRange(const u8 *ptr) const override { return IsInSpace(ptr); }
	bool DescribeCodePtr(const u8 *ptr, std::string &name);

	void CompileDelaySlot(int flags);
	void EatInstruction(MIPSOpcode op);
	void AddContinuedBlock(u32 dest);

	void Comp_RunBlock(MIPSOpcode op) override;
	void Comp_ReplacementFunc(MIPSOpcode op) override;

	// Ops
	void Comp_ITypeMem(MIPSOpcode op) override {}
	void Comp_StoreSync(MIPSOpcode op) override {}
	void Comp_Cache(MIPSOpcode op) override {}

	void Comp_RelBranch(MIPSOpcode op) override {}
	void Comp_RelBranchRI(MIPSOpcode op) override {}
	void Comp_FPUBranch(MIPSOpcode op) override {}
	void Comp_FPULS(MIPSOpcode op) override {}
	void Comp_FPUComp(MIPSOpcode op) override {}
	void Comp_Jump(MIPSOpcode op) override {}
	void Comp_JumpReg(MIPSOpcode op) override {}
	void Comp_Syscall(MIPSOpcode op) override {}
	void Comp_Break(MIPSOpcode op) override {}

	void Comp_IType(MIPSOpcode op) override {}
	void Comp_RType2(MIPSOpcode op) override {}
	void Comp_RType3(MIPSOpcode op) override {}
	void Comp_ShiftType(MIPSOpcode op) override {}
	void Comp_Allegrex(MIPSOpcode op) override {}
	void Comp_Allegrex2(MIPSOpcode op) override {}
	void Comp_VBranch(MIPSOpcode op) override {}
	void Comp_MulDivType(MIPSOpcode op) override {}
	void Comp_Special3(MIPSOpcode op) override {}

	void Comp_FPU3op(MIPSOpcode op) override {}
	void Comp_FPU2op(MIPSOpcode op) override {}
	void Comp_mxc1(MIPSOpcode op) override {}

	void Comp_DoNothing(MIPSOpcode op) override {}

	void Comp_SV(MIPSOpcode op) override {}
	void Comp_SVQ(MIPSOpcode op) override {}
	void Comp_VPFX(MIPSOpcode op) override {}
	void Comp_VVectorInit(MIPSOpcode op) override {}
	void Comp_VMatrixInit(MIPSOpcode op) override {}
	void Comp_VDot(MIPSOpcode op) override {}
	void Comp_VecDo3(MIPSOpcode op) override {}
	void Comp_VV2Op(MIPSOpcode op) override {}
	void Comp_Mftv(MIPSOpcode op) override {}
	void Comp_Vmfvc(MIPSOpcode op) override {}
	void Comp_Vmtvc(MIPSOpcode op) override {}
	void Comp_Vmmov(MIPSOpcode op) override {}
	void Comp_VScl(MIPSOpcode op) override {}
	void Comp_Vmmul(MIPSOpcode op) override {}
	void Comp_Vmscl(MIPSOpcode op) override {}
	void Comp_Vtfm(MIPSOpcode op) override {}
	void Comp_VHdp(MIPSOpcode op) override {}
	void Comp_VCrs(MIPSOpcode op) override {}
	void Comp_VDet(MIPSOpcode op) override {}
	void Comp_Vi2x(MIPSOpcode op) override {}
	void Comp_Vx2i(MIPSOpcode op) override {}
	void Comp_Vf2i(MIPSOpcode op) override {}
	void Comp_Vi2f(MIPSOpcode op) override {}
	void Comp_Vh2f(MIPSOpcode op) override {}
	void Comp_Vcst(MIPSOpcode op) override {}
	void Comp_Vhoriz(MIPSOpcode op) override {}
	void Comp_VRot(MIPSOpcode op) override {}
	void Comp_VIdt(MIPSOpcode op) override {}
	void Comp_Vcmp(MIPSOpcode op) override {}
	void Comp_Vcmov(MIPSOpcode op) override {}
	void Comp_Viim(MIPSOpcode op) override {}
	void Comp_Vfim(MIPSOpcode op) override {}
	void Comp_VCrossQuat(MIPSOpcode op) override {}
	void Comp_Vsgn(MIPSOpcode op) override {}
	void Comp_Vocp(MIPSOpcode op) override {}
	void Comp_ColorConv(MIPSOpcode op) override {}
	int Replace_fabsf() override { return 0; }

	void Comp_Vbfy(MIPSOpcode op) {}

	JitBlockCache *GetBlockCache() override { return &blocks; }
	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override { return &blocks; }

	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

	std::vector<u32> SaveAndClearEmuHackOps() override { return blocks.SaveAndClearEmuHackOps(); }
	void RestoreSavedEmuHackOps(std::vector<u32> saved) override { blocks.RestoreSavedEmuHackOps(saved); }

	void ClearCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override;
	void UpdateFCR31() override;

	const u8 *GetDispatcher() const override {
		return dispatcher;
	}

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

	void EatPrefix() override { js.EatPrefix(); }

private:
	void GenerateFixedCode();
	void FlushAll();
	void FlushPrefixV();

	void WriteDownCount(int offset = 0);
	void WriteDownCountR(MIPSReg reg);
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode();
	void MovFromPC(MIPSReg r);
	void MovToPC(MIPSReg r);

	bool ReplaceJalTo(u32 dest);

	void SaveDowncount();
	void RestoreDowncount();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(MIPSReg Reg);
	void WriteSyscallExit();

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

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
};

}	// namespace MIPSComp

