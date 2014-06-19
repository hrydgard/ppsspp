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

#include "Common/CommonTypes.h"

#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/PPC/PpcRegCache.h"
#include "Core/MIPS/PPC/PpcRegCacheFpu.h"

#include "Core/MIPS/MIPS.h"

#include <ppcEmitter.h>

namespace MIPSComp
{

	struct PpcJitOptions
	{
		PpcJitOptions()
		{
			enableBlocklink = true;
			downcountInRegister = true;
		}

		bool enableBlocklink;
		bool downcountInRegister;
	};

	struct PpcJitState
	{
		enum PrefixState
		{
			PREFIX_UNKNOWN     = 0x00,
			PREFIX_KNOWN       = 0x01,
			PREFIX_DIRTY       = 0x10,
			PREFIX_KNOWN_DIRTY = 0x11,
		};

		PpcJitState()
			: prefixSFlag(PREFIX_UNKNOWN),
			prefixTFlag(PREFIX_UNKNOWN),
			prefixDFlag(PREFIX_UNKNOWN) {}

		u32 compilerPC;
		u32 blockStart;
		bool cancel;
		bool inDelaySlot;
		int downcountAmount;
		bool compiling;	// TODO: get rid of this in favor of using analysis results to determine end of block
		JitBlock *curBlock;

		// VFPU prefix magic
		bool startDefaultPrefix;
		u32 prefixS;
		u32 prefixT;
		u32 prefixD;
		PrefixState prefixSFlag;
		PrefixState prefixTFlag;
		PrefixState prefixDFlag;
		void PrefixStart() {
			if (startDefaultPrefix) {
				EatPrefix();
			} else {
				PrefixUnknown();
			}
		}
		void PrefixUnknown() {
			prefixSFlag = PREFIX_UNKNOWN;
			prefixTFlag = PREFIX_UNKNOWN;
			prefixDFlag = PREFIX_UNKNOWN;
		}
		bool MayHavePrefix() const {
			if (HasUnknownPrefix()) {
				return true;
			} else if (prefixS != 0xE4 || prefixT != 0xE4 || prefixD != 0) {
				return true;
			} else if (VfpuWriteMask() != 0) {
				return true;
			}

			return false;
		}
		bool HasUnknownPrefix() const {
			if (!(prefixSFlag & PREFIX_KNOWN) || !(prefixTFlag & PREFIX_KNOWN) || !(prefixDFlag & PREFIX_KNOWN)) {
				return true;
			}
			return false;
		}
		bool HasNoPrefix() const {
			return (prefixDFlag & PREFIX_KNOWN) && (prefixSFlag & PREFIX_KNOWN) && (prefixTFlag & PREFIX_KNOWN) && (prefixS == 0xE4 && prefixT == 0xE4 && prefixD == 0);
		}

		void EatPrefix() {
			if ((prefixSFlag & PREFIX_KNOWN) == 0 || prefixS != 0xE4) {
				prefixSFlag = PREFIX_KNOWN_DIRTY;
				prefixS = 0xE4;
			}
			if ((prefixTFlag & PREFIX_KNOWN) == 0 || prefixT != 0xE4) {
				prefixTFlag = PREFIX_KNOWN_DIRTY;
				prefixT = 0xE4;
			}
			if ((prefixDFlag & PREFIX_KNOWN) == 0 || prefixD != 0x0 || VfpuWriteMask() != 0) {
				prefixDFlag = PREFIX_KNOWN_DIRTY;
				prefixD = 0x0;
			}
		}
		u8 VfpuWriteMask() const {
			_assert_(prefixDFlag & PREFIX_KNOWN);
			return (prefixD >> 8) & 0xF;
		}
		bool VfpuWriteMask(int i) const {
			_assert_(prefixDFlag & PREFIX_KNOWN);
			return (prefixD >> (8 + i)) & 1;
		}
	};


	enum CompileDelaySlotFlags
	{
		// Easy, nothing extra.
		DELAYSLOT_NICE = 0,
		// Flush registers after delay slot.
		DELAYSLOT_FLUSH = 1,
		// Preserve flags.
		DELAYSLOT_SAFE = 2,
		// Flush registers after and preserve flags.
		DELAYSLOT_SAFE_FLUSH = DELAYSLOT_FLUSH | DELAYSLOT_SAFE,
	};

