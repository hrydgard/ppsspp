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

#include "Core/Core.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSDisVFPU.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSIntVFPU.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/Debugger/Breakpoints.h"

#include "JitCommon/JitCommon.h"

enum MipsEncoding
{
	Imme,
	Spec,
	Spe2,
	Spe3,
	RegI,
	Cop0,
	Cop0CO,
	Cop1,
	Cop1BC,
	Cop1S,
	Cop1W,
	Cop2,
	Cop2BC2,
	Cop2Rese,
	VFPU0,
	VFPU1,
	VFPU3,
	VFPU4Jump,
	VFPU7,
	VFPU4,
	VFPU5,
	VFPU6,
	VFPUMatrix1,
	VFPU9,
	ALLEGREX0,
	Emu,
	Rese,
	NumEncodings,

	Instruc = -1,
	Inval = -2,
};

struct MIPSInstruction
{
	MipsEncoding altEncoding;
	const char *name;
	MIPSComp::MIPSCompileFunc compile;
#ifndef FINAL
	MIPSDisFunc disasm;
#endif
	MIPSInterpretFunc interpret;
	//MIPSInstructionInfo information;
	MIPSInfo flags;
};

#define INVALID {Inval}
#define INVALID_X_8 INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID
#define N(a) a

#ifndef FINAL
#define ENCODING(a) {a}
#define INSTR(name, comp, dis, inter, flags) {Instruc, N(name), comp, dis, inter, MIPSInfo(flags)}
#else
#define ENCODING(a) {a}
#define INSTR(name, comp, dis, inter, flags) {Instruc, comp, inter, flags}
#endif


