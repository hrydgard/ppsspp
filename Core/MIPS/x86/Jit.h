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


// TODO: Implement https://github.com/dolphin-emu/dolphin/pull/1025/commits/b597ec3e081a289d9ac782586617a876535183d6 .

#pragma once

#include "Common/CommonTypes.h"
#include "Common/Thunk.h"
#include "Common/x64Emitter.h"

#include "Common/x64Emitter.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/JitSafeMem.h"
#include "Core/MIPS/x86/RegCache.h"
#include "Core/MIPS/x86/RegCacheFPU.h"

class PointerWrap;

namespace MIPSComp {

// This is called when Jit hits a breakpoint.  Returns 1 when hit.
u32 JitBreakpoint();

struct RegCacheState {
	GPRRegCacheState gpr;
	FPURegCacheState fpr;
};

class Jit : public Gen::XCodeBlock, public JitInterface, public MIPSFrontendInterface {
public:
	Jit(MIPSState *mips);
	virtual ~Jit();

	const JitOptions &GetJitOptions() { return jo; }

	void DoState(PointerWrap &p) override;

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op) override;

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	const u8 *GetCrashHandler() const override { return crashHandler; }
	bool CodeInRange(const u8 *ptr) const override { return IsInSpace(ptr); }
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

	void Comp_DoNothing(MIPSOpcode op) override;

	int Replace_fabsf() override;

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
	void EatPrefix() override { js.EatPrefix(); }

	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode(u32 fcr31 = -1);

	JitBlockCache *GetBlockCache() override { return &blocks; }
	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override { return &blocks; }

	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

	std::vector<u32> SaveAndClearEmuHackOps() override { return blocks.SaveAndClearEmuHackOps(); }
	void RestoreSavedEmuHackOps(std::vector<u32> saved) override { blocks.RestoreSavedEmuHackOps(saved); }

	void ClearCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override {
		if (blocks.RangeMayHaveEmuHacks(em_address, em_address + length)) {
			blocks.InvalidateICache(em_address, length);
		}
	}
	void UpdateFCR31() override;

	const u8 *GetDispatcher() const override {
		return dispatcher;
	}

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

private:
	void GenerateFixedCode(JitOptions &jo);
	void GetStateAndFlushAll(RegCacheState &state);
	void RestoreState(const RegCacheState& state);
	void FlushAll();
	void FlushPrefixV();
	void WriteDowncount(int offset = 0);
	bool ReplaceJalTo(u32 dest);

