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

#include "Globals.h"
#include "Common/Thunk.h"
#include "Asm.h"

#if defined(ARM)
#error DO NOT BUILD X86 JIT ON ARM
#endif

#include "Common/x64Emitter.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "RegCache.h"
#include "RegCacheFPU.h"

namespace MIPSComp
{

// This is called when Jit hits a breakpoint.
void JitBreakpoint();

struct JitOptions
{
	JitOptions()
	{
		enableBlocklink = true;
	}

	bool enableBlocklink;
};

struct JitState
{
	enum PrefixState
	{
		PREFIX_UNKNOWN = 0x00,
		PREFIX_KNOWN = 0x01,
		PREFIX_DIRTY = 0x10,
		PREFIX_KNOWN_DIRTY = 0x11,
	};

	enum AfterOp
	{
		AFTER_NONE = 0x00,
		AFTER_CORE_STATE = 0x01,
		AFTER_REWIND_PC_BAD_STATE = 0x02,
	};

	u32 compilerPC;
	u32 blockStart;
	bool cancel;
	bool inDelaySlot;
	// See JitState::AfterOp for values.
	int afterOp;
	int downcountAmount;
	int numInstructions;
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
		_assert_(prefixDFlag & JitState::PREFIX_KNOWN);
		return (prefixD >> 8) & 0xF;
	}
	bool VfpuWriteMask(int i) const {
		_assert_(prefixDFlag & JitState::PREFIX_KNOWN);
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

class Jit : public Gen::XCodeBlock
{
public:
	Jit(MIPSState *mips);
	void DoState(PointerWrap &p);
	static void DoDummyState(PointerWrap &p);

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(u32 op);

	void RunLoopUntil(u64 globalticks);

	void Compile(u32 em_address);	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	void CompileAt(u32 addr);
	void Comp_RunBlock(u32 op);

	// Ops
	void Comp_ITypeMem(u32 op);

	void Comp_RelBranch(u32 op);
	void Comp_RelBranchRI(u32 op);
	void Comp_FPUBranch(u32 op);
	void Comp_FPULS(u32 op);
	void Comp_FPUComp(u32 op);
	void Comp_Jump(u32 op);
	void Comp_JumpReg(u32 op);
	void Comp_Syscall(u32 op);
	void Comp_Break(u32 op);

	void Comp_IType(u32 op);
	void Comp_RType2(u32 op);
	void Comp_RType3(u32 op);
	void Comp_ShiftType(u32 op);
	void Comp_Allegrex(u32 op);
	void Comp_Allegrex2(u32 op);
	void Comp_VBranch(u32 op);
	void Comp_MulDivType(u32 op);
	void Comp_Special3(u32 op);

	void Comp_FPU3op(u32 op);
	void Comp_FPU2op(u32 op);
	void Comp_mxc1(u32 op);

	void Comp_SV(u32 op);
	void Comp_SVQ(u32 op);
	void Comp_VPFX(u32 op);
	void Comp_VVectorInit(u32 op);
	void Comp_VDot(u32 op);
	void Comp_VecDo3(u32 op);
	void Comp_VV2Op(u32 op);
	void Comp_Mftv(u32 op);
	void Comp_Vmtvc(u32 op);
	void Comp_Vmmov(u32 op);
	void Comp_VScl(u32 op);
	void Comp_Vmmul(u32 op);
	void Comp_Vmscl(u32 op);
	void Comp_Vtfm(u32 op);
	void Comp_VHdp(u32 op);
	void Comp_VCrs(u32 op);
	void Comp_VDet(u32 op);
	void Comp_Vi2x(u32 op);
	void Comp_Vx2i(u32 op);
	void Comp_Vf2i(u32 op);
	void Comp_Vi2f(u32 op);
	void Comp_Vcst(u32 op);
	void Comp_Vhoriz(u32 op);

	void Comp_DoNothing(u32 op);

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
	void EatPrefix() { js.EatPrefix(); }

	JitBlockCache *GetBlockCache() { return &blocks; }
	AsmRoutineManager &Asm() { return asm_; }

	void ClearCache();
	void ClearCacheAt(u32 em_address);
private:
	void FlushAll();
	void FlushPrefixV();
	void WriteDowncount(int offset = 0);

	// See CompileDelaySlotFlags for flags.
	void CompileDelaySlot(int flags);
	void EatInstruction(u32 op);

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInEAX();
//	void WriteRfiExitDestInEAX();
	void WriteSyscallExit();
	bool CheckJitBreakpoint(u32 addr, int downcountOffset);

	// Utility compilation functions
	void BranchFPFlag(u32 op, Gen::CCFlags cc, bool likely);
	void BranchVFPUFlag(u32 op, Gen::CCFlags cc, bool likely);
	void BranchRSZeroComp(u32 op, Gen::CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(u32 op, Gen::CCFlags cc, bool likely);
	void BranchLog(u32 op);
	void BranchLogExit(u32 op, u32 dest, bool useEAX);

	// Utilities to reduce duplicated code
	void CompImmLogic(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &));
	void CompTriArith(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &), u32 (*doImm)(const u32, const u32));
	void CompShiftImm(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg), u32 (*doImm)(const u32, const u32));
	void CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg), u32 (*doImm)(const u32, const u32));
	void CompITypeMemRead(u32 op, u32 bits, void (XEmitter::*mov)(int, int, X64Reg, OpArg), void *safeFunc);
	void CompITypeMemWrite(u32 op, u32 bits, void *safeFunc);

	void CompFPTriArith(u32 op, void (XEmitter::*arith)(X64Reg reg, OpArg), bool orderMatters);
	void CompFPComp(int lhs, int rhs, u8 compare, bool allowNaN = false);

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

	GPRRegCache gpr;
	FPURegCache fpr;

	AsmRoutineManager asm_;
	ThunkManager thunks;

	MIPSState *mips_;

	class JitSafeMem
	{
	public:
		JitSafeMem(Jit *jit, int raddr, s32 offset);

		// Emit code necessary for a memory write, returns true if MOV to dest is needed.
		bool PrepareWrite(OpArg &dest, int size);
		// Emit code proceeding a slow write call, returns true if slow write is needed.
		bool PrepareSlowWrite();
		// Emit a slow write from src.
		void DoSlowWrite(void *safeFunc, const OpArg src, int suboffset = 0);

		// Emit code necessary for a memory read, returns true if MOV from src is needed.
		bool PrepareRead(OpArg &src, int size);
		// Emit code for a slow read call, and returns true if result is in EAX.
		bool PrepareSlowRead(void *safeFunc);
		
		// Cleans up final code for the memory access.
		void Finish();

		// Use this before anything else if you're gonna use the below.
		void SetFar();
		// WARNING: Only works for non-GPR.  Do not use for reads into GPR.
		OpArg NextFastAddress(int suboffset);
		// WARNING: Only works for non-GPR.  Do not use for reads into GPR.
		void NextSlowRead(void *safeFunc, int suboffset);

	private:
		enum ReadType
		{
			MEM_READ,
			MEM_WRITE,
		};

		OpArg PrepareMemoryOpArg(ReadType type);
		void PrepareSlowAccess();
		void MemCheckImm(ReadType type);
		void MemCheckAsm(ReadType type);
		bool ImmValid();

		Jit *jit_;
		int raddr_;
		s32 offset_;
		int size_;
		bool needsCheck_;
		bool needsSkip_;
		bool far_;
		u32 iaddr_;
		X64Reg xaddr_;
		FixupBranch tooLow_, tooHigh_, skip_;
		std::vector<FixupBranch> skipChecks_;
		const u8 *safe_;
	};
	friend class JitSafeMem;
};

typedef void (Jit::*MIPSCompileFunc)(u32 opcode);

}	// namespace MIPSComp