using namespace MIPSDis;
using namespace MIPSInt;
using namespace MIPSComp;
//regregreg instructions
const MIPSInstruction tableImmediate[64] = // xxxxxx ..... ..... ................
{
	//0
	ENCODING(Spec),
	ENCODING(RegI),
	INSTR("j",    &Jit::Comp_Jump, Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|DELAYSLOT),
	INSTR("jal",  &Jit::Comp_Jump, Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|OUT_RA|DELAYSLOT),
	INSTR("beq",  &Jit::Comp_RelBranch, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_EQ),
	INSTR("bne",  &Jit::Comp_RelBranch, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_NE),
	INSTR("blez", &Jit::Comp_RelBranch, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LEZ),
	INSTR("bgtz", &Jit::Comp_RelBranch, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GTZ),
	//8
	INSTR("addi",  &Jit::Comp_IType, Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("addiu", &Jit::Comp_IType, Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("slti",  &Jit::Comp_IType, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("sltiu", &Jit::Comp_IType, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("andi",  &Jit::Comp_IType, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("ori",   &Jit::Comp_IType, Dis_ori,    Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("xori",  &Jit::Comp_IType, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("lui",   &Jit::Comp_IType, Dis_IType1, Int_IType, IN_IMM16|OUT_RT),
	//16
	ENCODING(Cop0), //cop0
	ENCODING(Cop1), //cop1
	ENCODING(Cop2), //cop2
	INVALID, //copU

	INSTR("beql",  &Jit::Comp_RelBranch, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_EQ), //L = likely
	INSTR("bnel",  &Jit::Comp_RelBranch, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_NE),
	INSTR("blezl", &Jit::Comp_RelBranch, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LEZ),
	INSTR("bgtzl", &Jit::Comp_RelBranch, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GTZ),
	//24
	ENCODING(VFPU0),
	ENCODING(VFPU1),
	ENCODING(Emu),
	ENCODING(VFPU3),
	ENCODING(Spe2),//special2
	INVALID, //, "jalx", 0, Dis_JumpType, Int_JumpType},
	INVALID,
	ENCODING(Spe3),//special3
	//32
	INSTR("lb",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lh",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwl", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_WORD),
	INSTR("lw",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_WORD),
	INSTR("lbu", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lhu", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwr", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_WORD),
	INVALID,
	//40
	INSTR("sb",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_BYTE),
	INSTR("sh",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_HWORD),
	INSTR("swl", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("sw",  &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INVALID,
	INVALID,
	INSTR("swr", &Jit::Comp_ITypeMem, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("cache", &Jit::Comp_Cache, Dis_Cache, Int_Cache, IN_MEM|IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_OTHER),
	//48
	INSTR("ll", &Jit::Comp_Generic, Dis_Generic, Int_StoreSync, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|OUT_OTHER|MEMTYPE_WORD),
	INSTR("lwc1", &Jit::Comp_FPULS, Dis_FPULS, Int_FPULS, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|MEMTYPE_FLOAT),
	INSTR("lv.s", &Jit::Comp_SV, Dis_SV, Int_SV, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID, // HIT THIS IN WIPEOUT
	ENCODING(VFPU4Jump),
	INSTR("lv", &Jit::Comp_SVQ, Dis_SVLRQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	INSTR("lv.q", &Jit::Comp_SVQ, Dis_SVQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	ENCODING(VFPU5),
	//56
	INSTR("sc", &Jit::Comp_Generic, Dis_Generic, Int_StoreSync, IN_IMM16|IN_RS_ADDR|IN_OTHER|IN_RT|OUT_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("swc1", &Jit::Comp_FPULS, Dis_FPULS, Int_FPULS, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|MEMTYPE_FLOAT), //copU
	INSTR("sv.s", &Jit::Comp_SV, Dis_SV, Int_SV, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID,
	//60
	ENCODING(VFPU6),
	INSTR("sv", &Jit::Comp_SVQ, Dis_SVLRQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	INSTR("sv.q", &Jit::Comp_SVQ, Dis_SVQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	// Some call this VFPU7 (vflush/vnop/vsync), but it's not super important.
	INSTR("vflush", &Jit::Comp_DoNothing, Dis_Vflush, Int_Vflush, IS_VFPU|VFPU_NO_PREFIX),
};

const MIPSInstruction tableSpecial[64] = // 000000 ..... ..... ..... ..... xxxxxx
{
	INSTR("sll",   &Jit::Comp_ShiftType, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INVALID,  // copu

	INSTR("srl",   &Jit::Comp_ShiftType, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sra",   &Jit::Comp_ShiftType, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sllv",  &Jit::Comp_ShiftType, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INVALID,
	INSTR("srlv",  &Jit::Comp_ShiftType, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INSTR("srav",  &Jit::Comp_ShiftType, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),

	//8
	INSTR("jr",    &Jit::Comp_JumpReg, Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|DELAYSLOT),
	INSTR("jalr",  &Jit::Comp_JumpReg, Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|OUT_RD|DELAYSLOT),
	INSTR("movz",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_EQ),
	INSTR("movn",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_NE),
	INSTR("syscall", &Jit::Comp_Syscall, Dis_Syscall, Int_Syscall, IN_MEM|IN_OTHER|OUT_MEM|OUT_OTHER),
	INSTR("break", &Jit::Comp_Break, Dis_Generic, Int_Break, 0),
	INVALID,
	INSTR("sync",  &Jit::Comp_DoNothing, Dis_Generic, Int_Sync, 0),

	//16
	INSTR("mfhi",  &Jit::Comp_MulDivType, Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_OTHER),
	INSTR("mthi",  &Jit::Comp_MulDivType, Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_OTHER),
	INSTR("mflo",  &Jit::Comp_MulDivType, Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_OTHER),
	INSTR("mtlo",  &Jit::Comp_MulDivType, Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_OTHER),
	INVALID,
	INVALID,
	INSTR("clz",   &Jit::Comp_RType2, Dis_RType2, Int_RType2, OUT_RD|IN_RS),
	INSTR("clo",   &Jit::Comp_RType2, Dis_RType2, Int_RType2, OUT_RD|IN_RS),

	//24
	INSTR("mult",  &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_OTHER),
	INSTR("multu", &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_OTHER),
	INSTR("div",   &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_OTHER),
	INSTR("divu",  &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_OTHER),
	INSTR("madd",  &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_OTHER|OUT_OTHER),
	INSTR("maddu", &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_OTHER|OUT_OTHER),
	INVALID,
	INVALID,

	//32
	INSTR("add",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("addu", &Jit::Comp_RType3, Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sub",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("subu", &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("and",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("or",   &Jit::Comp_RType3, Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("xor",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("nor",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),

	//40
	INVALID,
	INVALID,
	INSTR("slt",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sltu", &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("max",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("min",  &Jit::Comp_RType3, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("msub",  &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_OTHER|OUT_OTHER),
	INSTR("msubu", &Jit::Comp_MulDivType, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_OTHER|OUT_OTHER),

	//48
	INSTR("tge",  &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INSTR("tgeu", &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INSTR("tlt",  &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INSTR("tltu", &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INSTR("teq",  &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INVALID,
	INSTR("tne",  &Jit::Comp_Generic, Dis_RType3, 0, 0),
	INVALID,

	//56
	INVALID_X_8,
};

// Theoretically should not hit these.
const MIPSInstruction tableSpecial2[64] = // 011100 ..... ..... ..... ..... xxxxxx
{
	INSTR("halt", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	INSTR("mfic", &Jit::Comp_Generic, Dis_Generic, Int_Special2, 0),
	INVALID,
	INSTR("mtic", &Jit::Comp_Generic, Dis_Generic, Int_Special2, 0),
	INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableSpecial3[64] = // 011111 ..... ..... ..... ..... xxxxxx
{
	INSTR("ext", &Jit::Comp_Special3, Dis_Special3, Int_Special3, IN_RS|OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	INSTR("ins", &Jit::Comp_Special3, Dis_Special3, Int_Special3, IN_RS|IN_RT|OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	//8
	INVALID_X_8,
	//16
	INVALID_X_8,
	//24
	// TODO: Is this right?  Or should it only be 32?  Comment above (24) was mistakenly 32 before.
	ENCODING(ALLEGREX0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//32
	ENCODING(ALLEGREX0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	//56
	INVALID, INVALID, INVALID,
	INSTR("rdhwr", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID,
};

const MIPSInstruction tableRegImm[32] = // 000001 ..... xxxxx ................
{
	INSTR("bltz",  &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgez",  &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzl", &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LTZ),
	INSTR("bgezl", &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//8
	INSTR("tgei",  &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("tgeiu", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("tlti",  &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("tltiu", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("teqi",  &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID,
	INSTR("tnei",  &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID,
	//16
	INSTR("bltzal",  &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgezal",  &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzall", &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_LTZ), //L = likely
	INSTR("bgezall", &Jit::Comp_RelBranchRI, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//24
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("synci", &Jit::Comp_Generic, Dis_Generic, 0, 0),
};

const MIPSInstruction tableCop2[32] = // 010010 xxxxx ..... ................
{
	INSTR("mfc2", &Jit::Comp_Generic, Dis_Generic, 0, OUT_RT),
	INVALID,
	INSTR("cfc2", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("mfv", &Jit::Comp_Mftv, Dis_Mftv, Int_Mftv, IN_OTHER|OUT_RT|IS_VFPU),
	INSTR("mtc2", &Jit::Comp_Generic, Dis_Generic, 0, IN_RT),
	INVALID,
	INSTR("ctc2", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("mtv", &Jit::Comp_Mftv, Dis_Mftv, Int_Mftv, IN_RT|OUT_OTHER|IS_VFPU),
	//8
	ENCODING(Cop2BC2),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("??", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	//16
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop2BC2[4] = // 010010 01000 ...xx ................
{
	INSTR("bvf", &Jit::Comp_VBranch, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvt", &Jit::Comp_VBranch, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvfl", &Jit::Comp_VBranch, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
	INSTR("bvtl", &Jit::Comp_VBranch, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
};

const MIPSInstruction tableCop0[32] = // 010000 xxxxx ..... ................
{
	INSTR("mfc0", &Jit::Comp_Generic, Dis_Generic, 0, OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	INSTR("mtc0", &Jit::Comp_Generic, Dis_Generic, 0, IN_RT),
	INVALID,
	INVALID,
	INVALID,
	//8
	INVALID,
	INVALID,
	INSTR("rdpgpr", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("mfmc0", &Jit::Comp_Generic, Dis_Generic, 0, 0),

	INVALID,
	INVALID,
	INSTR("wrpgpr", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID,
	//16
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
};

// we won't encounter these since we only do user mode emulation
const MIPSInstruction tableCop0CO[64] = // 010000 1.... ..... ..... ..... xxxxxx
{
	INVALID,
	INSTR("tlbr", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("tlbwi", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID,
	INVALID,
	INVALID,
	INSTR("tlbwr", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID,
	//8
	INSTR("tlbp", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID_X_8,
	//24
	INSTR("eret", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INSTR("iack", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("deret", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	//32
	INSTR("wait", &Jit::Comp_Generic, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop1[32] = // 010001 xxxxx ..... ..... ...........
{
	INSTR("mfc1", &Jit::Comp_mxc1, Dis_mxc1, Int_mxc1, IN_OTHER|OUT_RT),
	INVALID,
	INSTR("cfc1", &Jit::Comp_mxc1, Dis_mxc1, Int_mxc1, IN_OTHER|IN_FPUFLAG|OUT_RT),
	INVALID,
	INSTR("mtc1", &Jit::Comp_mxc1, Dis_mxc1, Int_mxc1, IN_RT|OUT_OTHER),
	INVALID,
	INSTR("ctc1", &Jit::Comp_mxc1, Dis_mxc1, Int_mxc1, IN_RT|OUT_FPUFLAG|OUT_OTHER),
	INVALID,
	//8
	ENCODING(Cop1BC), INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//16
	ENCODING(Cop1S), INVALID, INVALID, INVALID,
	ENCODING(Cop1W), INVALID, INVALID, INVALID,
	//24
	INVALID_X_8,
};

const MIPSInstruction tableCop1BC[32] = // 010001 01000 xxxxx ................
{
	INSTR("bc1f",  &Jit::Comp_FPUBranch, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUFALSE),
	INSTR("bc1t",  &Jit::Comp_FPUBranch, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUTRUE),
	INSTR("bc1fl", &Jit::Comp_FPUBranch, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUFALSE),
	INSTR("bc1tl", &Jit::Comp_FPUBranch, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUTRUE),
	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop1S[64] = // 010001 10000 ..... ..... ..... xxxxxx
{
	INSTR("add.s",  &Jit::Comp_FPU3op, Dis_FPU3op, Int_FPU3op, IN_OTHER|OUT_OTHER),
	INSTR("sub.s",  &Jit::Comp_FPU3op, Dis_FPU3op, Int_FPU3op, IN_OTHER|OUT_OTHER),
	INSTR("mul.s",  &Jit::Comp_FPU3op, Dis_FPU3op, Int_FPU3op, IN_OTHER|OUT_OTHER),
	INSTR("div.s",  &Jit::Comp_FPU3op, Dis_FPU3op, Int_FPU3op, IN_OTHER|OUT_OTHER),
	INSTR("sqrt.s", &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("abs.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("mov.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("neg.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	//8
	INVALID, INVALID, INVALID, INVALID,
	INSTR("round.w.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("trunc.w.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("ceil.w.s",   &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INSTR("floor.w.s",  &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	//36
	INSTR("cvt.w.s", &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INVALID,
	INSTR("dis.int", &Jit::Comp_Generic, Dis_Generic, Int_Interrupt, 0),
	INVALID,
	//40
	INVALID_X_8,
	//48 - 010001 10000 ..... ..... ..... 11xxxx
	INSTR("c.f",   &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.un",  &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.eq",  &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ueq", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.olt", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ult", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ole", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ule", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.sf",  &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ngle",&Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.seq", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ngl", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.lt",  &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.nge", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.le",  &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
	INSTR("c.ngt", &Jit::Comp_FPUComp, Dis_FPUComp, Int_FPUComp, IN_OTHER|OUT_FPUFLAG),
};

const MIPSInstruction tableCop1W[64] = // 010001 10100 ..... ..... ..... xxxxxx
{
	INVALID_X_8,
	//8
	INVALID_X_8,
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INSTR("cvt.s.w", &Jit::Comp_FPU2op, Dis_FPU2op, Int_FPU2op, IN_OTHER|OUT_OTHER),
	INVALID, INVALID, INVALID,
	//36
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//40
	INVALID_X_8,
	//48
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableVFPU0[8] = // 011000 xxx ....... . ....... . .......
{
	INSTR("vadd", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsub", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsbn", &Jit::Comp_Generic, Dis_VectorSet3, Int_Vsbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INVALID, INVALID, INVALID, INVALID,

	INSTR("vdiv", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU1[8] = // 011001 xxx ....... . ....... . .......
{
	INSTR("vmul", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdot", &Jit::Comp_VDot, Dis_VectorDot, Int_VDot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vscl", &Jit::Comp_VScl, Dis_VScl, Int_VScl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vhdp", &Jit::Comp_VHdp, Dis_VectorDot, Int_VHdp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrs", &Jit::Comp_VCrs, Dis_Vcrs, Int_Vcrs, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdet", &Jit::Comp_VDet, Dis_VectorDot, Int_Vdet, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
};

const MIPSInstruction tableVFPU3[8] = // 011011 xxx ....... . ....... . .......
{
	INSTR("vcmp", &Jit::Comp_Vcmp, Dis_Vcmp, Int_Vcmp, IN_OTHER|OUT_VFPU_CC|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vmin", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmax", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vscmp", &Jit::Comp_Generic, Dis_VectorSet3, Int_Vscmp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsge", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_Vsge, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vslt", &Jit::Comp_VecDo3, Dis_VectorSet3, Int_Vslt, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU4Jump[32] = // 110100 xxxxx ..... . ....... . .......
{
	ENCODING(VFPU4),
	ENCODING(VFPU7),
	ENCODING(VFPU9),
	INSTR("vcst", &Jit::Comp_Vcst, Dis_Vcst, Int_Vcst, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID, INVALID,

	//8
	INVALID_X_8,

	//16
	INSTR("vf2in", &Jit::Comp_Vf2i, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iz", &Jit::Comp_Vf2i, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iu", &Jit::Comp_Vf2i, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2id", &Jit::Comp_Vf2i, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//20
	INSTR("vi2f", &Jit::Comp_Vi2f, Dis_Vf2i, Int_Vi2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcmov", &Jit::Comp_Vcmov, Dis_Vcmov, Int_Vcmov, IN_OTHER|IN_VFPU_CC|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	//24 - 110100 11 ........ . ....... . .......
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", &Jit::Comp_Generic, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
};

const MIPSInstruction tableVFPU7[32] = // 110100 00001 xxxxx . ....... . .......
{
	// TODO disasm
	INSTR("vrnds", &Jit::Comp_Generic, Dis_Vrnds, Int_Vrnds, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndi", &Jit::Comp_Generic, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf1", &Jit::Comp_Generic, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf2", &Jit::Comp_Generic, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID, INVALID, INVALID, INVALID,
	// TODO: Flags may not be correct (prefixes, etc.)  Is this the correct encoding?  Others say 10110.
	INSTR("vsbz", &Jit::Comp_Generic, Dis_Generic, Int_Vsbz, IN_OTHER|OUT_OTHER|IS_VFPU),
	INVALID, INVALID, INVALID,
	//16
	INVALID,
	INVALID,
	INSTR("vf2h", &Jit::Comp_Generic, Dis_Vf2h, Int_Vf2h, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vh2f", &Jit::Comp_Vh2f, Dis_Vh2f, Int_Vh2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INVALID,
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vlgb", &Jit::Comp_Generic, Dis_Generic, Int_Vlgb, IN_OTHER|OUT_OTHER|IS_VFPU),
	//24
	INSTR("vuc2i", &Jit::Comp_Vx2i, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // Seen in BraveStory, initialization  110100 00001110000 000 0001 0000 0000
	INSTR("vc2i",  &Jit::Comp_Vx2i, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vus2i", &Jit::Comp_Vx2i, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vs2i",  &Jit::Comp_Vx2i, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vi2uc", &Jit::Comp_Vi2x, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2c",  &Jit::Comp_Vi2x, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2us", &Jit::Comp_Vi2x, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2s",  &Jit::Comp_Vi2x, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

// 110100 00000 10100 0000000000000000
// 110100 00000 10111 0000000000000000
const MIPSInstruction tableVFPU4[32] = // 110100 00000 xxxxx . ....... . .......
{
	INSTR("vmov", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vabs", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vneg", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vidt", &Jit::Comp_VIdt, Dis_VectorSet1, Int_Vidt, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat0", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat1", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vzero", &Jit::Comp_VVectorInit, Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vone",  &Jit::Comp_VVectorInit, Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INVALID_X_8,
	//16
	INSTR("vrcp", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrsq", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsin", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcos", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vexp2", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vlog2", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsqrt", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vasin", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INSTR("vnrcp", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vnsin", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vrexp2", &Jit::Comp_VV2Op, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID,
};

MIPSInstruction tableVFPU5[8] = // 110111 xxx ....... ................
{
	INSTR("vpfxs", &Jit::Comp_VPFX, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxs", &Jit::Comp_VPFX, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", &Jit::Comp_VPFX, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", &Jit::Comp_VPFX, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", &Jit::Comp_VPFX, Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", &Jit::Comp_VPFX, Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("viim.s", &Jit::Comp_Viim, Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfim.s", &Jit::Comp_Vfim, Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU6[32] = // 111100 xxxxx ..... . ....... . .......
{
	//0
	INSTR("vmmul", &Jit::Comp_Vmmul, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", &Jit::Comp_Vmmul, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", &Jit::Comp_Vmmul, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", &Jit::Comp_Vmmul, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm2", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INSTR("v(h)tfm3", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm4", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", &Jit::Comp_Vtfm, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//16
	INSTR("vmscl", &Jit::Comp_Vmscl, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", &Jit::Comp_Vmscl, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", &Jit::Comp_Vmscl, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", &Jit::Comp_Vmscl, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vcrsp.t/vqmul.q", &Jit::Comp_VCrossQuat, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", &Jit::Comp_VCrossQuat, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", &Jit::Comp_VCrossQuat, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", &Jit::Comp_VCrossQuat, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//28
	ENCODING(VFPUMatrix1),
	INSTR("vrot", &Jit::Comp_VRot, Dis_VRot, Int_Vrot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
};

// TODO: Should this only be when bit 20 is 0?
const MIPSInstruction tableVFPUMatrixSet1[16] = // 111100 11100 .xxxx . ....... . .......  (rm x is 16)
{
	INSTR("vmmov", &Jit::Comp_Vmmov, Dis_MatrixSet2, Int_Vmmov, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	INSTR("vmidt", &Jit::Comp_VMatrixInit, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INSTR("vmzero", &Jit::Comp_VMatrixInit, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmone",  &Jit::Comp_VMatrixInit, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID_X_8,
};

const MIPSInstruction tableVFPU9[32] = // 110100 00010 xxxxx . ....... . .......
{
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt1", &Jit::Comp_Generic, Dis_Vbfy, Int_Vsrt1, IN_OTHER|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt2", &Jit::Comp_Generic, Dis_Vbfy, Int_Vsrt2, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vbfy1", &Jit::Comp_Generic, Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vbfy2", &Jit::Comp_Generic, Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//4
	INSTR("vocp", &Jit::Comp_Vocp, Dis_Vbfy, Int_Vocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // one's complement
	INSTR("vsocp", &Jit::Comp_Generic, Dis_Vbfy, Int_Vsocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfad", &Jit::Comp_Vhoriz, Dis_Vfad, Int_Vfad, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vavg", &Jit::Comp_Vhoriz, Dis_Vfad, Int_Vavg, IN_OTHER|OUT_OTHER|IS_VFPU),
	//8
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt3", &Jit::Comp_Generic, Dis_Vbfy, Int_Vsrt3, IN_OTHER|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt4", &Jit::Comp_Generic, Dis_Vbfy, Int_Vsrt4, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vsgn", &Jit::Comp_Vsgn, Dis_Vbfy, Int_Vsgn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	//12
	INVALID,
	INVALID,
	INVALID,
	INVALID,

	//16
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vmfvc", &Jit::Comp_Generic, Dis_Vmftvc, Int_Vmfvc, IN_OTHER|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vmtvc", &Jit::Comp_Generic, Dis_Vmftvc, Int_Vmtvc, IN_OTHER|OUT_OTHER|IS_VFPU),
	INVALID,
	INVALID,

	//20
	INVALID, INVALID, INVALID, INVALID,
	//24
	INVALID,
	INSTR("vt4444", &Jit::Comp_Generic, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5551", &Jit::Comp_Generic, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5650", &Jit::Comp_Generic, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	//28
	INVALID, INVALID, INVALID, INVALID,
};

const MIPSInstruction tableALLEGREX0[32] =  // 011111 ..... ..... ..... xxxxx 100000 - or ending with 011000?
{
	INVALID,
	INVALID,
	INSTR("wsbh", &Jit::Comp_Allegrex2, Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INSTR("wsbw", &Jit::Comp_Allegrex2, Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INVALID, INVALID, INVALID, INVALID,
//8
	INVALID_X_8,
//16
	INSTR("seb", &Jit::Comp_Allegrex, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//20
	INSTR("bitrev", &Jit::Comp_Allegrex, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//24
	INSTR("seh", &Jit::Comp_Allegrex, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//28
	INVALID,
	INVALID,
	INVALID,
	INVALID,
};


const MIPSInstruction tableEMU[4] = {
	INSTR("RUNBLOCK", &Jit::Comp_RunBlock, Dis_Emuhack, Int_Emuhack, 0xFFFFFFFF),
	INSTR("RetKrnl", 0, Dis_Emuhack, Int_Emuhack, 0),
	INSTR("CallRepl", &Jit::Comp_ReplacementFunc, Dis_Emuhack, Int_Emuhack, 0),
	INVALID,
};

struct EncodingBitsInfo {
	EncodingBitsInfo(u8 shift_, u8 maskBits_) : shift(shift_) {
		mask = (1 << maskBits_) - 1;
	}
	u8 shift;
	u32 mask;
};

const EncodingBitsInfo encodingBits[NumEncodings] =
{
	EncodingBitsInfo(26, 6), //IMME
	EncodingBitsInfo(0,  6), //Special
	EncodingBitsInfo(0,  6), //special2
	EncodingBitsInfo(0,  6), //special3
	EncodingBitsInfo(16, 5), //RegImm
	EncodingBitsInfo(21, 5), //Cop0
	EncodingBitsInfo(0,  6), //Cop0CO
	EncodingBitsInfo(21, 5), //Cop1
	EncodingBitsInfo(16, 5), //Cop1BC
	EncodingBitsInfo(0,  6), //Cop1S
	EncodingBitsInfo(0,  6), //Cop1W
	EncodingBitsInfo(21, 5), //Cop2
	EncodingBitsInfo(16, 2), //Cop2BC2
	EncodingBitsInfo(0,  0), //Cop2Rese
	EncodingBitsInfo(23, 3), //VFPU0
	EncodingBitsInfo(23, 3), //VFPU1
	EncodingBitsInfo(23, 3), //VFPU3
	EncodingBitsInfo(21, 5), //VFPU4Jump
	EncodingBitsInfo(16, 5), //VFPU7
	EncodingBitsInfo(16, 5), //VFPU4
	EncodingBitsInfo(23, 3), //VFPU5
	EncodingBitsInfo(21, 5), //VFPU6
	EncodingBitsInfo(16, 4), //VFPUMatrix1
	EncodingBitsInfo(16, 5), //VFPU9
	EncodingBitsInfo(6,  5), //ALLEGREX0
	EncodingBitsInfo(24, 2), //EMUHACK
	EncodingBitsInfo(0,  0), //Rese
};

const MIPSInstruction *mipsTables[NumEncodings] =
{
	tableImmediate,
	tableSpecial,
	tableSpecial2,
	tableSpecial3,
	tableRegImm,
	tableCop0,
	tableCop0CO,
	tableCop1,
	tableCop1BC,
	tableCop1S,
	tableCop1W,
	tableCop2,
	tableCop2BC2,
	0,
	tableVFPU0, //vfpu0
	tableVFPU1, //vfpu1
	tableVFPU3, //vfpu3
	tableVFPU4Jump,
	tableVFPU7, //vfpu4     110100 00001
	tableVFPU4, //vfpu4     110100 00000
	tableVFPU5, //vfpu5     110111
	tableVFPU6, //vfpu6     111100
	tableVFPUMatrixSet1,
	tableVFPU9,
	tableALLEGREX0,
	tableEMU,
	0,
};



//arm encoding table
//const MIPSInstruction mipsinstructions[] = 
//{
//{Comp_Unimpl,Dis_Unimpl, Info_NN,    0, 0x601,       0x1FE,0}, //could be used for drec hook :) bits 5-24 plus 0-3 are available, 19 bits are more than enough
// DATA PROCESSING INSTRUCTIONS
//                        S
//	{Comp_AND,   Dis_AND,    Info_DP,    0, DATAP(0, 0), 0x20F, {0}},
//};


//Todo : generate dispatcher functions from above tables
//instead of this horribly slow abomination

const MIPSInstruction *MIPSGetInstruction(MIPSOpcode op)
{
	MipsEncoding encoding = Imme;
	const MIPSInstruction *instr = &tableImmediate[op>>26];
	while (instr->altEncoding != Instruc)
	{
		if (instr->altEncoding == Inval)
		{
			//ERROR_LOG(CPU, "Invalid instruction %08x in table %i, entry %i", op, (int)encoding, subop);
			return 0; //invalid instruction
		}
		encoding = instr->altEncoding;

		const MIPSInstruction *table = mipsTables[encoding];
		const u32 subop = (op >> encodingBits[encoding].shift) & encodingBits[encoding].mask;
		instr = &table[subop];
	}
	//alright, we have a valid MIPS instruction!
	return instr;
}

void MIPSCompileOp(MIPSOpcode op)
{
	if (op == 0)
		return;
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	const MIPSInfo info = MIPSGetInfo(op);
	if (instr)
	{
		if (instr->compile) {
			(MIPSComp::jit->*(instr->compile))(op);
		} else {
			ERROR_LOG_REPORT(CPU,"MIPSCompileOp %08x failed",op.encoding);
		}

		if (info & OUT_EAT_PREFIX)
			MIPSComp::jit->EatPrefix();
	}
	else
	{
		ERROR_LOG_REPORT(CPU, "MIPSCompileOp: Invalid instruction %08x", op.encoding);
	}
}

void MIPSDisAsm(MIPSOpcode op, u32 pc, char *out, bool tabsToSpaces)
{
	if (op == 0) {
		sprintf(out,"nop");
	} else {
		disPC = pc;
		const MIPSInstruction *instr = MIPSGetInstruction(op);
		if (instr && instr->disasm) {
			instr->disasm(op, out);
			if (tabsToSpaces) {
				while (*out) {
					if (*out == '\t')
						*out = ' ';
					out++;
				}
			}
		} else {
			strcpy(out, "no instruction :(");
			MIPSGetInstruction(op);
		}
	}
}

void MIPSInterpret(MIPSOpcode op) {
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (instr && instr->interpret) {
		instr->interpret(op);
	} else {
		ERROR_LOG_REPORT(CPU, "Unknown instruction %08x at %08x", op.encoding, currentMIPS->pc);
		// Try to disassemble it
		char disasm[256];
		MIPSDisAsm(op, currentMIPS->pc, disasm);
		_dbg_assert_msg_(CPU, 0, "%s", disasm);
		currentMIPS->pc += 4;
	}
}

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define R(i)   (curMips->r[i])


int MIPSInterpret_RunUntil(u64 globalTicks)
{
	MIPSState *curMips = currentMIPS;
	while (coreState == CORE_RUNNING)
	{
		CoreTiming::Advance();

		// NEVER stop in a delay slot!
		while (curMips->downcount >= 0 && coreState == CORE_RUNNING)
		{
			// int cycles = 0;
			{
				again:
				MIPSOpcode op = MIPSOpcode(Memory::Read_U32(curMips->pc));
				//MIPSOpcode op = Memory::Read_Opcode_JIT(mipsr4k.pc);
				/*
				// Choke on VFPU
				MIPSInfo info = MIPSGetInfo(op);
				if (info & IS_VFPU)
				{
					if (!Core_IsStepping() && !GetAsyncKeyState(VK_LSHIFT))
					{
						Core_EnableStepping(true);
						return;
					}
				}*/

		//2: check for breakpoint (VERY SLOW)
#if defined(_DEBUG)
				if (CBreakPoints::IsAddressBreakPoint(curMips->pc))
				{
					auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
					if (!cond || cond->Evaluate())
					{
						Core_EnableStepping(true);
						if (CBreakPoints::IsTempBreakPoint(curMips->pc))
							CBreakPoints::RemoveBreakPoint(curMips->pc);
						break;
					}
				}
#endif

				bool wasInDelaySlot = curMips->inDelaySlot;

				MIPSInterpret(op);

				if (curMips->inDelaySlot)
				{
					// The reason we have to check this is the delay slot hack in Int_Syscall.
					if (wasInDelaySlot)
					{
						curMips->pc = curMips->nextPC;
						curMips->inDelaySlot = false;
					}
					curMips->downcount -= 1;
					goto again;
				}
			}

			curMips->downcount -= 1;
			if (CoreTiming::GetTicks() > globalTicks)
			{
				// DEBUG_LOG(CPU, "Hit the max ticks, bailing 1 : %llu, %llu", globalTicks, CoreTiming::GetTicks());
				return 1;
			}
		}
	}

	return 1;
}

static inline void DelayBranchTo(MIPSState *curMips, u32 where)
{
	curMips->pc += 4;
	curMips->nextPC = where;
	curMips->inDelaySlot = true;
}

const char *MIPSGetName(MIPSOpcode op)
{
	static const char *noname = "unk";
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (!instr)
		return noname;
	else
		return instr->name;
}

MIPSInfo MIPSGetInfo(MIPSOpcode op)
{
	//	int crunch = CRUNCH_MIPS_OP(op);
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (instr)
		return instr->flags;
	else
		return MIPSInfo(BAD_INSTRUCTION);
}

MIPSInterpretFunc MIPSGetInterpretFunc(MIPSOpcode op)
{
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (instr->interpret)
		return instr->interpret;
	else
		return 0;
}

// TODO: Do something that makes sense here.
int MIPSGetInstructionCycleEstimate(MIPSOpcode op)
{
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT)
		return 2;
	else
		return 1;
}

const char *MIPSDisasmAt(u32 compilerPC) {
	static char temp[256];
	MIPSDisAsm(Memory::Read_Instruction(compilerPC), 0, temp);
	return temp;
}