	u32 GetCompilerPC();
	// See CompileDelaySlotFlags for flags.
	void CompileDelaySlot(int flags, RegCacheState *state = NULL);
	void CompileDelaySlot(int flags, RegCacheState &state) {
		CompileDelaySlot(flags, &state);
	}
	void EatInstruction(MIPSOpcode op);
	void AddContinuedBlock(u32 dest);
	MIPSOpcode GetOffsetInstruction(int offset);

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInReg(Gen::X64Reg reg);

//	void WriteRfiExitDestInEAX();
	void WriteSyscallExit();
	bool CheckJitBreakpoint(u32 addr, int downcountOffset);

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, Gen::CCFlags cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, Gen::CCFlags cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, Gen::CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, Gen::CCFlags cc, bool likely);
	void BranchLog(MIPSOpcode op);
	void BranchLogExit(MIPSOpcode op, u32 dest, bool useEAX);

	// Utilities to reduce duplicated code
	void CompImmLogic(MIPSOpcode op, void (XEmitter::*arith)(int, const Gen::OpArg &, const Gen::OpArg &));
	void CompTriArith(MIPSOpcode op, void (XEmitter::*arith)(int, const Gen::OpArg &, const Gen::OpArg &), u32 (*doImm)(const u32, const u32), bool invertResult = false);
	void CompShiftImm(MIPSOpcode op, void (XEmitter::*shift)(int, Gen::OpArg, Gen::OpArg), u32 (*doImm)(const u32, const u32));
	void CompShiftVar(MIPSOpcode op, void (XEmitter::*shift)(int, Gen::OpArg, Gen::OpArg), u32 (*doImm)(const u32, const u32));
	void CompITypeMemRead(MIPSOpcode op, u32 bits, void (XEmitter::*mov)(int, int, Gen::X64Reg, Gen::OpArg), const void *safeFunc);
	template <typename T>
	void CompITypeMemRead(MIPSOpcode op, u32 bits, void (XEmitter::*mov)(int, int, Gen::X64Reg, Gen::OpArg), T (*safeFunc)(u32 addr)) {
		CompITypeMemRead(op, bits, mov, (const void *)safeFunc);
	}
	void CompITypeMemWrite(MIPSOpcode op, u32 bits, const void *safeFunc);
	template <typename T>
	void CompITypeMemWrite(MIPSOpcode op, u32 bits, void (*safeFunc)(T val, u32 addr)) {
		CompITypeMemWrite(op, bits, (const void *)safeFunc);
	}
	void CompITypeMemUnpairedLR(MIPSOpcode op, bool isStore);
	void CompITypeMemUnpairedLRInner(MIPSOpcode op, Gen::X64Reg shiftReg);
	void CompBranchExits(Gen::CCFlags cc, u32 targetAddr, u32 notTakenAddr, bool delaySlotIsNice, bool likely, bool andLink);
	void CompBranchExit(bool taken, u32 targetAddr, u32 notTakenAddr, bool delaySlotIsNice, bool likely, bool andLink);
	static Gen::CCFlags FlipCCFlag(Gen::CCFlags flag);
	static Gen::CCFlags SwapCCFlag(Gen::CCFlags flag);

	void CopyFPReg(Gen::X64Reg dst, Gen::OpArg src);
	void CompFPTriArith(MIPSOpcode op, void (XEmitter::*arith)(Gen::X64Reg reg, Gen::OpArg), bool orderMatters);
	void CompFPComp(int lhs, int rhs, u8 compare, bool allowNaN = false);
	void CompVrotShuffle(u8 *dregs, int imm, int n, bool negSin);

	void CallProtectedFunction(const void *func, const Gen::OpArg &arg1);
	void CallProtectedFunction(const void *func, const Gen::OpArg &arg1, const Gen::OpArg &arg2);
	void CallProtectedFunction(const void *func, const u32 arg1, const u32 arg2, const u32 arg3);
	void CallProtectedFunction(const void *func, const Gen::OpArg &arg1, const u32 arg2, const u32 arg3);

	template <typename Tr, typename T1>
	void CallProtectedFunction(Tr (*func)(T1), const Gen::OpArg &arg1) {
		CallProtectedFunction((const void *)func, arg1);
	}

	template <typename Tr, typename T1, typename T2>
	void CallProtectedFunction(Tr (*func)(T1, T2), const Gen::OpArg &arg1, const Gen::OpArg &arg2) {
		CallProtectedFunction((const void *)func, arg1, arg2);
	}

	template <typename Tr, typename T1, typename T2, typename T3>
	void CallProtectedFunction(Tr (*func)(T1, T2, T3), const u32 arg1, const u32 arg2, const u32 arg3) {
		CallProtectedFunction((const void *)func, arg1, arg2, arg3);
	}

	template <typename Tr, typename T1, typename T2, typename T3>
	void CallProtectedFunction(Tr (*func)(T1, T2, T3), const Gen::OpArg &arg1, const u32 arg2, const u32 arg3) {
		CallProtectedFunction((const void *)func, arg1, arg2, arg3);
	}

	bool PredictTakeBranch(u32 targetAddr, bool likely);
	bool CanContinueBranch(u32 targetAddr) {
		if (!jo.continueBranches || js.numInstructions >= jo.continueMaxInstructions) {
			return false;
		}
		// Need at least 2 exits left over.
		if (js.nextExit >= MAX_JIT_BLOCK_EXITS - 2) {
			return false;
		}
		// Sometimes we predict wrong and get into impossible conditions where games have jumps to 0.
		if (!targetAddr) {
			return false;
		}
		return true;
	}
	bool CanContinueJump(u32 targetAddr) {
		if (!jo.continueJumps || js.numInstructions >= jo.continueMaxInstructions) {
			return false;
		}
		if (!targetAddr) {
			return false;
		}
		return true;
	}
	bool CanContinueImmBranch(u32 targetAddr) {
		if (!jo.immBranches || js.numInstructions >= jo.continueMaxInstructions) {
			return false;
		}
		return true;
	}
	void SaveFlags();
	void LoadFlags();

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

	GPRRegCache gpr;
	FPURegCache fpr;

	ThunkManager thunks;
	JitSafeMemFuncs safeMemFuncs;

	MIPSState *mips_;


	const u8 *enterDispatcher;

	const u8 *outerLoop;
	const u8 *dispatcher;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherNoCheck;
	const u8 *dispatcherInEAXNoCheck;

	const u8 *restoreRoundingMode;
	const u8 *applyRoundingMode;

	const u8 *endOfPregeneratedCode;

	const u8 *crashHandler;

	friend class JitSafeMem;
	friend class JitSafeMemFuncs;
};

}	// namespace MIPSComp

