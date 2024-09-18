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

#include "Common/StringUtils.h"

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

enum MipsEncoding {
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

struct MIPSInstruction {
	MipsEncoding altEncoding;
	const char *name;
	MIPSComp::MIPSCompileFunc compile;
	MIPSDisFunc disasm;
	MIPSInterpretFunc interpret;
	//MIPSInstructionInfo information;
	MIPSInfo flags;
};

#define INVALID {Inval}
#define INVALID_X_8 INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID

#define ENCODING(a) {a}
#define INSTR(name, comp, dis, inter, flags) {Instruc, name, comp, dis, inter, MIPSInfo(flags)}

#define JITFUNC(f) (&MIPSFrontendInterface::f)

using namespace MIPSDis;
using namespace MIPSInt;
using namespace MIPSComp;

// %s/&Jit::\(.\{-}\),/JITFUNC(\1),/g

// regregreg instructions
static const MIPSInstruction tableImmediate[64] = // xxxxxx ..... ..... ................
{
	//0
	ENCODING(Spec),
	ENCODING(RegI),
	INSTR("j",    JITFUNC(Comp_Jump), Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|DELAYSLOT),
	INSTR("jal",  JITFUNC(Comp_Jump), Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|OUT_RA|DELAYSLOT),
	INSTR("beq",  JITFUNC(Comp_RelBranch), Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_EQ),
	INSTR("bne",  JITFUNC(Comp_RelBranch), Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_NE),
	INSTR("blez", JITFUNC(Comp_RelBranch), Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LEZ),
	INSTR("bgtz", JITFUNC(Comp_RelBranch), Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GTZ),
	//8
	INSTR("addi",  JITFUNC(Comp_IType), Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("addiu", JITFUNC(Comp_IType), Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("slti",  JITFUNC(Comp_IType), Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("sltiu", JITFUNC(Comp_IType), Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("andi",  JITFUNC(Comp_IType), Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("ori",   JITFUNC(Comp_IType), Dis_ori,    Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("xori",  JITFUNC(Comp_IType), Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("lui",   JITFUNC(Comp_IType), Dis_IType1, Int_IType, IN_IMM16|OUT_RT),
	//16
	ENCODING(Cop0), //cop0
	ENCODING(Cop1), //cop1
	ENCODING(Cop2), //cop2
	INVALID, //copU

	INSTR("beql",  JITFUNC(Comp_RelBranch), Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_EQ), //L = likely
	INSTR("bnel",  JITFUNC(Comp_RelBranch), Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_NE),
	INSTR("blezl", JITFUNC(Comp_RelBranch), Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LEZ),
	INSTR("bgtzl", JITFUNC(Comp_RelBranch), Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GTZ),
	//24
	ENCODING(VFPU0),
	ENCODING(VFPU1),
	ENCODING(Emu),
	ENCODING(VFPU3),
	ENCODING(Spe2), //special2
	INVALID,
	INVALID,
	ENCODING(Spe3), //special3
	//32
	INSTR("lb",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lh",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwl", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|IN_RT|OUT_RT|MEMTYPE_WORD),
	INSTR("lw",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_WORD),
	INSTR("lbu", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lhu", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwr", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|IN_RT|OUT_RT|MEMTYPE_WORD),
	INVALID,
	//40
	INSTR("sb",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_BYTE),
	INSTR("sh",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_HWORD),
	INSTR("swl", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("sw",  JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INVALID,
	INVALID,
	INSTR("swr", JITFUNC(Comp_ITypeMem), Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("cache", JITFUNC(Comp_Cache), Dis_Cache, Int_Cache, IN_MEM|IN_IMM16|IN_RS_ADDR),
	//48
	INSTR("ll", JITFUNC(Comp_StoreSync), Dis_ITypeMem, Int_StoreSync, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|OUT_OTHER|MEMTYPE_WORD),
	INSTR("lwc1", JITFUNC(Comp_FPULS), Dis_FPULS, Int_FPULS, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_FT|MEMTYPE_FLOAT|IS_FPU),
	INSTR("lv.s", JITFUNC(Comp_SV), Dis_SV, Int_SV, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID,
	ENCODING(VFPU4Jump),
	INSTR("lv", JITFUNC(Comp_SVQ), Dis_SVLRQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	INSTR("lv.q", JITFUNC(Comp_SVQ), Dis_SVQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	ENCODING(VFPU5),
	//56
	INSTR("sc", JITFUNC(Comp_StoreSync), Dis_ITypeMem, Int_StoreSync, IN_IMM16|IN_RS_ADDR|IN_OTHER|IN_RT|OUT_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("swc1", JITFUNC(Comp_FPULS), Dis_FPULS, Int_FPULS, IN_IMM16|IN_RS_ADDR|IN_FT|OUT_MEM|MEMTYPE_FLOAT|IS_FPU), //copU
	INSTR("sv.s", JITFUNC(Comp_SV), Dis_SV, Int_SV, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID,
	//60
	ENCODING(VFPU6),
	INSTR("sv", JITFUNC(Comp_SVQ), Dis_SVLRQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	INSTR("sv.q", JITFUNC(Comp_SVQ), Dis_SVQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	// Some call this VFPU7 (vflush/vnop/vsync), but it's not super important.
	INSTR("vflush", JITFUNC(Comp_DoNothing), Dis_Vflush, Int_Vflush, IS_VFPU|VFPU_NO_PREFIX),
};

static const MIPSInstruction tableSpecial[64] = // 000000 ..... ..... ..... ..... xxxxxx
{
	INSTR("sll",   JITFUNC(Comp_ShiftType), Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INVALID,  // copu

	INSTR("srl",   JITFUNC(Comp_ShiftType), Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sra",   JITFUNC(Comp_ShiftType), Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sllv",  JITFUNC(Comp_ShiftType), Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INVALID,
	INSTR("srlv",  JITFUNC(Comp_ShiftType), Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INSTR("srav",  JITFUNC(Comp_ShiftType), Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),

	//8
	INSTR("jr",    JITFUNC(Comp_JumpReg), Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|DELAYSLOT),
	INSTR("jalr",  JITFUNC(Comp_JumpReg), Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|OUT_RD|DELAYSLOT),
	INSTR("movz",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_EQ),
	INSTR("movn",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_NE),
	INSTR("syscall", JITFUNC(Comp_Syscall), Dis_Syscall, Int_Syscall, IN_MEM|IN_OTHER|OUT_MEM|OUT_OTHER|IS_SYSCALL),
	INSTR("break", JITFUNC(Comp_Break), Dis_Generic, Int_Break, 0),
	INVALID,
	INSTR("sync",  JITFUNC(Comp_DoNothing), Dis_Generic, Int_Sync, 0),

	//16
	INSTR("mfhi",  JITFUNC(Comp_MulDivType), Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_HI),
	INSTR("mthi",  JITFUNC(Comp_MulDivType), Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_HI),
	INSTR("mflo",  JITFUNC(Comp_MulDivType), Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_LO),
	INSTR("mtlo",  JITFUNC(Comp_MulDivType), Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_LO),
	INVALID,
	INVALID,
	INSTR("clz",   JITFUNC(Comp_RType2), Dis_RType2, Int_RType2, OUT_RD|IN_RS),
	INSTR("clo",   JITFUNC(Comp_RType2), Dis_RType2, Int_RType2, OUT_RD|IN_RS),

	//24
	INSTR("mult",  JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("multu", JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("div",   JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("divu",  JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("madd",  JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INSTR("maddu", JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INVALID,
	INVALID,

	//32
	INSTR("add",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("addu", JITFUNC(Comp_RType3), Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sub",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("subu", JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("and",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("or",   JITFUNC(Comp_RType3), Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("xor",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("nor",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),

	//40
	INVALID,
	INVALID,
	INSTR("slt",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sltu", JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("max",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("min",  JITFUNC(Comp_RType3), Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("msub",  JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INSTR("msubu", JITFUNC(Comp_MulDivType), Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),

	//48
	INSTR("tge",  JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INSTR("tgeu", JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INSTR("tlt",  JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INSTR("tltu", JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INSTR("teq",  JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INVALID,
	INSTR("tne",  JITFUNC(Comp_Generic), Dis_RType3, 0, 0),
	INVALID,

	//56
	INVALID_X_8,
};

// Theoretically should not hit these.
static const MIPSInstruction tableSpecial2[64] = // 011100 ..... ..... ..... ..... xxxxxx
{
	INSTR("halt", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	INSTR("mfic", JITFUNC(Comp_Generic), Dis_Generic, Int_Special2, OUT_OTHER),
	INVALID,
	INSTR("mtic", JITFUNC(Comp_Generic), Dis_Generic, Int_Special2, OUT_OTHER),
	INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

static const MIPSInstruction tableSpecial3[64] = // 011111 ..... ..... ..... ..... xxxxxx
{
	INSTR("ext", JITFUNC(Comp_Special3), Dis_Special3, Int_Special3, IN_RS|OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	INSTR("ins", JITFUNC(Comp_Special3), Dis_Special3, Int_Special3, IN_RS|IN_RT|OUT_RT),
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
	INSTR("rdhwr", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID,
};

static const MIPSInstruction tableRegImm[32] = // 000001 ..... xxxxx ................
{
	INSTR("bltz",  JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgez",  JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzl", JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LTZ),
	INSTR("bgezl", JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//8
	INSTR("tgei",  JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("tgeiu", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("tlti",  JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("tltiu", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("teqi",  JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID,
	INSTR("tnei",  JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID,
	//16
	INSTR("bltzal",  JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgezal",  JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzall", JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_LTZ), //L = likely
	INSTR("bgezall", JITFUNC(Comp_RelBranchRI), Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//24
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("synci", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
};

static const MIPSInstruction tableCop2[32] = // 010010 xxxxx ..... ................
{
	INSTR("mfc2", JITFUNC(Comp_Generic), Dis_Generic, 0, OUT_RT),
	INVALID,
	INSTR("cfc2", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("mfv", JITFUNC(Comp_Mftv), Dis_Mftv, Int_Mftv, IN_OTHER|IN_VFPU_CC|OUT_RT|IS_VFPU),
	INSTR("mtc2", JITFUNC(Comp_Generic), Dis_Generic, 0, IN_RT),
	INVALID,
	INSTR("ctc2", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("mtv", JITFUNC(Comp_Mftv), Dis_Mftv, Int_Mftv, IN_RT|OUT_VFPU_CC|OUT_OTHER|IS_VFPU|OUT_VFPU_PREFIX),
	//8
	ENCODING(Cop2BC2),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("??", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	//16
	INVALID_X_8,
	INVALID_X_8,
};

static const MIPSInstruction tableCop2BC2[4] = // 010010 01000 ...xx ................
{
	INSTR("bvf", JITFUNC(Comp_VBranch), Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvt", JITFUNC(Comp_VBranch), Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvfl", JITFUNC(Comp_VBranch), Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
	INSTR("bvtl", JITFUNC(Comp_VBranch), Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
};

static const MIPSInstruction tableCop0[32] = // 010000 xxxxx ..... ................
{
	INSTR("mfc0", JITFUNC(Comp_Generic), Dis_Generic, 0, OUT_RT),  // unused
	INVALID,
	INVALID,
	INVALID,
	INSTR("mtc0", JITFUNC(Comp_Generic), Dis_Generic, 0, IN_RT),  // unused
	INVALID,
	INVALID,
	INVALID,
	//8
	INVALID,
	INVALID,
	INSTR("rdpgpr", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("mfmc0", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),

	INVALID,
	INVALID,
	INSTR("wrpgpr", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID,
	//16
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
};

// we won't encounter these since we only do user mode emulation
static const MIPSInstruction tableCop0CO[64] = // 010000 1.... ..... ..... ..... xxxxxx
{
	INVALID,
	INSTR("tlbr", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("tlbwi", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID,
	INVALID,
	INVALID,
	INSTR("tlbwr", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID,
	//8
	INSTR("tlbp", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID_X_8,
	//24
	INSTR("eret", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INSTR("iack", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("deret", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	//32
	INSTR("wait", JITFUNC(Comp_Generic), Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

static const MIPSInstruction tableCop1[32] = // 010001 xxxxx ..... ..... ...........
{
	INSTR("mfc1", JITFUNC(Comp_mxc1), Dis_mxc1, Int_mxc1, IN_FS|OUT_RT|IS_FPU),
	INVALID,
	INSTR("cfc1", JITFUNC(Comp_mxc1), Dis_mxc1, Int_mxc1, IN_OTHER|IN_FPUFLAG|OUT_RT|IS_FPU),
	INVALID,
	INSTR("mtc1", JITFUNC(Comp_mxc1), Dis_mxc1, Int_mxc1, IN_RT|OUT_FS|IS_FPU),
	INVALID,
	INSTR("ctc1", JITFUNC(Comp_mxc1), Dis_mxc1, Int_mxc1, IN_RT|OUT_FPUFLAG|OUT_OTHER|IS_FPU),
	INVALID,
	//8
	ENCODING(Cop1BC), INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//16
	ENCODING(Cop1S), INVALID, INVALID, INVALID,
	ENCODING(Cop1W), INVALID, INVALID, INVALID,
	//24
	INVALID_X_8,
};

static const MIPSInstruction tableCop1BC[32] = // 010001 01000 xxxxx ................
{
	INSTR("bc1f",  JITFUNC(Comp_FPUBranch), Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUFALSE|IS_FPU),
	INSTR("bc1t",  JITFUNC(Comp_FPUBranch), Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUTRUE|IS_FPU),
	INSTR("bc1fl", JITFUNC(Comp_FPUBranch), Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUFALSE|IS_FPU),
	INSTR("bc1tl", JITFUNC(Comp_FPUBranch), Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUTRUE|IS_FPU),
	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

static const MIPSInstruction tableCop1S[64] = // 010001 10000 ..... ..... ..... xxxxxx
{
	INSTR("add.s",  JITFUNC(Comp_FPU3op), Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("sub.s",  JITFUNC(Comp_FPU3op), Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("mul.s",  JITFUNC(Comp_FPU3op), Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("div.s",  JITFUNC(Comp_FPU3op), Dis_FPU3op, Int_FPU3op, MIPSInfo(OUT_FD|IN_FS|IN_FT|IS_FPU, 29)),
	INSTR("sqrt.s", JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("abs.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("mov.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("neg.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	//8
	INVALID, INVALID, INVALID, INVALID,
	INSTR("round.w.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("trunc.w.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("ceil.w.s",   JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("floor.w.s",  JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	//36
	INSTR("cvt.w.s", JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INVALID,
	INSTR("dis.int", JITFUNC(Comp_Generic), Dis_Generic, Int_Interrupt, 0),
	INVALID,
	//40
	INVALID_X_8,
	//48 - 010001 10000 ..... ..... ..... 11xxxx
	INSTR("c.f",   JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, OUT_FPUFLAG|IS_FPU),
	INSTR("c.un",  JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.eq",  JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ueq", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.olt", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ult", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ole", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ule", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.sf",  JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngle",JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.seq", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngl", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.lt",  JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.nge", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.le",  JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngt", JITFUNC(Comp_FPUComp), Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
};

static const MIPSInstruction tableCop1W[64] = // 010001 10100 ..... ..... ..... xxxxxx
{
	INVALID_X_8,
	//8
	INVALID_X_8,
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INSTR("cvt.s.w", JITFUNC(Comp_FPU2op), Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
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

static const MIPSInstruction tableVFPU0[8] = // 011000 xxx ....... . ....... . .......
{
	INSTR("vadd", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_VecDo3, MIPSInfo(IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX, 2)),
	INSTR("vsub", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_VecDo3, MIPSInfo(IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX, 2)),
	// TODO: Disasm is wrong.
	INSTR("vsbn", JITFUNC(Comp_Generic), Dis_VectorSet3, Int_Vsbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID, INVALID,

	INSTR("vdiv", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

static const MIPSInstruction tableVFPU1[8] = // 011001 xxx ....... . ....... . .......
{
	INSTR("vmul", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdot", JITFUNC(Comp_VDot), Dis_VectorDot, Int_VDot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vscl", JITFUNC(Comp_VScl), Dis_VScl, Int_VScl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vhdp", JITFUNC(Comp_VHdp), Dis_VectorDot, Int_VHdp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrs", JITFUNC(Comp_VCrs), Dis_Vcrs, Int_Vcrs, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdet", JITFUNC(Comp_VDet), Dis_VectorDot, Int_Vdet, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
};

static const MIPSInstruction tableVFPU3[8] = // 011011 xxx ....... . ....... . .......
{
	INSTR("vcmp", JITFUNC(Comp_Vcmp), Dis_Vcmp, Int_Vcmp, IN_OTHER|OUT_VFPU_CC|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vmin", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmax", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vscmp", JITFUNC(Comp_Generic), Dis_VectorSet3, Int_Vscmp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsge", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_Vsge, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vslt", JITFUNC(Comp_VecDo3), Dis_VectorSet3, Int_Vslt, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

static const MIPSInstruction tableVFPU4Jump[32] = // 110100 xxxxx ..... . ....... . .......
{
	ENCODING(VFPU4),
	ENCODING(VFPU7),
	ENCODING(VFPU9),
	INSTR("vcst", JITFUNC(Comp_Vcst), Dis_Vcst, Int_Vcst, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID, INVALID,

	//8
	INVALID_X_8,

	//16
	INSTR("vf2in", JITFUNC(Comp_Vf2i), Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iz", JITFUNC(Comp_Vf2i), Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iu", JITFUNC(Comp_Vf2i), Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2id", JITFUNC(Comp_Vf2i), Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//20
	INSTR("vi2f", JITFUNC(Comp_Vi2f), Dis_Vf2i, Int_Vi2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcmov", JITFUNC(Comp_Vcmov), Dis_Vcmov, Int_Vcmov, IN_OTHER|IN_VFPU_CC|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	//24 - 110100 11 ........ . ....... . .......
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vwbn", JITFUNC(Comp_Generic), Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

static const MIPSInstruction tableVFPU7[32] = // 110100 00001 xxxxx . ....... . .......
{
	INSTR("vrnds", JITFUNC(Comp_Generic), Dis_Vrnds, Int_Vrnds, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndi", JITFUNC(Comp_Generic), Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf1", JITFUNC(Comp_Generic), Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf2", JITFUNC(Comp_Generic), Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID,
	//16
	INVALID,
	INVALID,
	INSTR("vf2h", JITFUNC(Comp_Generic), Dis_Vf2h, Int_Vf2h, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vh2f", JITFUNC(Comp_Vh2f), Dis_Vh2f, Int_Vh2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INSTR("vsbz", JITFUNC(Comp_Generic), Dis_Generic, Int_Vsbz, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vlgb", JITFUNC(Comp_Generic), Dis_Generic, Int_Vlgb, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INSTR("vuc2i", JITFUNC(Comp_Vx2i), Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // Seen in BraveStory, initialization  110100 00001110000 000 0001 0000 0000
	INSTR("vc2i",  JITFUNC(Comp_Vx2i), Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vus2i", JITFUNC(Comp_Vx2i), Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vs2i",  JITFUNC(Comp_Vx2i), Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vi2uc", JITFUNC(Comp_Vi2x), Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2c",  JITFUNC(Comp_Vi2x), Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2us", JITFUNC(Comp_Vi2x), Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2s",  JITFUNC(Comp_Vi2x), Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

// 110100 00000 10100 0000000000000000
// 110100 00000 10111 0000000000000000
static const MIPSInstruction tableVFPU4[32] = // 110100 00000 xxxxx . ....... . .......
{
	INSTR("vmov", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vabs", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vneg", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vidt", JITFUNC(Comp_VIdt), Dis_VectorSet1, Int_Vidt, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat0", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat1", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vzero", JITFUNC(Comp_VVectorInit), Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vone",  JITFUNC(Comp_VVectorInit), Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INVALID_X_8,
	//16
	INSTR("vrcp", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrsq", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsin", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcos", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vexp2", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vlog2", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsqrt", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vasin", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INSTR("vnrcp", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vnsin", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vrexp2", JITFUNC(Comp_VV2Op), Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID,
};

static const MIPSInstruction tableVFPU5[8] = // 110111 xxx ....... ................
{
	INSTR("vpfxs", JITFUNC(Comp_VPFX), Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxs", JITFUNC(Comp_VPFX), Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", JITFUNC(Comp_VPFX), Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", JITFUNC(Comp_VPFX), Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", JITFUNC(Comp_VPFX), Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", JITFUNC(Comp_VPFX), Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("viim.s", JITFUNC(Comp_Viim), Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfim.s", JITFUNC(Comp_Vfim), Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

static const MIPSInstruction tableVFPU6[32] = // 111100 xxxxx ..... . ....... . .......
{
	//0
	INSTR("vmmul", JITFUNC(Comp_Vmmul), Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", JITFUNC(Comp_Vmmul), Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", JITFUNC(Comp_Vmmul), Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", JITFUNC(Comp_Vmmul), Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm2", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INSTR("v(h)tfm3", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm4", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", JITFUNC(Comp_Vtfm), Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//16
	INSTR("vmscl", JITFUNC(Comp_Vmscl), Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", JITFUNC(Comp_Vmscl), Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", JITFUNC(Comp_Vmscl), Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", JITFUNC(Comp_Vmscl), Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vcrsp.t/vqmul.q", JITFUNC(Comp_VCrossQuat), Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", JITFUNC(Comp_VCrossQuat), Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", JITFUNC(Comp_VCrossQuat), Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", JITFUNC(Comp_VCrossQuat), Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//28
	ENCODING(VFPUMatrix1),
	INSTR("vrot", JITFUNC(Comp_VRot), Dis_VRot, Int_Vrot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
};

// TODO: Should this only be when bit 20 is 0?
static const MIPSInstruction tableVFPUMatrixSet1[16] = // 111100 11100 .xxxx . ....... . .......  (rm x is 16)
{
	INSTR("vmmov", JITFUNC(Comp_Vmmov), Dis_MatrixSet2, Int_Vmmov, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	INSTR("vmidt", JITFUNC(Comp_VMatrixInit), Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INSTR("vmzero", JITFUNC(Comp_VMatrixInit), Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmone",  JITFUNC(Comp_VMatrixInit), Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID_X_8,
};

static const MIPSInstruction tableVFPU9[32] = // 110100 00010 xxxxx . ....... . .......
{
	INSTR("vsrt1", JITFUNC(Comp_Generic), Dis_Vbfy, Int_Vsrt1, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsrt2", JITFUNC(Comp_Generic), Dis_Vbfy, Int_Vsrt2, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vbfy1", JITFUNC(Comp_Vbfy), Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vbfy2", JITFUNC(Comp_Vbfy), Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//4
	INSTR("vocp", JITFUNC(Comp_Vocp), Dis_Vbfy, Int_Vocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // one's complement
	INSTR("vsocp", JITFUNC(Comp_Generic), Dis_Vbfy, Int_Vsocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfad", JITFUNC(Comp_Vhoriz), Dis_Vfad, Int_Vfad, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vavg", JITFUNC(Comp_Vhoriz), Dis_Vfad, Int_Vavg, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INSTR("vsrt3", JITFUNC(Comp_Generic), Dis_Vbfy, Int_Vsrt3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsrt4", JITFUNC(Comp_Generic), Dis_Vbfy, Int_Vsrt4, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsgn", JITFUNC(Comp_Vsgn), Dis_Vbfy, Int_Vsgn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	//12
	INVALID,
	INVALID,
	INVALID,
	INVALID,

	//16
	INSTR("vmfvc", JITFUNC(Comp_Vmfvc), Dis_Vmfvc, Int_Vmfvc, IN_OTHER|IN_VFPU_CC|OUT_OTHER|IS_VFPU),
	INSTR("vmtvc", JITFUNC(Comp_Vmtvc), Dis_Vmtvc, Int_Vmtvc, IN_OTHER|OUT_VFPU_CC|OUT_OTHER|IS_VFPU|OUT_VFPU_PREFIX),
	INVALID,
	INVALID,

	//20
	INVALID, INVALID, INVALID, INVALID,
	//24
	INVALID,
	INSTR("vt4444", JITFUNC(Comp_ColorConv), Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5551", JITFUNC(Comp_ColorConv), Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5650", JITFUNC(Comp_ColorConv), Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	//28
	INVALID, INVALID, INVALID, INVALID,
};

static const MIPSInstruction tableALLEGREX0[32] =  // 011111 ..... ..... ..... xxxxx 100000 - or ending with 011000?
{
	INVALID,
	INVALID,
	INSTR("wsbh", JITFUNC(Comp_Allegrex2), Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INSTR("wsbw", JITFUNC(Comp_Allegrex2), Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INVALID, INVALID, INVALID, INVALID,
//8
	INVALID_X_8,
//16
	INSTR("seb", JITFUNC(Comp_Allegrex), Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//20
	INSTR("bitrev", JITFUNC(Comp_Allegrex), Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//24
	INSTR("seh", JITFUNC(Comp_Allegrex), Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//28
	INVALID,
	INVALID,
	INVALID,
	INVALID,
};

static const MIPSInstruction tableEMU[4] = {
	INSTR("RUNBLOCK", JITFUNC(Comp_RunBlock), Dis_Emuhack, Int_Emuhack, 0xFFFFFFFF),
	INSTR("RetKrnl", 0, Dis_Emuhack, Int_Emuhack, 0),
	INSTR("CallRepl", JITFUNC(Comp_ReplacementFunc), Dis_Emuhack, Int_Emuhack, 0),
	INVALID,
};

struct EncodingBitsInfo {
	EncodingBitsInfo(u8 shift_, u8 maskBits_) : shift(shift_) {
		mask = (1 << maskBits_) - 1;
	}
	u8 shift;
	u32 mask;
};

static const EncodingBitsInfo encodingBits[NumEncodings] = {
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

static const MIPSInstruction *mipsTables[NumEncodings] = {
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

// TODO : generate smart dispatcher functions from above tables
// instead of this slow method.
const MIPSInstruction *MIPSGetInstruction(MIPSOpcode op) {
	MipsEncoding encoding = Imme;
	const MIPSInstruction *instr = &tableImmediate[op.encoding >> 26];
	while (instr->altEncoding != Instruc) {
		if (instr->altEncoding == Inval) {
			//ERROR_LOG(Log::CPU, "Invalid instruction %08x in table %i, entry %i", op, (int)encoding, subop);
			return 0; //invalid instruction
		}
		encoding = instr->altEncoding;

		const MIPSInstruction *table = mipsTables[encoding];
		const u32 subop = (op.encoding >> encodingBits[encoding].shift) & encodingBits[encoding].mask;
		instr = &table[subop];
	}
	//alright, we have a valid MIPS instruction!
	return instr;
}

void MIPSCompileOp(MIPSOpcode op, MIPSComp::MIPSFrontendInterface *jit) {
	if (op == 0)
		return;
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	const MIPSInfo info = MIPSGetInfo(op);
	if (instr) {
		if (instr->compile) {
			(jit->*(instr->compile))(op);
		} else {
			ERROR_LOG_REPORT(Log::CPU,"MIPSCompileOp %08x failed",op.encoding);
		}
		if (info & OUT_EAT_PREFIX)
			jit->EatPrefix();
	} else {
		ERROR_LOG_REPORT(Log::CPU, "MIPSCompileOp: Invalid instruction %08x", op.encoding);
	}
}

void MIPSDisAsm(MIPSOpcode op, u32 pc, char *out, size_t outSize, bool tabsToSpaces) {
	if (op == 0) {
		truncate_cpy(out, outSize, "nop");
	} else {
		const MIPSInstruction *instr = MIPSGetInstruction(op);
		if (instr && instr->disasm) {
			instr->disasm(op, pc, out, outSize);
			if (tabsToSpaces) {
				while (*out) {
					if (*out == '\t')
						*out = ' ';
					out++;
				}
			}
		} else {
			truncate_cpy(out, outSize, "no instruction :(");
		}
	}
}

static inline void Interpret(const MIPSInstruction *instr, MIPSOpcode op) {
	if (instr && instr->interpret) {
		instr->interpret(op);
	} else {
		ERROR_LOG_REPORT(Log::CPU, "Unknown instruction %08x at %08x", op.encoding, currentMIPS->pc);
		// Try to disassemble it
		char disasm[256];
		MIPSDisAsm(op, currentMIPS->pc, disasm, sizeof(disasm));
		_dbg_assert_msg_(0, "%s", disasm);
		currentMIPS->pc += 4;
	}
}

inline int GetInstructionCycleEstimate(const MIPSInstruction *instr) {
	if (instr)
		return instr->flags.cycles;
	return 1;
}

void MIPSInterpret(MIPSOpcode op) {
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	Interpret(instr, op);
}

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define R(i)   (curMips->r[i])

static inline void RunUntilFast() {
	MIPSState *curMips = currentMIPS;
	// NEVER stop in a delay slot!
	while (curMips->downcount >= 0 && coreState == CORE_RUNNING) {
		do {
			// Replacements and similar are processed here, intentionally.
			MIPSOpcode op = MIPSOpcode(Memory::Read_U32(curMips->pc));

			bool wasInDelaySlot = curMips->inDelaySlot;
			const MIPSInstruction *instr = MIPSGetInstruction(op);
			Interpret(instr, op);
			curMips->downcount -= GetInstructionCycleEstimate(instr);

			// The reason we have to check this is the delay slot hack in Int_Syscall.
			if (curMips->inDelaySlot && wasInDelaySlot) {
				curMips->pc = curMips->nextPC;
				curMips->inDelaySlot = false;
			}
		} while (curMips->inDelaySlot);
	}
}

static void RunUntilWithChecks(u64 globalTicks) {
	MIPSState *curMips = currentMIPS;
	// NEVER stop in a delay slot!
	bool hasBPs = CBreakPoints::HasBreakPoints();
	bool hasMCs = CBreakPoints::HasMemChecks();
	while (curMips->downcount >= 0 && coreState == CORE_RUNNING) {
		do {
			// Replacements and similar are processed here, intentionally.
			MIPSOpcode op = MIPSOpcode(Memory::Read_U32(curMips->pc));
			const MIPSInstruction *instr = MIPSGetInstruction(op);

			// Check for breakpoint
			if (hasBPs && CBreakPoints::IsAddressBreakPoint(curMips->pc) && CBreakPoints::CheckSkipFirst() != curMips->pc) {
				auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
				if (!cond || cond->Evaluate()) {
					Core_EnableStepping(true, "cpu.breakpoint", curMips->pc);
					if (CBreakPoints::IsTempBreakPoint(curMips->pc))
						CBreakPoints::RemoveBreakPoint(curMips->pc);
					break;
				}
			}
			if (hasMCs && (instr->flags & (IN_MEM | OUT_MEM)) != 0 && CBreakPoints::CheckSkipFirst() != curMips->pc && instr->interpret != &Int_Syscall) {
				// This is common for all IN_MEM/OUT_MEM funcs.
				int offset = (instr->flags & IS_VFPU) != 0 ? SignExtend16ToS32(op & 0xFFFC) : SignExtend16ToS32(op);
				u32 addr = (R(_RS) + offset) & 0xFFFFFFFC;
				int sz = MIPSGetMemoryAccessSize(op);

				if ((instr->flags & IN_MEM) != 0)
					CBreakPoints::ExecMemCheck(addr, false, sz, curMips->pc, "interpret");
				if ((instr->flags & OUT_MEM) != 0)
					CBreakPoints::ExecMemCheck(addr, true, sz, curMips->pc, "interpret");

				// If it tripped, bail without running.
				if (coreState == CORE_STEPPING)
					break;
			}

			bool wasInDelaySlot = curMips->inDelaySlot;
			Interpret(instr, op);
			curMips->downcount -= GetInstructionCycleEstimate(instr);

			// The reason we have to check this is the delay slot hack in Int_Syscall.
			if (curMips->inDelaySlot && wasInDelaySlot) {
				curMips->pc = curMips->nextPC;
				curMips->inDelaySlot = false;
			}
		} while (curMips->inDelaySlot);

		if (CoreTiming::GetTicks() > globalTicks)
			return;
	}
}

int MIPSInterpret_RunUntil(u64 globalTicks) {
	MIPSState *curMips = currentMIPS;
	while (coreState == CORE_RUNNING) {
		CoreTiming::Advance();

		uint64_t ticksLeft = globalTicks - CoreTiming::GetTicks();
		if (CBreakPoints::HasBreakPoints() || CBreakPoints::HasMemChecks() || ticksLeft <= curMips->downcount)
			RunUntilWithChecks(globalTicks);
		else
			RunUntilFast();

		if (CoreTiming::GetTicks() > globalTicks) {
			// DEBUG_LOG(Log::CPU, "Hit the max ticks, bailing 1 : %llu, %llu", globalTicks, CoreTiming::GetTicks());
			return 1;
		}
	}

	return 1;
}

const char *MIPSGetName(MIPSOpcode op)
{
	static const char * const noname = "unk";
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
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	return GetInstructionCycleEstimate(instr);
}

int MIPSGetMemoryAccessSize(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if ((info & (IN_MEM | OUT_MEM)) == 0) {
		return 0;
	}

	switch (info & MEMTYPE_MASK) {
	case MEMTYPE_BYTE:
		return 1;
	case MEMTYPE_HWORD:
		return 2;
	case MEMTYPE_WORD:
	case MEMTYPE_FLOAT:
		return 4;
	case MEMTYPE_VQUAD:
		return 16;
	}

	return 0;
}

std::string MIPSDisasmAt(u32 compilerPC) {
	char temp[512];
	MIPSDisAsm(Memory::Read_Instruction(compilerPC), 0, temp, sizeof(temp));
	return temp;
}