	class Jit: public PpcGen::PPCXCodeBlock
	{
	protected:	
		JitBlockCache blocks;
	public:
		Jit(MIPSState *mips);
		void DoState(PointerWrap &p) {
			auto s = p.Section("Jit", 1);
			if (!s)
				return;

			// Do nothing
		}
		static void DoDummyState(PointerWrap &p) {
			auto s = p.Section("Jit", 1);
			if (!s)
				return;

			// Do nothing
		}

		// Compiled ops should ignore delay slots
		// the compiler will take care of them by itself
		// OR NOT
		void Comp_Generic(MIPSOpcode op);

		void EatInstruction(MIPSOpcode op);
		void Comp_RunBlock(MIPSOpcode op);
		void Comp_ReplacementFunc(MIPSOpcode op);

		// TODO: Eat VFPU prefixes here.
		void EatPrefix() { js.EatPrefix(); }

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

		int Replace_fabsf();

		// Utility compilation functions
		void BranchFPFlag(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely);
		void BranchVFPUFlag(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely);
		void BranchRSZeroComp(MIPSOpcode op, PpcGen::FixupBranchType cc, bool andLink, bool likely);
		void BranchRSRTComp(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely);

		void SetRegToEffectiveAddress(PpcGen::PPCReg r, int rs, s16 offset);
		
		// Utilities to reduce duplicated code
		void CompImmLogic(int rs, int rt, u32 uimm, void (PPCXEmitter::*arith)(PPCReg Rd, PPCReg Ra, unsigned short imm), u32 (*eval)(u32 a, u32 b));
		void CompType3(int rd, int rs, int rt, void (PPCXEmitter::*arithOp2)(PPCReg Rd, PPCReg Ra, PPCReg Rb), u32 (*eval)(u32 a, u32 b), bool isSub = false);
		void FPUComp(int fs, int ft, PpcGen::FixupBranchType cond, bool unorderer = false, int bf = 0);
		
		void ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz);
		void ApplyPrefixD(const u8 *vregs, VectorSize sz);
		void GetVectorRegsPrefixS(u8 *regs, VectorSize sz, int vectorReg) {
			_assert_(js.prefixSFlag & PpcJitState::PREFIX_KNOWN);
			GetVectorRegs(regs, sz, vectorReg);
			ApplyPrefixST(regs, js.prefixS, sz);
		}
		void GetVectorRegsPrefixT(u8 *regs, VectorSize sz, int vectorReg) {
			_assert_(js.prefixTFlag & PpcJitState::PREFIX_KNOWN);
			GetVectorRegs(regs, sz, vectorReg);
			ApplyPrefixST(regs, js.prefixT, sz);
		}
		void GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg);
		
		// flush regs
		void FlushAll();
		void FlushPrefixV();

		void WriteDownCount(int offset = 0);
		void MovFromPC(PpcGen::PPCReg r);
		void MovToPC(PpcGen::PPCReg r);

		void SaveDowncount(PpcGen::PPCReg r);
		void RestoreDowncount(PpcGen::PPCReg r);

		void WriteExit(u32 destination, int exit_num);
		void WriteExitDestInR(PPCReg Reg);
		void WriteSyscallExit();

		void ClearCache();
		void InvalidateCache();
		void InvalidateCacheAt(u32 em_address, int length = 4);

		void RunLoopUntil(u64 globalticks);
		void GenerateFixedCode();

		void DumpJit();

		void CompileDelaySlot(int flags);
		void Compile(u32 em_address);	// Compiles a block at current MIPS PC
		const u8 *DoJit(u32 em_address, JitBlock *b);

		bool DescribeCodePtr(const u8 *ptr, std::string &name);

		PpcJitOptions jo;
		PpcJitState js;

		PpcRegCache gpr;
		PpcRegCacheFPU fpr;

		MIPSState *mips_;

		JitBlockCache *GetBlockCache() { return &blocks; }

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

