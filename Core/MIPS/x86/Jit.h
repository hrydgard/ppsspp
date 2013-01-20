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

#include "../../../Globals.h"
#include "../../../Common/Thunk.h"
#include "Asm.h"

#if defined(ARM)
#error DO NOT BUILD X86 JIT ON ARM
#endif

#include "x64Emitter.h"
#include "JitCache.h"
#include "RegCache.h"

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
	u32 compilerPC;
	u32 blockStart;
	bool cancel;
	bool inDelaySlot;
	int downcountAmount;
	bool compiling;	// TODO: get rid of this in favor of using analysis results to determine end of block
	JitBlock *curBlock;
};

class Jit : public Gen::XCodeBlock
{
public:
	Jit(MIPSState *mips);

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(u32 op);

	void RunLoopUntil(u64 globalticks);

	void Compile(u32 em_address);	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	void CompileDelaySlot(u32 addr, bool saveFlags = false);
	void CompileAt(u32 addr);
	void Comp_RunBlock(u32 op);

	// Ops
	void Comp_ITypeMem(u32 op);

	void Comp_RelBranch(u32 op);
	void Comp_RelBranchRI(u32 op);
	void Comp_FPUBranch(u32 op);
	void Comp_FPULS(u32 op);
	void Comp_Jump(u32 op);
	void Comp_JumpReg(u32 op);
	void Comp_Syscall(u32 op);

	void Comp_IType(u32 op);
	void Comp_RType3(u32 op);
	void Comp_ShiftType(u32 op);
	void Comp_Allegrex(u32 op);
	void Comp_VBranch(u32 op);

	void Comp_FPU3op(u32 op);
	void Comp_FPU2op(u32 op);
	void Comp_mxc1(u32 op);

	JitBlockCache *GetBlockCache() { return &blocks; }
	AsmRoutineManager &Asm() { return asm_; }

	void ClearCache();
	void ClearCacheAt(u32 em_address);
private:
	void FlushAll();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInEAX();
//	void WriteRfiExitDestInEAX();
	void WriteSyscallExit();
	bool CheckJitBreakpoint(u32 addr);

	// Utility compilation functions
	void BranchFPFlag(u32 op, Gen::CCFlags cc, bool likely);
	void BranchVFPUFlag(u32 op, Gen::CCFlags cc, bool likely);
	void BranchRSZeroComp(u32 op, Gen::CCFlags cc, bool likely);
	void BranchRSRTComp(u32 op, Gen::CCFlags cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &));
	void CompTriArith(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &));
	void CompShiftImm(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg));
	void CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg));
	void CompITypeMemRead(u32 op, u32 bits, void (XEmitter::*mov)(int, int, X64Reg, OpArg), void *safeFunc);
	void CompITypeMemWrite(u32 op, u32 bits, void *safeFunc);

	void CompFPTriArith(u32 op, void (XEmitter::*arith)(X64Reg reg, OpArg), bool orderMatters);

	JitBlockCache blocks;
	JitOptions jo;
	JitState js;

	GPRRegCache gpr;
	FPURegCache fpr;

	AsmRoutineManager asm_;
	ThunkManager thunks;

	MIPSState *mips_;
};

typedef void (Jit::*MIPSCompileFunc)(u32 opcode);

}	// namespace MIPSComp

