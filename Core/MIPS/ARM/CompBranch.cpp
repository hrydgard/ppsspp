// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.
#include "../../HLE/HLE.h"

#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSAnalyst.h"
#include "../MIPSTables.h"

#include "Jit.h"
#include "RegCache.h"
#include "JitCache.h"
#include "ArmEmitter.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define LOOPOPTIMIZATION 0

using namespace MIPSAnalyst;

namespace MIPSComp
{
  /*
void Jit::BranchRSRTComp(u32 op, Gen::CCFlags cc, bool likely)
{
	int offset = (signed short)(op&0xFFFF)<<2;
	int rt = _RT;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;
		
	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC+4);

	//Compile the delay slot
	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rt && GetOutReg(delaySlotOp) != rs;// IsDelaySlotNice(op, delaySlotOp);
  if (!delaySlotIsNice)
  {
    ERROR_LOG(CPU, "Not nice delay slot in BranchRSRTComp :( %08x", js.compilerPC);
  }
  // The delay slot being nice doesn't really matter though...

  if (rs == 0)
  {
    CMP(32, gpr.R(rt), Imm32(0));
  }
  else
  {
    gpr.BindToRegister(rs, true, false);
    CMP(32, gpr.R(rs), rt == 0 ? Imm32(0) : gpr.R(rt));
  }
  FlushAll();

  js.inDelaySlot = true;
  Gen::FixupBranch ptr;
  if (!likely)
  {
    PUSHF(); // preserve flag around the delay slot!
    CompileAt(js.compilerPC + 4);
    FlushAll();
    POPF(); // restore flag!
    ptr = J_CC(cc, true);
  }
  else
  {
    ptr = J_CC(cc, true);
    CompileAt(js.compilerPC + 4);
    FlushAll();
  }
  js.inDelaySlot = false;

  // Take the branch
  WriteExit(targetAddr, 0);

  SetJumpTarget(ptr);
  // Not taken
  WriteExit(js.compilerPC+8, 1);

  js.compiling = false;
}
*/
  /*
void Jit::BranchRSZeroComp(u32 op, Gen::CCFlags cc, bool likely)
{
	int offset = (signed short)(op&0xFFFF)<<2;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rs;
  if (!delaySlotIsNice)
  {
    ERROR_LOG(CPU, "Not nice delay slot in BranchRSZeroComp :( %08x", js.compilerPC);
  }

  gpr.BindToRegister(rs, true, false);
  CMP(32, gpr.R(rs), Imm32(0));
  FlushAll();

  Gen::FixupBranch ptr;
  js.inDelaySlot = true;
  if (!likely)
  {
    PUSHF(); // preserve flag around the delay slot! Better hope the delay slot instruction doesn't need to fall back to interpreter...
    CompileAt(js.compilerPC + 4);
    FlushAll();
    POPF(); // restore flag!
    ptr = J_CC(cc, true);
  }
  else
  {
    ptr = J_CC(cc, true);
    CompileAt(js.compilerPC + 4);
    FlushAll();
  }
  js.inDelaySlot = false;

  // Take the branch
  WriteExit(targetAddr, 0);

  SetJumpTarget(ptr);
  // Not taken
  WriteExit(js.compilerPC + 8, 1);
  js.compiling = false;
}
*/

void Jit::Comp_RelBranch(u32 op)
{
  /*
	switch (op>>26) 
	{
	case 4: BranchRSRTComp(op, CC_NZ, false); break;//beq
	case 5: BranchRSRTComp(op, CC_Z,  false); break;//bne

	case 6: BranchRSZeroComp(op, CC_G, false); break;//blez
	case 7: BranchRSZeroComp(op, CC_LE, false); break;//bgtz

	case 20: BranchRSRTComp(op, CC_NZ, true); break;//beql
	case 21: BranchRSRTComp(op, CC_Z,  true); break;//bnel

	case 22: BranchRSZeroComp(op, CC_G, true); break;//blezl
	case 23: BranchRSZeroComp(op, CC_LE, true); break;//bgtzl

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  */
  js.compiling = false;
}

void Jit::Comp_RelBranchRI(u32 op)
{
  /*
	switch ((op >> 16) & 0x1F)
	{
	case 0: BranchRSZeroComp(op, CC_GE, false); break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltz
	case 1: BranchRSZeroComp(op, CC_L, false);  break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, CC_GE, true);  break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, CC_L, true);   break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  */
  js.compiling = false;
}


// If likely is set, discard the branch slot if NOT taken.
/*
void Jit::BranchFPFlag(u32 op, Gen::CCFlags cc, bool likely)
{
  int offset = (signed short)(op & 0xFFFF) << 2;
  u32 targetAddr = js.compilerPC + offset + 4;

  u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

  bool delaySlotIsNice = IsDelaySlotNice(op, delaySlotOp);
  if (!delaySlotIsNice)
  {
    ERROR_LOG(CPU, "Not nice delay slot in BranchFPFlag :(");
  }
  FlushAll();

  TEST(32, M((void *)&(mips_->fpcond)), Imm32(1));
  Gen::FixupBranch ptr;
  js.inDelaySlot = true;
  if (!likely)
  {
    PUSHF(); // preserve flag around the delay slot!
    CompileAt(js.compilerPC + 4);
    FlushAll();
    POPF(); // restore flag!
    ptr = J_CC(cc, true);
  }
  else
  {
    ptr = J_CC(cc, true);
    CompileAt(js.compilerPC + 4);
    FlushAll();
  }
  js.inDelaySlot = false;

  // Take the branch
  WriteExit(targetAddr, 0);

  SetJumpTarget(ptr);
  // Not taken
  WriteExit(js.compilerPC + 8, 1);

  js.compiling = false;
}
*/


void Jit::Comp_FPUBranch(u32 op)
{
  /*
	switch((op >> 16) & 0x1f)
	{
	case 0:	BranchFPFlag(op, CC_NZ, false); break;  // bc1f
	case 1: BranchFPFlag(op, CC_Z,  false); break;  // bc1t
	case 2: BranchFPFlag(op, CC_NZ, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, CC_Z,  true);  break;  // bc1tl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		break;
	}
  */
  js.compiling = false;
}

void Jit::Comp_VBranch(u32 op)
{
  /*
  Comp_Generic(op + 4);
  Comp_Generic(op);
  js.compiling = false;
  return;

  int imm = (signed short)(op&0xFFFF)<<2;
  u32 targetAddr = js.compilerPC + imm + 4;

  int imm3 = (op >> 18) & 7;
  int val = (mips_->vfpuCtrl[VFPU_CTRL_CC] >> imm3) & 1;

  switch ((op >> 16) & 3)
  {
  //case 0: if (!val) DelayBranchTo(addr); else PC += 4; break; //bvf
  //case 1: if ( val) DelayBranchTo(addr); else PC += 4; break; //bvt
  //case 2: if (!val) DelayBranchTo(addr); else PC += 8; break; //bvfl
  //case 3: if ( val) DelayBranchTo(addr); else PC += 8; break; //bvtl
    //TODO
  }
  js.compiling = false;
  */
}

void Jit::Comp_Jump(u32 op)
{
  /*
	u32 off = ((op & 0x3FFFFFF) << 2);
	u32 targetAddr = (js.compilerPC & 0xF0000000) | off;
	//Delay slot
	CompileAt(js.compilerPC + 4);
	FlushAll();

	switch (op >> 26) 
	{
	case 2: //j
    WriteExit(targetAddr, 0);
    break; 

	case 3: //jal
		MOV(32, M(&mips_->r[MIPS_REG_RA]), Imm32(js.compilerPC + 8));  // Save return address
    WriteExit(targetAddr, 0);
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
  */
}

void Jit::Comp_JumpReg(u32 op)
{
  /*
  int rs = _RS;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);
	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rs;
  // Do what with that information?

  gpr.BindToRegister(rs, true, false);
  PUSH(32, gpr.R(rs));
  CompileAt(js.compilerPC + 4);
  FlushAll();
  POP(32, R(EAX));
 
	switch (op & 0x3f) 
	{
	case 8: //jr
		break;
	case 9: //jalr
		MOV(32, M(&mips_->r[MIPS_REG_RA]), Imm32(js.compilerPC + 8));
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}

  WriteExitDestInEAX();
  js.compiling = false;
  */
}

	
void Jit::Comp_Syscall(u32 op)
{
  /*
	FlushAll();

  MOV(32, R(EAX), M(&mips_->r[MIPS_REG_RA]));
	MOV(32, M(&mips_->pc), R(EAX));

  ABI_CallFunctionC(&CallSyscall, op);

  WriteSyscallExit();*/

}

}   // namespace Mipscomp