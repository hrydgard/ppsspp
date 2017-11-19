#include "stdafx.h"
#include "MipsOpcodes.h"

const tMipsOpcode MipsOpcodes[] = {
//     31---------26---------------------------------------------------0
//     |  opcode   |                                                   |
//     ------6----------------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 | *1    | *2    | J     | JAL   | BEQ   | BNE   | BLEZ  | BGTZ  | 00..07
// 001 | ADDI  | ADDIU | SLTI  | SLTIU | ANDI  | ORI   | XORI  | LUI   | 08..0F
// 010 | *3    | *4    | *5    | ---   | BEQL  | BNEL  | BLEZL | BGTZL | 10..17
// 011 | DADDI | DADDIU| LDL   | LDR   | ---   | ---   | LQ    | SQ    | 18..1F
// 100 | LB    | LH    | LWL   | LW    | LBU   | LHU   | LWR   | LWU   | 20..27
// 101 | SB    | SH    | SWL   | SW    | SDL   | SDR   | SWR   | CACHE | 28..2F
// 110 | LL    | LWC1  | LV.S  | ---   | LLD   | ULV.Q | LV.Q  | LD    | 30..37
// 111 | SC    | SWC1  | SV.S  | ---   | SCD   | USV.Q | SV.Q  | SD    | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
//		*1 = SPECIAL	*2 = REGIMM		*3 = COP0		*4 = COP1		*5 = COP2
	{ "j",		"i26",				MIPS_OP(0x02), 			MA_MIPS1,	MO_IPCA|MO_DELAY|MO_NODELAYSLOT },
	{ "jal",	"i26",				MIPS_OP(0x03),			MA_MIPS1,	MO_IPCA|MO_DELAY|MO_NODELAYSLOT },
	{ "beq",	"s,t,i16",			MIPS_OP(0x04),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "beqz",	"s,i16",			MIPS_OP(0x04),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "b",		"i16",				MIPS_OP(0x04), 			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bne",	"s,t,i16",			MIPS_OP(0x05),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bnez",	"s,i16",			MIPS_OP(0x05),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "blez",	"s,i16",			MIPS_OP(0x06),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgtz",	"s,i16",			MIPS_OP(0x07),			MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "addi",	"t,s,i16",			MIPS_OP(0x08),			MA_MIPS1,	MO_IGNORERTD },
	{ "addi",	"s,i16",			MIPS_OP(0x08),			MA_MIPS1,	MO_RST },
	{ "subi",	"t,s,i16",			MIPS_OP(0x08),			MA_MIPS1,	MO_IGNORERTD|MO_NEGIMM },
	{ "subi",	"s,i16",			MIPS_OP(0x08),			MA_MIPS1,	MO_RST|MO_NEGIMM },
	{ "addiu",	"t,s,i16",			MIPS_OP(0x09),			MA_MIPS1,	MO_IGNORERTD },
	{ "addiu",	"s,i16",			MIPS_OP(0x09),			MA_MIPS1,	MO_RST },
	{ "subiu",	"t,s,i16",			MIPS_OP(0x09),			MA_MIPS1,	MO_IGNORERTD|MO_NEGIMM },
	{ "subiu",	"s,i16",			MIPS_OP(0x09),			MA_MIPS1,	MO_RST|MO_NEGIMM },
	{ "slti",	"t,s,i16",			MIPS_OP(0x0A),			MA_MIPS1,	MO_IGNORERTD },
	{ "slti",	"s,i16",			MIPS_OP(0x0A),			MA_MIPS1,	MO_RST },
	{ "sltiu",	"t,s,i16",			MIPS_OP(0x0B),			MA_MIPS1,	MO_IGNORERTD },
	{ "sltiu",	"s,i16",			MIPS_OP(0x0B),			MA_MIPS1,	MO_RST },
	{ "andi",	"t,s,i16",			MIPS_OP(0x0C),			MA_MIPS1,	MO_IGNORERTD },
	{ "andi",	"s,i16",			MIPS_OP(0x0C),			MA_MIPS1,	MO_RST },
	{ "ori",	"t,s,i16",			MIPS_OP(0x0D),			MA_MIPS1,	MO_IGNORERTD },
	{ "ori",	"s,i16",			MIPS_OP(0x0D),			MA_MIPS1,	MO_RST },
	{ "xori",	"t,s,i16",			MIPS_OP(0x0E),			MA_MIPS1,	MO_IGNORERTD },
	{ "xori",	"s,i16",			MIPS_OP(0x0E),			MA_MIPS1,	MO_RST },
	{ "lui",	"t,i16",			MIPS_OP(0x0F),			MA_MIPS1,	MO_IGNORERTD },
	{ "beql",	"s,t,i16",			MIPS_OP(0x14),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "beqzl",	"s,i16",			MIPS_OP(0x14),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bnel",	"s,t,i16",			MIPS_OP(0x15),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bnezl",	"s,i16",			MIPS_OP(0x15),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "blezl",	"s,i16",			MIPS_OP(0x16),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgtzl",	"s,i16",			MIPS_OP(0x17),			MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "daddi",	"t,s,i16",			MIPS_OP(0x18),			MA_MIPS3,	MO_64BIT },
	{ "daddi",	"s,i16",			MIPS_OP(0x18),			MA_MIPS3,	MO_64BIT|MO_RST },
	{ "dsubi",	"t,s,i16",			MIPS_OP(0x18),			MA_MIPS3,	MO_64BIT|MO_NEGIMM },
	{ "dsubi",	"s,i16",			MIPS_OP(0x18),			MA_MIPS3,	MO_64BIT|MO_RST|MO_NEGIMM },
	{ "daddiu",	"t,s,i16",			MIPS_OP(0x19),			MA_MIPS3,	MO_64BIT },
	{ "daddiu",	"s,i16",			MIPS_OP(0x19),			MA_MIPS3,	MO_64BIT|MO_RST },
	{ "dsubiu",	"t,s,i16",			MIPS_OP(0x19),			MA_MIPS3,	MO_64BIT|MO_NEGIMM },
	{ "dsubiu",	"s,i16",			MIPS_OP(0x19),			MA_MIPS3,	MO_64BIT|MO_RST|MO_NEGIMM },
	{ "ldl",	"t,i16(s)",			MIPS_OP(0x1A),			MA_MIPS3,	MO_64BIT|MO_DELAYRT|MO_IGNORERTD },
	{ "ldl",	"t,(s)",			MIPS_OP(0x1A),			MA_MIPS3,	MO_64BIT|MO_DELAYRT|MO_IGNORERTD },
	{ "ldr",	"t,i16(s)",			MIPS_OP(0x1B),			MA_MIPS3,	MO_64BIT|MO_DELAYRT|MO_IGNORERTD },
	{ "ldr",	"t,(s)",			MIPS_OP(0x1B),			MA_MIPS3,	MO_64BIT|MO_DELAYRT|MO_IGNORERTD },
	{ "lq",		"t,i16(s)",			MIPS_OP(0x1E),			MA_PS2,		MO_DELAYRT|MO_IGNORERTD },
	{ "lq",		"t,(s)",			MIPS_OP(0x1E),			MA_PS2,		MO_DELAYRT|MO_IGNORERTD },
	{ "sq",		"t,i16(s)",			MIPS_OP(0x1F),			MA_PS2,		MO_DELAYRT|MO_IGNORERTD },
	{ "sq",		"t,(s)",			MIPS_OP(0x1F),			MA_PS2,		MO_DELAYRT|MO_IGNORERTD },
	{ "lb",		"t,i16(s)",			MIPS_OP(0x20),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lb",		"t,(s)",			MIPS_OP(0x20),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lh",		"t,i16(s)",			MIPS_OP(0x21),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lh",		"t,(s)",			MIPS_OP(0x21),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwl",	"t,i16(s)",			MIPS_OP(0x22),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwl",	"t,(s)",			MIPS_OP(0x22),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lw",		"t,i16(s)",			MIPS_OP(0x23),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lw",		"t,(s)",			MIPS_OP(0x23),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lbu",	"t,i16(s)",			MIPS_OP(0x24),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lbu",	"t,(s)",			MIPS_OP(0x24),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lhu",	"t,i16(s)",			MIPS_OP(0x25),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lhu",	"t,(s)",			MIPS_OP(0x25),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwr",	"t,i16(s)",			MIPS_OP(0x26),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwr",	"t,(s)",			MIPS_OP(0x26),			MA_MIPS1,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwu",	"t,i16(s)",			MIPS_OP(0x27),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "lwu",	"t,(s)",			MIPS_OP(0x27),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "sb",		"t,i16(s)",			MIPS_OP(0x28),			MA_MIPS1,	0 },
	{ "sb",		"t,(s)",			MIPS_OP(0x28),			MA_MIPS1,	0 },
	{ "sh",		"t,i16(s)",			MIPS_OP(0x29),			MA_MIPS1,	0 },
	{ "sh",		"t,(s)",			MIPS_OP(0x29),			MA_MIPS1,	0 },
	{ "swl",	"t,i16(s)",			MIPS_OP(0x2A),			MA_MIPS1,	0 },
	{ "swl",	"t,(s)",			MIPS_OP(0x2A),			MA_MIPS1,	0 },
	{ "sw",		"t,i16(s)",			MIPS_OP(0x2B),			MA_MIPS1,	0 },
	{ "sw",		"t,(s)",			MIPS_OP(0x2B),			MA_MIPS1,	0 },
	{ "sdl",	"t,i16(s)",			MIPS_OP(0x2C),			MA_MIPS3,	MO_64BIT },
	{ "sdl",	"t,(s)",			MIPS_OP(0x2C),			MA_MIPS3,	MO_64BIT },
	{ "sdr",	"t,i16(s)",			MIPS_OP(0x2D),			MA_MIPS3,	MO_64BIT|MO_IGNORERTD },
	{ "sdr",	"t,(s)",			MIPS_OP(0x2D),			MA_MIPS3,	MO_64BIT|MO_IGNORERTD },
	{ "swr",	"t,i16(s)",			MIPS_OP(0x2E),			MA_MIPS1,	0 },
	{ "swr",	"t,(s)",			MIPS_OP(0x2E),			MA_MIPS1,	0 },
	{ "cache",	"jc,i16(s)",		MIPS_OP(0x2F),			MA_MIPS2,	0 },
	{ "cache",	"jc,(s)",			MIPS_OP(0x2F),			MA_MIPS2,	0 },
	{ "ll",		"t,i16(s)",			MIPS_OP(0x30),			MA_MIPS2,	MO_DELAYRT|MO_IGNORERTD },
	{ "ll",		"t,(s)",			MIPS_OP(0x30),			MA_MIPS2,	MO_DELAYRT|MO_IGNORERTD },
	{ "lwc1",	"T,i16(s)",			MIPS_OP(0x31),			MA_MIPS1,	0 },
	{ "lwc1",	"T,(s)",			MIPS_OP(0x31),			MA_MIPS1,	0 },
	{ "l.s",	"T,i16(s)",			MIPS_OP(0x31),			MA_MIPS1,	0 },
	{ "l.s",	"T,(s)",			MIPS_OP(0x31),			MA_MIPS1,	0 },
	{ "lv.s",	"vt,i16(s)",		MIPS_OP(0x32),			MA_PSP,		MO_VFPU_SINGLE|MO_VFPU_MIXED|MO_IMMALIGNED },
	{ "lv.s",	"vt,(s)",			MIPS_OP(0x32),			MA_PSP,		MO_VFPU_SINGLE|MO_VFPU_MIXED },
	{ "lld",	"t,i16(s)",			MIPS_OP(0x34),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "lld",	"t,(s)",			MIPS_OP(0x34),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "ldc1",	"T,i16(s)",			MIPS_OP(0x35),			MA_MIPS2,	MO_DFPU },
	{ "ldc1",	"T,(s)",			MIPS_OP(0x35),			MA_MIPS2,	MO_DFPU },
	{ "l.d",	"T,i16(s)",			MIPS_OP(0x35),			MA_MIPS2,	MO_DFPU },
	{ "l.d",	"T,(s)",			MIPS_OP(0x35),			MA_MIPS2,	MO_DFPU },
	{ "ulv.q",	"vt,i16(s)",		MIPS_OP(0x35),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_IMMALIGNED },
	{ "ulv.q",	"vt,(s)",			MIPS_OP(0x35),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED },
	{ "lvl.q",	"vt,i16(s)",		MIPS_OP(0x35),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "lvl.q",	"vt,(s)",			MIPS_OP(0x35),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "lvr.q",	"vt,i16(s)",		MIPS_OP(0x35)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "lvr.q",	"vt,(s)",			MIPS_OP(0x35)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "lv.q",	"vt,i16(s)",		MIPS_OP(0x36),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "lv.q",	"vt,(s)",			MIPS_OP(0x36),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "lqc2",	"Vt,i16(s)",		MIPS_OP(0x36),			MA_PS2,		MO_DELAYRT },
	{ "ld",		"t,i16(s)",			MIPS_OP(0x37),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "ld",		"t,(s)",			MIPS_OP(0x37),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "sc",		"t,i16(s)",			MIPS_OP(0x38),			MA_MIPS2,	0 },
	{ "sc",		"t,(s)",			MIPS_OP(0x38),			MA_MIPS2,	0 },
	{ "swc1",	"T,i16(s)",			MIPS_OP(0x39),			MA_MIPS1,	0 },
	{ "swc1",	"T,(s)",			MIPS_OP(0x39),			MA_MIPS1,	0 },
	{ "s.s",	"T,i16(s)",			MIPS_OP(0x39),			MA_MIPS1,	0 },
	{ "s.s",	"T,(s)",			MIPS_OP(0x39),			MA_MIPS1,	0 },
	{ "sv.s",	"vt,i16(s)",		MIPS_OP(0x3A),			MA_PSP,		MO_VFPU_SINGLE|MO_VFPU_MIXED|MO_IMMALIGNED },
	{ "sv.s",	"vt,(s)",			MIPS_OP(0x3A),			MA_PSP,		MO_VFPU_SINGLE|MO_VFPU_MIXED },
	{ "scd",	"t,i16(s)",			MIPS_OP(0x3C),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "scd",	"t,(s)",			MIPS_OP(0x3C),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "sdc1",	"T,i16(s)",			MIPS_OP(0x3D),			MA_MIPS2,	MO_DFPU },
	{ "sdc1",	"T,(s)",			MIPS_OP(0x3D),			MA_MIPS2,	MO_DFPU },
	{ "s.d",	"T,i16(s)",			MIPS_OP(0x3D),			MA_MIPS2,	MO_DFPU },
	{ "s.d",	"T,(s)",			MIPS_OP(0x3D),			MA_MIPS2,	MO_DFPU },
	{ "usv.q",	"vt,i16(s)",		MIPS_OP(0x3D),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_IMMALIGNED },
	{ "usv.q",	"vt,(s)",			MIPS_OP(0x3D),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED },
	{ "svl.q",	"vt,i16(s)",		MIPS_OP(0x3D),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "svl.q",	"vt,(s)",			MIPS_OP(0x3D),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "svr.q",	"vt,i16(s)",		MIPS_OP(0x3D)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "svr.q",	"vt,(s)",			MIPS_OP(0x3D)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "sv.q",	"vt,i16(s),w",		MIPS_OP(0x3E)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "sv.q",	"vt,(s),w",			MIPS_OP(0x3E)|0x02,		MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "sv.q",	"vt,i16(s)",		MIPS_OP(0x3E),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT|MO_IMMALIGNED },
	{ "sv.q",	"vt,(s)",			MIPS_OP(0x3E),			MA_PSP,		MO_VFPU_QUAD|MO_VFPU_MIXED|MO_VFPU_6BIT },
	{ "sqc2",	"Vt,i16(s)",		MIPS_OP(0x3E),			MA_PS2,		MO_DELAYRT },
	{ "sd",		"t,i16(s)",			MIPS_OP(0x3F),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },
	{ "sd",		"t,(s)",			MIPS_OP(0x3F),			MA_MIPS3,	MO_64BIT|MO_DELAYRT },

//     31---------26------------------------------------------5--------0
//     |=   SPECIAL|                                         | function|
//     ------6----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 | SLL   | ---   | SRL*1 | SRA   | SLLV  |  ---  | SRLV*2| SRAV  | 00..07
// 001 | JR    | JALR  | MOVZ  | MOVN  |SYSCALL| BREAK |  ---  | SYNC  | 08..0F
// 010 | MFHI  | MTHI  | MFLO  | MTLO  | DSLLV |  ---  |   *3  |  *4   | 10..17
// 011 | MULT  | MULTU | DIV   | DIVU  | MADD  | MADDU | ----  | ----- | 18..1F
// 100 | ADD   | ADDU  | SUB   | SUBU  | AND   | OR    | XOR   | NOR   | 20..27
// 101 | mfsa  | mtsa  | SLT   | SLTU  |  *5   |  *6   |  *7   |  *8   | 28..2F
// 110 | TGE   | TGEU  | TLT   | TLTU  | TEQ   |  ---  | TNE   |  ---  | 30..37
// 111 | dsll  |  ---  | dsrl  | dsra  |dsll32 |  ---  |dsrl32 |dsra32 | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
// *1:	rotr when rs = 1 (PSP only)		*2:	rotrv when sa = 1 (PSP only)
// *3:	dsrlv on PS2, clz on PSP		*4:	dsrav on PS2, clo on PSP
// *5:	dadd on PS2, max on PSP			*6:	daddu on PS2, min on PSP
// *7:	dsub on PS2, msub on PSP		*8:	dsubu on PS2, msubu on PSP
	{ "sll",	"d,t,i5",	MIPS_SPECIAL(0x00),				MA_MIPS1,	0 },
	{ "sll",	"d,i5",		MIPS_SPECIAL(0x00),				MA_MIPS1,	MO_RDT },
	{ "nop",	"",			MIPS_SPECIAL(0x00),				MA_MIPS1,	0 },
	{ "srl",	"d,t,i5",	MIPS_SPECIAL(0x02),				MA_MIPS1,	0 },
	{ "srl",	"d,i5",		MIPS_SPECIAL(0x02),				MA_MIPS1,	MO_RDT },
	{ "rotr",	"d,t,i5",	MIPS_SPECIAL(0x02)|MIPS_RS(1),	MA_PSP,		0 },
	{ "rotr",	"d,i5",		MIPS_SPECIAL(0x02)|MIPS_RS(1),	MA_PSP,		MO_RDT },
	{ "sra",	"d,t,i5",	MIPS_SPECIAL(0x03),				MA_MIPS1,	0 },
	{ "sra",	"d,i5",		MIPS_SPECIAL(0x03),				MA_MIPS1,	MO_RDT },
	{ "sllv",	"d,t,s",	MIPS_SPECIAL(0x04),				MA_MIPS1,	0 },
	{ "sllv",	"d,s",		MIPS_SPECIAL(0x04),				MA_MIPS1,	MO_RDT },
	{ "srlv",	"d,t,s",	MIPS_SPECIAL(0x06),				MA_MIPS1,	0 },
	{ "srlv",	"d,s",		MIPS_SPECIAL(0x06),				MA_MIPS1,	MO_RDT },
	{ "rotrv",	"d,t,s",	MIPS_SPECIAL(0x06)|MIPS_SA(1),	MA_PSP,		0 },
	{ "rotrv",	"d,s",		MIPS_SPECIAL(0x06)|MIPS_SA(1),	MA_PSP,		MO_RDT },
	{ "srav",	"d,t,s",	MIPS_SPECIAL(0x07),				MA_MIPS1,	0 },
	{ "srav",	"d,s",		MIPS_SPECIAL(0x07),				MA_MIPS1,	MO_RDT },
	{ "jr",		"s",		MIPS_SPECIAL(0x08),				MA_MIPS1,	MO_DELAY|MO_NODELAYSLOT },
	{ "jalr",	"s,d",		MIPS_SPECIAL(0x09),				MA_MIPS1,	MO_DELAY|MO_NODELAYSLOT },
	{ "jalr",	"s",		MIPS_SPECIAL(0x09)|MIPS_RD(31),	MA_MIPS1,	MO_DELAY|MO_NODELAYSLOT },
	{ "movz",	"d,s,t",	MIPS_SPECIAL(0x0A),				MA_MIPS4|MA_PS2|MA_PSP,	0 },
	{ "movn",	"d,s,t",	MIPS_SPECIAL(0x0B),				MA_MIPS4|MA_PS2|MA_PSP,	0 },
	{ "syscall","i20",		MIPS_SPECIAL(0x0C),				MA_MIPS1,	MO_NODELAYSLOT },
	{ "syscall","",			MIPS_SPECIAL(0x0C),				MA_MIPS1,	MO_NODELAYSLOT },
	{ "break",	"i20",		MIPS_SPECIAL(0x0D),				MA_MIPS1,	MO_NODELAYSLOT },
	{ "break",	"",			MIPS_SPECIAL(0x0D),				MA_MIPS1,	MO_NODELAYSLOT },
	{ "sync",	"",			MIPS_SPECIAL(0x0F),				MA_MIPS2,	0 },
	{ "mfhi",	"d",		MIPS_SPECIAL(0x10),				MA_MIPS1,	0 },
	{ "mthi",	"s",		MIPS_SPECIAL(0x11),				MA_MIPS1,	0 },
	{ "mflo",	"d",		MIPS_SPECIAL(0x12),				MA_MIPS1,	0 },
	{ "mtlo",	"s",		MIPS_SPECIAL(0x13),				MA_MIPS1,	0 },
	{ "dsllv",	"d,t,s",	MIPS_SPECIAL(0x14),				MA_MIPS3,	MO_64BIT },
	{ "dsllv",	"d,s",		MIPS_SPECIAL(0x14),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsrlv",	"d,t,s",	MIPS_SPECIAL(0x16),				MA_MIPS3,	MO_64BIT },
	{ "dsrlv",	"d,s",		MIPS_SPECIAL(0x16),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "clz",	"d,s",		MIPS_SPECIAL(0x16),				MA_PSP,		0 },
	{ "dsrav",	"d,t,s",	MIPS_SPECIAL(0x17),				MA_MIPS3,	MO_64BIT },
	{ "dsrav",	"d,s",		MIPS_SPECIAL(0x17),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "clo",	"d,s",		MIPS_SPECIAL(0x17),				MA_PSP,		0 },
	{ "mult",	"d,s,t",	MIPS_SPECIAL(0x18),				MA_PS2,		0 },
	{ "multu",	"d,s,t",	MIPS_SPECIAL(0x19),				MA_PS2,		0 },
	{ "mult",	"s,t",		MIPS_SPECIAL(0x18),				MA_MIPS1|MA_EXRSP,	0 },
	{ "mult",	"r\x0,s,t",	MIPS_SPECIAL(0x18),				MA_MIPS1|MA_EXRSP,	0 },
	{ "multu",	"s,t",		MIPS_SPECIAL(0x19),				MA_MIPS1|MA_EXRSP,	0 },
	{ "multu",	"r\x0,s,t",	MIPS_SPECIAL(0x19),				MA_MIPS1|MA_EXRSP,	0 },
	{ "div",	"s,t",		MIPS_SPECIAL(0x1A),				MA_MIPS1|MA_EXRSP,	0 },
	{ "div",	"r\x0,s,t",	MIPS_SPECIAL(0x1A),				MA_MIPS1|MA_EXRSP,	0 },
	{ "divu",	"s,t",		MIPS_SPECIAL(0x1B),				MA_MIPS1|MA_EXRSP,	0 },
	{ "divu",	"r\x0,s,t",	MIPS_SPECIAL(0x1B),				MA_MIPS1|MA_EXRSP,	0 },
	{ "dmult",	"s,t",		MIPS_SPECIAL(0x1C),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "dmult",	"r\x0,s,t",	MIPS_SPECIAL(0x1C),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "madd",	"s,t",		MIPS_SPECIAL(0x1C),				MA_PSP,		0 },
	{ "dmultu",	"s,t",		MIPS_SPECIAL(0x1D),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "dmultu",	"r\x0,s,t",	MIPS_SPECIAL(0x1D),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "maddu",	"s,t",		MIPS_SPECIAL(0x1D),				MA_PSP,		0 },
	{ "ddiv",	"s,t",		MIPS_SPECIAL(0x1E),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "ddiv",	"r\x0,s,t",	MIPS_SPECIAL(0x1E),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "ddivu",	"s,t",		MIPS_SPECIAL(0x1F),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "ddivu",	"r\x0,s,t",	MIPS_SPECIAL(0x1F),				MA_MIPS3|MA_EXPS2,	MO_64BIT },
	{ "add",	"d,s,t",	MIPS_SPECIAL(0x20),				MA_MIPS1,	0 },
	{ "add",	"s,t",		MIPS_SPECIAL(0x20),				MA_MIPS1,	MO_RSD },
	{ "addu",	"d,s,t",	MIPS_SPECIAL(0x21),				MA_MIPS1,	0 },
	{ "addu",	"s,t",		MIPS_SPECIAL(0x21),				MA_MIPS1,	MO_RSD },
	{ "move",	"d,s",		MIPS_SPECIAL(0x21),				MA_MIPS1,	0 },
	{ "clear",	"d",		MIPS_SPECIAL(0x21),				MA_MIPS1,	0 },
	{ "sub",	"d,s,t",	MIPS_SPECIAL(0x22),				MA_MIPS1,	0 },
	{ "sub",	"s,t",		MIPS_SPECIAL(0x22),				MA_MIPS1,	MO_RSD },
	{ "neg",	"d,t",		MIPS_SPECIAL(0x22),				MA_MIPS1,	0 },
	{ "subu",	"d,s,t",	MIPS_SPECIAL(0x23),				MA_MIPS1,	0 },
	{ "subu",	"s,t",		MIPS_SPECIAL(0x23),				MA_MIPS1,	MO_RSD },
	{ "negu",	"d,t",		MIPS_SPECIAL(0x23),				MA_MIPS1,	0 },
	{ "and",	"d,s,t",	MIPS_SPECIAL(0x24),				MA_MIPS1,	0 },
	{ "and",	"s,t",		MIPS_SPECIAL(0x24),				MA_MIPS1,	MO_RSD },
	{ "or",		"d,s,t",	MIPS_SPECIAL(0x25),				MA_MIPS1,	0 },
	{ "or",		"s,t",		MIPS_SPECIAL(0x25),				MA_MIPS1,	MO_RSD },
	{ "xor",	"d,s,t",	MIPS_SPECIAL(0x26), 			MA_MIPS1,	0 },
	{ "eor",	"d,s,t",	MIPS_SPECIAL(0x26),				MA_MIPS1,	0 },
	{ "xor",	"s,t",		MIPS_SPECIAL(0x26), 			MA_MIPS1,	MO_RSD },
	{ "eor",	"s,t",		MIPS_SPECIAL(0x26), 			MA_MIPS1,	MO_RSD },
	{ "nor",	"d,s,t",	MIPS_SPECIAL(0x27),				MA_MIPS1,	0 },
	{ "nor",	"s,t",		MIPS_SPECIAL(0x27),				MA_MIPS1,	MO_RSD },
	{ "not",	"d,s",		MIPS_SPECIAL(0x27),				MA_MIPS1,	0 },
	{ "mfsa",	"d",		MIPS_SPECIAL(0x28),				MA_PS2,		0 },
	{ "mtsa",	"s",		MIPS_SPECIAL(0x29),				MA_PS2,		0 },
	{ "slt",	"d,s,t",	MIPS_SPECIAL(0x2A),				MA_MIPS1,	0 },
	{ "slt",	"s,t",		MIPS_SPECIAL(0x2A),				MA_MIPS1,	MO_RSD},
	{ "sgt",	"d,t,s",	MIPS_SPECIAL(0x2A),				MA_MIPS1,	0 },
	{ "sgt",	"d,s",		MIPS_SPECIAL(0x2A),				MA_MIPS1,	MO_RDT},
	{ "sltu",	"d,s,t",	MIPS_SPECIAL(0x2B),				MA_MIPS1,	0 },
	{ "sltu",	"s,t",		MIPS_SPECIAL(0x2B),				MA_MIPS1,	MO_RSD },
	{ "sgtu",	"d,t,s",	MIPS_SPECIAL(0x2B),				MA_MIPS1,	0 },
	{ "sgtu",	"d,s",		MIPS_SPECIAL(0x2B),				MA_MIPS1,	MO_RDT},
	{ "dadd",	"d,s,t",	MIPS_SPECIAL(0x2C),				MA_MIPS3,	MO_64BIT },
	{ "dadd",	"s,t",		MIPS_SPECIAL(0x2C),				MA_MIPS3,	MO_64BIT|MO_RSD },
	{ "max",	"d,s,t",	MIPS_SPECIAL(0x2C),				MA_PSP,		0 },
	{ "daddu",	"d,s,t",	MIPS_SPECIAL(0x2D), 			MA_MIPS3,	MO_64BIT },
	{ "daddu",	"s,t",		MIPS_SPECIAL(0x2D), 			MA_MIPS3,	MO_64BIT|MO_RSD },
	{ "dmove",	"d,s",		MIPS_SPECIAL(0x2D), 			MA_MIPS3,	MO_64BIT },
	{ "min",	"d,s,t",	MIPS_SPECIAL(0x2D), 			MA_PSP,		0 },
	{ "dsub",	"d,s,t",	MIPS_SPECIAL(0x2E), 			MA_MIPS3,	MO_64BIT },
	{ "dsub",	"s,t",		MIPS_SPECIAL(0x2E), 			MA_MIPS3,	MO_64BIT|MO_RSD },
	{ "dneg",	"d,t",		MIPS_SPECIAL(0x2E),				MA_MIPS3,	MO_64BIT },
	{ "msub",	"s,t",		MIPS_SPECIAL(0x2E),				MA_PSP,		0 },
	{ "dsubu",	"d,s,t",	MIPS_SPECIAL(0x2F), 			MA_MIPS3,	MO_64BIT },
	{ "dsubu",	"s,t",		MIPS_SPECIAL(0x2F), 			MA_MIPS3,	MO_64BIT|MO_RSD },
	{ "dnegu",	"d,t",		MIPS_SPECIAL(0x2F),				MA_MIPS3,	MO_64BIT },
	{ "msubu",	"s,t",		MIPS_SPECIAL(0x2F),				MA_PSP,		0 },
	{ "tge",	"s,t,i10",	MIPS_SPECIAL(0x30),				MA_MIPS2,	0 },
	{ "tge",	"s,t",		MIPS_SPECIAL(0x30),				MA_MIPS2,	0 },
	{ "tgeu",	"s,t,i10",	MIPS_SPECIAL(0x31),				MA_MIPS2,	0 },
	{ "tgeu",	"s,t",		MIPS_SPECIAL(0x31),				MA_MIPS2,	0 },
	{ "tlt",	"s,t,i10",	MIPS_SPECIAL(0x32),				MA_MIPS2,	0 },
	{ "tlt",	"s,t",		MIPS_SPECIAL(0x32),				MA_MIPS2,	0 },
	{ "tltu",	"s,t,i10",	MIPS_SPECIAL(0x33),				MA_MIPS2,	0 },
	{ "tltu",	"s,t",		MIPS_SPECIAL(0x33),				MA_MIPS2,	0 },
	{ "teq",	"s,t,i10",	MIPS_SPECIAL(0x34),				MA_MIPS2,	0 },
	{ "teq",	"s,t",		MIPS_SPECIAL(0x34),				MA_MIPS2,	0 },
	{ "tne",	"s,t,i10",	MIPS_SPECIAL(0x36),				MA_MIPS2,	0 },
	{ "tne",	"s,t",		MIPS_SPECIAL(0x36),				MA_MIPS2,	0 },
	{ "dsll",	"d,t,i5",	MIPS_SPECIAL(0x38),				MA_MIPS3,	MO_64BIT },
	{ "dsll",	"d,i5",		MIPS_SPECIAL(0x38),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsrl",	"d,t,i5",	MIPS_SPECIAL(0x3A),				MA_MIPS3,	MO_64BIT },
	{ "dsrl",	"d,i5",		MIPS_SPECIAL(0x3A),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsra",	"d,t,i5",	MIPS_SPECIAL(0x3B),				MA_MIPS3,	MO_64BIT },
	{ "dsra",	"d,i5",		MIPS_SPECIAL(0x3B),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsll32",	"d,t,i5",	MIPS_SPECIAL(0x3C),				MA_MIPS3,	MO_64BIT },
	{ "dsll32",	"d,i5",		MIPS_SPECIAL(0x3C),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsrl32",	"d,t,i5",	MIPS_SPECIAL(0x3E),				MA_MIPS3,	MO_64BIT },
	{ "dsrl32",	"d,i5",		MIPS_SPECIAL(0x3E),				MA_MIPS3,	MO_64BIT|MO_RDT },
	{ "dsra32",	"d,t,i5",	MIPS_SPECIAL(0x3F),				MA_MIPS3,	MO_64BIT },
	{ "dsra32",	"d,i5",		MIPS_SPECIAL(0x3F),				MA_MIPS3,	MO_64BIT|MO_RDT },

//     REGIMM: encoded by the rt field when opcode field = REGIMM.
//     31---------26----------20-------16------------------------------0
//     |=    REGIMM|          |   rt    |                              |
//     ------6---------------------5------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | BLTZ  | BGEZ  | BLTZL | BGEZL |  ---  |  ---  |  ---  |  ---  | 00-07
//  01 | tgei  | tgeiu | tlti  | tltiu | teqi  |  ---  | tnei  |  ---  | 08-0F
//  10 | BLTZAL| BGEZAL|BLTZALL|BGEZALL|  ---  |  ---  |  ---  |  ---  | 10-17
//  11 | mtsab | mtsah |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18-1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "bltz",	"s,i16",	MIPS_REGIMM(0x00),				MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgez",	"s,i16",	MIPS_REGIMM(0x01),				MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bltzl",	"s,i16",	MIPS_REGIMM(0x02),				MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgezl",	"s,i16",	MIPS_REGIMM(0x03),				MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "tgei",	"s,i16",	MIPS_REGIMM(0x08),				MA_MIPS2,	0 },
	{ "tgeiu",	"s,i16",	MIPS_REGIMM(0x09),				MA_MIPS2,	0 },
	{ "tlti",	"s,i16",	MIPS_REGIMM(0x0A),				MA_MIPS2,	0 },
	{ "tltiu",	"s,i16",	MIPS_REGIMM(0x0B),				MA_MIPS2,	0 },
	{ "teqi",	"s,i16",	MIPS_REGIMM(0x0C),				MA_MIPS2,	0 },
	{ "tnei",	"s,i16",	MIPS_REGIMM(0x0E),				MA_MIPS2,	0 },
	{ "bltzal",	"s,i16",	MIPS_REGIMM(0x10),				MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgezal",	"s,i16",	MIPS_REGIMM(0x11),				MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bal",	"i16",		MIPS_REGIMM(0x11),				MA_MIPS1,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bltzall","s,i16",	MIPS_REGIMM(0x12),				MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bgezall","s,i16",	MIPS_REGIMM(0x13),				MA_MIPS2,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "mtsab",	"s,i16",	MIPS_REGIMM(0x18),				MA_PS2,		0 },
	{ "mtsah",	"s,i16",	MIPS_REGIMM(0x19),				MA_PS2,		0 },

//     31---------26---------21----------------------------------------0
//     |=      COP0|    rs    |                                        |
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  MFC0 | DMFC0 |  ---  |  ---  |  MTC0 | DMTC0 |  ---  |  ---  | 00..07
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
//  10 |FUNCT* |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "mfc0",	"t,z",		MIPS_COP0(0x00),				MA_MIPS1|MA_EXRSP,	0 },
	{ "mfc0",	"t,Rz",		MIPS_COP0(0x00),				MA_RSP,		0 },
	{ "dmfc0",	"t,z",		MIPS_COP0(0x01),				MA_MIPS3,	MO_64BIT },
	{ "mtc0",	"t,z",		MIPS_COP0(0x04),				MA_MIPS1|MA_EXRSP,	0 },
	{ "mtc0",	"t,Rz",		MIPS_COP0(0x04),				MA_RSP,		0 },
	{ "dmtc0",	"t,z",		MIPS_COP0(0x05),				MA_MIPS3,	MO_64BIT },

//     31--------------------21-------------------------------5--------0
//     |=            COP0FUNCT|                              | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  ---  | TLBR  | TLBWI |  ---  |  ---  |  ---  | TLBWR |  ---  | 00..07
// 001 | TLBP  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
// 011 | ERET  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
// 100 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 20..27
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 28..2F
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 30..37
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "tlbr",	"",			MIPS_COP0FUNCT(0x01),			MA_MIPS3,	0 },
	{ "tlbwi",	"",			MIPS_COP0FUNCT(0x02),			MA_MIPS3,	0 },
	{ "tlbwr",	"",			MIPS_COP0FUNCT(0x06),			MA_MIPS3,	0 },
	{ "tlbp",	"",			MIPS_COP0FUNCT(0x08),			MA_MIPS3,	0 },
	{ "eret",	"",			MIPS_COP0FUNCT(0x18),			MA_MIPS3,	0 },

//     31---------26---------21----------------------------------------0
//     |=      COP1|    rs    |                                        |
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  MFC1 | DMFC1 |  CFC1 |  ---  |  MTC1 | DMTC1 |  CTC1 |  ---  | 00..07
//  01 |  BC*  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
//  10 |  S*   |  ---  |  ---  |  ---  |  W*   |  ---  |  ---  |  ---  | 10..17
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|

	{ "mfc1",	"t,S",		MIPS_COP1(0x00),				MA_MIPS1,	MO_FPU },
	{ "dmfc1",	"t,S",		MIPS_COP1(0x01),				MA_MIPS3,	MO_DFPU|MO_64BIT },
	{ "cfc1",	"t,f",		MIPS_COP1(0x02),				MA_MIPS1,	MO_FPU },
	{ "mtc1",	"t,S",		MIPS_COP1(0x04),				MA_MIPS1,	MO_FPU },
	{ "dmtc1",	"t,S",		MIPS_COP1(0x05),				MA_MIPS3,	MO_DFPU|MO_64BIT },
	{ "ctc1",	"t,f",		MIPS_COP1(0x06),				MA_MIPS1,	MO_FPU },

//     31---------26----------20-------16------------------------------0
//     |=    COP1BC|          |   rt    |                              |
//     ------11--------------5------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  BC1F | BC1T  | BC1FL | BC1TL |  ---  |  ---  |  ---  |  ---  | 00..07
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "bc1f",	"i16",		MIPS_COP1BC(0x00),				MA_MIPS1,	MO_FPU|MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bc1t",	"i16",		MIPS_COP1BC(0x01),				MA_MIPS1,	MO_FPU|MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bc1fl",	"i16",		MIPS_COP1BC(0x02),				MA_MIPS2,	MO_FPU|MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bc1tl",	"i16",		MIPS_COP1BC(0x03),				MA_MIPS2,	MO_FPU|MO_IPCR|MO_DELAY|MO_NODELAYSLOT },

//     31--------------------21-------------------------------5--------0
//     |=                COP1S|                              | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  add  |  sub  |  mul  |  div  | sqrt  |  abs  |  mov  |  neg  | 00..07
// 001 |round.l|trunc.l|ceil.l |floor.l|round.w|trunc.w|ceil.w |floor.w| 08..0F
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | rsqrt |  ---  | 10..17
// 011 | adda  | suba  | mula  |  ---  | madd  | msub  | madda | msuba | 18..1F
// 100 |  ---  | cvt.d |  ---  |  ---  | cvt.w | cvt.l |  ---  |  ---  | 20..27
// 101 |  max  |  min  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 28..2F
// 110 |  c.f  | c.un  | c.eq  | c.ueq |c.(o)lt| c.ult |c.(o)le| c.ule | 30..37
// 110 |  c.sf | c.ngle| c.seq | c.ngl | c.lt  | c.nge | c.le  | c.ngt | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "add.s",		"D,S,T",	MIPS_COP1S(0x00),			MA_MIPS1,	MO_FPU },
	{ "add.s",		"S,T",		MIPS_COP1S(0x00),			MA_MIPS1,	MO_FPU|MO_FRSD },
	{ "sub.s",		"D,S,T",	MIPS_COP1S(0x01),			MA_MIPS1,	MO_FPU },
	{ "sub.s",		"S,T",		MIPS_COP1S(0x01),			MA_MIPS1,	MO_FPU|MO_FRSD },
	{ "mul.s",		"D,S,T",	MIPS_COP1S(0x02),			MA_MIPS1,	MO_FPU },
	{ "mul.s",		"S,T",		MIPS_COP1S(0x02),			MA_MIPS1,	MO_FPU|MO_FRSD },
	{ "div.s",		"D,S,T",	MIPS_COP1S(0x03),			MA_MIPS1,	MO_FPU },
	{ "div.s",		"S,T",		MIPS_COP1S(0x03),			MA_MIPS1,	MO_FPU|MO_FRSD },
	{ "sqrt.s",		"D,S",		MIPS_COP1S(0x04),			MA_MIPS2,	MO_FPU },
	{ "abs.s",		"D,S",		MIPS_COP1S(0x05),			MA_MIPS1,	MO_FPU },
	{ "mov.s",		"D,S",		MIPS_COP1S(0x06),			MA_MIPS1,	MO_FPU },
	{ "neg.s",		"D,S",		MIPS_COP1S(0x07),			MA_MIPS1,	MO_FPU },
	{ "round.l.s",	"D,S",		MIPS_COP1S(0x08),			MA_MIPS3,	MO_DFPU },
	{ "trunc.l.s",	"D,S",		MIPS_COP1S(0x09),			MA_MIPS3,	MO_DFPU },
	{ "ceil.l.s",	"D,S",		MIPS_COP1S(0x0A),			MA_MIPS3,	MO_DFPU },
	{ "floor.l.s",	"D,S",		MIPS_COP1S(0x0B),			MA_MIPS3,	MO_DFPU },
	{ "round.w.s",	"D,S",		MIPS_COP1S(0x0C),			MA_MIPS2|MA_EXPS2,	MO_FPU },
	{ "trunc.w.s",	"D,S",		MIPS_COP1S(0x0D),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "ceil.w.s",	"D,S",		MIPS_COP1S(0x0E),			MA_MIPS2|MA_EXPS2,	MO_FPU },
	{ "floor.w.s",	"D,S",		MIPS_COP1S(0x0F),			MA_MIPS2|MA_EXPS2,	MO_FPU },
	{ "rsqrt.w.s",	"D,S",		MIPS_COP1S(0x16),			MA_PS2,		0 },
	{ "adda.s",		"S,T",		MIPS_COP1S(0x18),			MA_PS2,		0 },
	{ "suba.s",		"S,T",		MIPS_COP1S(0x19),			MA_PS2,		0 },
	{ "mula.s",		"S,T",		MIPS_COP1S(0x1A),			MA_PS2,		0 },
	{ "madd.s",		"D,S,T",	MIPS_COP1S(0x1C),			MA_PS2,		0 },
	{ "madd.s",		"S,T",		MIPS_COP1S(0x1C),			MA_PS2,		MO_FRSD },
	{ "msub.s",		"D,S,T",	MIPS_COP1S(0x1D),			MA_PS2,		0 },
	{ "msub.s",		"S,T",		MIPS_COP1S(0x1D),			MA_PS2,		MO_FRSD },
	{ "madda.s",	"S,T",		MIPS_COP1S(0x1E),			MA_PS2,		0 },
	{ "msuba.s",	"S,T",		MIPS_COP1S(0x1F),			MA_PS2,		0 },
	{ "cvt.d.s",	"D,S",		MIPS_COP1S(0x21),			MA_MIPS1,	MO_DFPU },
	{ "cvt.w.s",	"D,S",		MIPS_COP1S(0x24),			MA_MIPS1,	MO_FPU },
	{ "cvt.l.s",	"D,S",		MIPS_COP1S(0x25),			MA_MIPS3,	MO_DFPU },
	{ "max.s",		"D,S,T",	MIPS_COP1S(0x28),			MA_PS2,		0 },
	{ "min.s",		"D,S,T",	MIPS_COP1S(0x29),			MA_PS2,		0 },
	{ "c.f.s",		"S,T",		MIPS_COP1S(0x30),			MA_MIPS1,	MO_FPU },
	{ "c.un.s",		"S,T",		MIPS_COP1S(0x31),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.eq.s",		"S,T",		MIPS_COP1S(0x32),			MA_MIPS1,	MO_FPU },
	{ "c.ueq.s",	"S,T",		MIPS_COP1S(0x33),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.olt.s",	"S,T",		MIPS_COP1S(0x34),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.lt.s",		"S,T",		MIPS_COP1S(0x34),			MA_PS2,		0 },
	{ "c.ult.s",	"S,T",		MIPS_COP1S(0x35),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.ole.s",	"S,T",		MIPS_COP1S(0x36),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.le.s",		"S,T",		MIPS_COP1S(0x36),			MA_PS2,		0 },
	{ "c.ule.s",	"S,T",		MIPS_COP1S(0x37),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.sf.s",		"S,T",		MIPS_COP1S(0x38),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.ngle.s",	"S,T",		MIPS_COP1S(0x39),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.seq.s",	"S,T",		MIPS_COP1S(0x3A),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.ngl.s",	"S,T",		MIPS_COP1S(0x3B),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.lt.s",		"S,T",		MIPS_COP1S(0x3C),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.nge.s",	"S,T",		MIPS_COP1S(0x3D),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.le.s",		"S,T",		MIPS_COP1S(0x3E),			MA_MIPS1|MA_EXPS2,	MO_FPU },
	{ "c.ngt.s",	"S,T",		MIPS_COP1S(0x3F),			MA_MIPS1|MA_EXPS2,	MO_FPU },

//     31--------------------21-------------------------------5--------0
//     |=                COP1D|                              | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  add  |  sub  |  mul  |  div  | sqrt  |  abs  |  mov  |  neg  | 00..07
// 001 |round.l|trunc.l|ceil.l |floor.l|round.w|trunc.w|ceil.w |floor.w| 08..0F
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
// 011 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
// 100 | cvt.s |  ---  |  ---  |  ---  | cvt.w | cvt.l |  ---  |  ---  | 20..27
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 28..2F
// 110 |  c.f  | c.un  | c.eq  | c.ueq | c.olt | c.ult | c.ole | c.ule | 30..37
// 110 |  c.sf | c.ngle| c.seq | c.ngl | c.lt  | c.nge | c.le  | c.ngt | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "add.d",		"D,S,T",	MIPS_COP1D(0x00),			MA_MIPS1,	MO_DFPU },
	{ "add.d",		"S,T",		MIPS_COP1D(0x00),			MA_MIPS1,	MO_DFPU|MO_FRSD },
	{ "sub.d",		"D,S,T",	MIPS_COP1D(0x01),			MA_MIPS1,	MO_DFPU },
	{ "sub.d",		"S,T",		MIPS_COP1D(0x01),			MA_MIPS1,	MO_DFPU|MO_FRSD },
	{ "mul.d",		"D,S,T",	MIPS_COP1D(0x02),			MA_MIPS1,	MO_DFPU },
	{ "mul.d",		"S,T",		MIPS_COP1D(0x02),			MA_MIPS1,	MO_DFPU|MO_FRSD },
	{ "div.d",		"D,S,T",	MIPS_COP1D(0x03),			MA_MIPS1,	MO_DFPU },
	{ "div.d",		"S,T",		MIPS_COP1D(0x03),			MA_MIPS1,	MO_DFPU|MO_FRSD },
	{ "sqrt.d",		"D,S",		MIPS_COP1D(0x04),			MA_MIPS2,	MO_DFPU },
	{ "abs.d",		"D,S",		MIPS_COP1D(0x05),			MA_MIPS1,	MO_DFPU },
	{ "mov.d",		"D,S",		MIPS_COP1D(0x06),			MA_MIPS1,	MO_DFPU },
	{ "neg.d",		"D,S",		MIPS_COP1D(0x07),			MA_MIPS1,	MO_DFPU },
	{ "round.l.d",	"D,S",		MIPS_COP1D(0x08),			MA_MIPS3,	MO_DFPU },
	{ "trunc.l.d",	"D,S",		MIPS_COP1D(0x09),			MA_MIPS3,	MO_DFPU },
	{ "ceil.l.d",	"D,S",		MIPS_COP1D(0x0A),			MA_MIPS3,	MO_DFPU },
	{ "floor.l.d",	"D,S",		MIPS_COP1D(0x0B),			MA_MIPS3,	MO_DFPU },
	{ "round.w.d",	"D,S",		MIPS_COP1D(0x0C),			MA_MIPS2,	MO_DFPU },
	{ "trunc.w.d",	"D,S",		MIPS_COP1D(0x0D),			MA_MIPS1,	MO_DFPU },
	{ "ceil.w.d",	"D,S",		MIPS_COP1D(0x0E),			MA_MIPS2,	MO_DFPU },
	{ "floor.w.d",	"D,S",		MIPS_COP1D(0x0F),			MA_MIPS2,	MO_DFPU },
	{ "cvt.s.d",	"D,S",		MIPS_COP1D(0x20),			MA_MIPS1,	MO_DFPU },
	{ "cvt.w.d",	"D,S",		MIPS_COP1D(0x24),			MA_MIPS1,	MO_DFPU },
	{ "cvt.l.d",	"D,S",		MIPS_COP1D(0x25),			MA_MIPS3,	MO_DFPU },
	{ "c.f.d",		"S,T",		MIPS_COP1D(0x30),			MA_MIPS1,	MO_DFPU },
	{ "c.un.d",		"S,T",		MIPS_COP1D(0x31),			MA_MIPS1,	MO_DFPU },
	{ "c.eq.d",		"S,T",		MIPS_COP1D(0x32),			MA_MIPS1,	MO_DFPU },
	{ "c.ueq.d",	"S,T",		MIPS_COP1D(0x33),			MA_MIPS1,	MO_DFPU },
	{ "c.olt.d",	"S,T",		MIPS_COP1D(0x34),			MA_MIPS1,	MO_DFPU },
	{ "c.ult.d",	"S,T",		MIPS_COP1D(0x35),			MA_MIPS1,	MO_DFPU },
	{ "c.ole.d",	"S,T",		MIPS_COP1D(0x36),			MA_MIPS1,	MO_DFPU },
	{ "c.ule.d",	"S,T",		MIPS_COP1D(0x37),			MA_MIPS1,	MO_DFPU },
	{ "c.sf.d",		"S,T",		MIPS_COP1D(0x38),			MA_MIPS1,	MO_DFPU },
	{ "c.ngle.d",	"S,T",		MIPS_COP1D(0x39),			MA_MIPS1,	MO_DFPU },
	{ "c.seq.d",	"S,T",		MIPS_COP1D(0x3A),			MA_MIPS1,	MO_DFPU },
	{ "c.ngl.d",	"S,T",		MIPS_COP1D(0x3B),			MA_MIPS1,	MO_DFPU },
	{ "c.lt.d",		"S,T",		MIPS_COP1D(0x3C),			MA_MIPS1,	MO_DFPU },
	{ "c.nge.d",	"S,T",		MIPS_COP1D(0x3D),			MA_MIPS1,	MO_DFPU },
	{ "c.le.d",		"S,T",		MIPS_COP1D(0x3E),			MA_MIPS1,	MO_DFPU },
	{ "c.ngt.d",	"S,T",		MIPS_COP1D(0x3F),			MA_MIPS1,	MO_DFPU },

//     31--------------------21-------------------------------5--------0
//     |=                COP1W|                              | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 00..07
// 001 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
// 011 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
// 100 | cvt.s | cvt.d |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 20..27
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 28..2F
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 30..37
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "cvt.s.w",	"D,S",		MIPS_COP1W(0x20),			MA_MIPS1,	MO_FPU },
	{ "cvt.d.w",	"D,S",		MIPS_COP1W(0x21),			MA_MIPS1,	MO_DFPU },

//     31--------------------21-------------------------------5--------0
//     |=                COP1L|                              | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 00..07
// 001 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 08..0F
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
// 011 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
// 100 | cvt.s | cvt.d |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 20..27
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 28..2F
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 30..37
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "cvt.s.l",	"D,S",		MIPS_COP1L(0x20),			MA_MIPS3,	MO_DFPU },
	{ "cvt.d.l",	"D,S",		MIPS_COP1L(0x21),			MA_MIPS3,	MO_DFPU },

//     31---------26---------21----------------------------------------0
//     |=      COP2|    rs    |                                        |
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  MFC2 |  ---  |  CFC2 |  MFV  |  MTC2 |  ---  |  CTC2 |  MTV  |
//  01 |  BC*  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|

	{ "mfc2",	"t,RsRo",		MIPS_COP2(0x00),			MA_RSP, 0 },
	{ "cfc2",	"t,d",			MIPS_COP2(0x02),			MA_RSP, 0 },
	{ "mtc2",	"t,RsRo",		MIPS_COP2(0x04),			MA_RSP, 0 },
	{ "ctc2",	"t,d",			MIPS_COP2(0x06),			MA_RSP, 0 },
	// VVVVVV VVVVV ttttt -------- C DDDDDDD
	{ "mfv",	"t,vd",			MIPS_COP2(0x03),			MA_PSP,	MO_VFPU|MO_VFPU_SINGLE },
	{ "mfvc",	"t,vc",			MIPS_COP2(0x03) | 0x80,		MA_PSP,	MO_VFPU },
	{ "mtv",	"t,vd",			MIPS_COP2(0x07),			MA_PSP,	MO_VFPU|MO_VFPU_SINGLE },
	{ "mtvc",	"t,vc",			MIPS_COP2(0x07) | 0x80,		MA_PSP,	MO_VFPU },


//     COP2BC: ? indicates any, * indicates all
//     31---------26----------20-------16------------------------------0
//     |=    COP2BC|          |   rt    |                              |
//     ------11---------5-----------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  BVFx |  BVTx | BVFLx | BVTLx |  BVFy |  BVTy | BVFLy | BVTLy |
//  01 |  BVFz |  BVTz | BVFLz | BVTLz |  BVFw |  BVTw | BVFLw | BVTLw |
//  10 |  BVF? |  BVT? | BVFL? | BVTL? |  BVF* |  BVT* | BVFL* | BVTL* |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "bvf",		"jb,i16",	MIPS_COP2BC(0x00),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvf.B",		"i16",		MIPS_COP2BC(0x00),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvt",		"jb,i16",	MIPS_COP2BC(0x01),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvt.B",		"i16",		MIPS_COP2BC(0x01),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvfl",		"jb,i16",	MIPS_COP2BC(0x02),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvfl.B",		"i16",		MIPS_COP2BC(0x02),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvtl",		"jb,i16",	MIPS_COP2BC(0x03),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },
	{ "bvtl.B",		"i16",		MIPS_COP2BC(0x03),			MA_PSP,	MO_IPCR|MO_DELAY|MO_NODELAYSLOT },

//     31---------26-----23--------------------------------------------0
//     |= VFPU0| VOP | |
//     ------6--------3-------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
// 000 | VADD  | VSUB  | VSBN  | ---   | ---   | ---   | ---   | VDIV  | 00..07
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vadd.S",		"vd,vs,vt",	MIPS_VFPU0(0x00),			MA_PSP,	MO_VFPU },
	{ "vsub.S",		"vd,vs,vt",	MIPS_VFPU0(0x01),			MA_PSP,	MO_VFPU },
	{ "vsbn.S",		"vd,vs,vt",	MIPS_VFPU0(0x02),			MA_PSP,	MO_VFPU },
	{ "vdiv.S",		"vd,vs,vt",	MIPS_VFPU0(0x07),			MA_PSP,	MO_VFPU },

//     31-------26-----23----------------------------------------------0
//     |=   VFPU1|  f  |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
//     |  VMUL |  VDOT |  VSCL |  ---  |  VHDP |  VDET |  VCRS |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vmul.S",		"vd,vs,vt",	MIPS_VFPU1(0),				MA_PSP,	MO_VFPU },
	{ "vdot.S",		"vd,vs,vt",	MIPS_VFPU1(1),				MA_PSP,	MO_VFPU },
	{ "vscl.S",		"vd,vs,vt",	MIPS_VFPU1(2),				MA_PSP,	MO_VFPU },
	{ "vhdp.S",		"vd,vs,vt",	MIPS_VFPU1(4),				MA_PSP,	MO_VFPU },
	{ "vdet.S",		"vd,vs,vt",	MIPS_VFPU1(5),				MA_PSP,	MO_VFPU },
	{ "vcrs.S",		"vd,vs,vt",	MIPS_VFPU1(6),				MA_PSP,	MO_VFPU },

//     31-------26-----23----------------------------------------------0
//     |=   VFPU3|  f  |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
//     |  VCMP |  ---  |  VMIN |  VMAX |  ---  | VSCMP |  VSGE |  VSLT |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVV TTTTTTT z SSSSSSS z --- CCCC
	{ "vcmp.S",		"C,vs,vt",	MIPS_VFPU3(0),				MA_PSP,	MO_VFPU },
	{ "vmin.S",		"vd,vs,vt",	MIPS_VFPU3(2),				MA_PSP,	MO_VFPU },
	{ "vmax.S",		"vd,vs,vt",	MIPS_VFPU3(3),				MA_PSP,	MO_VFPU },
	{ "vscmp.S",	"vd,vs,vt",	MIPS_VFPU3(5),				MA_PSP,	MO_VFPU },
	{ "vsge.S",		"vd,vs,vt",	MIPS_VFPU3(6),				MA_PSP,	MO_VFPU },
	{ "vslt.S",		"vd,vs,vt",	MIPS_VFPU3(7),				MA_PSP,	MO_VFPU },

//     31-------26--------------------------------------------5--------0
//     |=SPECIAL3|                                           | function|
//     -----11----------------------------------------------------6-----
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  EXT  |  ---  |  ---  |  ---  |  INS  |  ---  |  ---  |  ---  |
// 001 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 011 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 100 |ALLEGRE|  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "ext",	"t,s,i5,je",	MIPS_SPECIAL3(0),			MA_PSP },
	{ "ins",	"t,s,i5,ji",	MIPS_SPECIAL3(4),			MA_PSP },
	
//     31-------26----------------------------------10--------5--------0
//     |=SPECIAL3|                                 | secfunc |ALLEGREX0|
//     ------11---------5-------------------------------5---------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  ---  |  ---  | WSBH  | WSBW  |  ---  |  ---  |  ---  |  ---  |
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 |  SEB  |  ---  |  ---  |  ---  |BITREV |  ---  |  ---  |  ---  |
//  11 |  SEH  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV ----- ttttt ddddd VVVVV VVVVVV
	{ "wsbh",	"d,t",			MIPS_ALLEGREX0(0x02),		MA_PSP },
	{ "wsbh",	"d",			MIPS_ALLEGREX0(0x02),		MA_PSP },
	{ "wsbw",	"d,t",			MIPS_ALLEGREX0(0x03),		MA_PSP },
	{ "wsbw",	"d",			MIPS_ALLEGREX0(0x03),		MA_PSP },
	{ "seb",	"d,t",			MIPS_ALLEGREX0(0x10),		MA_PSP },
	{ "seb",	"d",			MIPS_ALLEGREX0(0x10),		MA_PSP },
	{ "bitrev",	"d,t",			MIPS_ALLEGREX0(0x14),		MA_PSP },
	{ "bitrev",	"d",			MIPS_ALLEGREX0(0x14),		MA_PSP },
	{ "seh",	"d,t",			MIPS_ALLEGREX0(0x18),		MA_PSP },
	{ "seh",	"d",			MIPS_ALLEGREX0(0x18),		MA_PSP },


//     VFPU4: This one is a bit messy.
//     31-------26------21---------------------------------------------0
//     |=   VFPU4|  rs  |                                              |
//     -----6-------5---------------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |VF4-1.1|VF4-1.2|VF4-1.3| VCST  |  ---  |  ---  |  ---  |  ---  |
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 | VF2IN | VF2IZ | VF2IU | VF2ID | VI2F  | VCMOV |  ---  |  ---  |
//  11 | VWBN  | VWBN  | VWBN  | VWBN  | VWBN  | VWBN  | VWBN  | VWBN  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVVVV iiiii z ------- z DDDDDDD
	// Technically these also have names (as the second arg.)
	{ "vcst.S",		"vd,Wc",	MIPS_VFPU4(0x03),			MA_PSP, MO_VFPU },
	{ "vf2in.S",	"vd,vs,i5",	MIPS_VFPU4(0x10),			MA_PSP, MO_VFPU },
	{ "vf2iz.S",	"vd,vs,i5",	MIPS_VFPU4(0x11),			MA_PSP, MO_VFPU },
	{ "vf2iu.S",	"vd,vs,i5",	MIPS_VFPU4(0x12),			MA_PSP, MO_VFPU },
	{ "vf2id.S",	"vd,vs,i5",	MIPS_VFPU4(0x13),			MA_PSP, MO_VFPU },
	{ "vi2f.S",		"vd,vs,i5",	MIPS_VFPU4(0x14),			MA_PSP, MO_VFPU },
	{ "vcmovt.S",	"vd,vs,i5",	MIPS_VFPU4(0x15) | 0,  		MA_PSP, MO_VFPU },
	{ "vcmovf.S",	"vd,vs,i5",	MIPS_VFPU4(0x15) | (1<<19),	MA_PSP, MO_VFPU },
	{ "vwbn.S",		"vd,vs,i5",	MIPS_VFPU4(0x18),			MA_PSP, MO_VFPU },

//     31-------------21-------16--------------------------------------0
//     |= VF4-1.1      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | VMOV  | VABS  | VNEG  | VIDT  | vsAT0 | vsAT1 | VZERO | VONE  |
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 | VRCP  | VRSQ  | vsIN  | VCOS  | VEXP2 | VLOG2 | vsQRT | VASIN |
//  11 | VNRCP |  ---  | VNSIN |  ---  |VREXP2 |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vmov.S",		"vd,vs",	MIPS_VFPU4_11(0x00),		MA_PSP, MO_VFPU },
	{ "vabs.S",		"vd,vs",	MIPS_VFPU4_11(0x01),		MA_PSP, MO_VFPU },
	{ "vneg.S",		"vd,vs",	MIPS_VFPU4_11(0x02), 		MA_PSP, MO_VFPU },
	{ "vidt.S",		"vd",		MIPS_VFPU4_11(0x03),		MA_PSP, MO_VFPU },
	{ "vsat0.S",	"vd,vs",	MIPS_VFPU4_11(0x04),		MA_PSP, MO_VFPU },
	{ "vsat1.S",	"vd,vs",	MIPS_VFPU4_11(0x05),		MA_PSP, MO_VFPU },
	{ "vzero.S",	"vd",		MIPS_VFPU4_11(0x06),		MA_PSP, MO_VFPU },
	{ "vone.S",		"vd",		MIPS_VFPU4_11(0x07),		MA_PSP, MO_VFPU },
	{ "vrcp.S",		"vd,vs",	MIPS_VFPU4_11(0x10),		MA_PSP, MO_VFPU },
	{ "vrsq.S",		"vd,vs",	MIPS_VFPU4_11(0x11),		MA_PSP, MO_VFPU },
	{ "vsin.S",		"vd,vs",	MIPS_VFPU4_11(0x12),		MA_PSP, MO_VFPU },
	{ "vcos.S",		"vd,vs",	MIPS_VFPU4_11(0x13),		MA_PSP, MO_VFPU },
	{ "vexp2.S",	"vd,vs",	MIPS_VFPU4_11(0x14),		MA_PSP, MO_VFPU },
	{ "vlog2.S",	"vd,vs",	MIPS_VFPU4_11(0x15),		MA_PSP, MO_VFPU },
	{ "vsqrt.S",	"vd,vs",	MIPS_VFPU4_11(0x16),		MA_PSP, MO_VFPU },
	{ "vasin.S",	"vd,vs",	MIPS_VFPU4_11(0x17),		MA_PSP, MO_VFPU },
	{ "vnrcp.S",	"vd,vs",	MIPS_VFPU4_11(0x18),		MA_PSP, MO_VFPU },
	{ "vnsin.S",	"vd,vs",	MIPS_VFPU4_11(0x1a),		MA_PSP, MO_VFPU },
	{ "vrexp2.S",	"vd,vs",	MIPS_VFPU4_11(0x1c),		MA_PSP, MO_VFPU },

//     VFPU4 1.2: TODO: Unsure where vsBZ goes, no one uses it.
//     31-------------21-------16--------------------------------------0
//     |= VF4-1.2      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | VRNDS | VRNDI |VRNDF1 |VRNDF2 |  ---  |  ---  |  ---  |  ---  |
//  01 |  ---  |  ---  |  ---  |  ---  | vsBZ? |  ---  |  ---  |  ---  |
//  10 |  ---  |  ---  | VF2H  | VH2F  |  ---  |  ---  | vsBZ? | VLGB  |
//  11 | VUC2I | VC2I  | VUS2I | vs2I  | VI2UC | VI2C  | VI2US | VI2S  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vrnds.S",	"vd",		MIPS_VFPU4_12(0x00),		MA_PSP,	MO_VFPU },
	{ "vrndi.S",	"vd",		MIPS_VFPU4_12(0x01),		MA_PSP,	MO_VFPU },
	{ "vrndf1.S",	"vd",		MIPS_VFPU4_12(0x02),		MA_PSP,	MO_VFPU },
	{ "vrndf2.S",	"vd",		MIPS_VFPU4_12(0x03),		MA_PSP,	MO_VFPU },
	// TODO: vsBZ?
	{ "vf2h.S",		"vd,vs",	MIPS_VFPU4_12(0x12),		MA_PSP,	MO_VFPU },
	{ "vh2f.S",		"vd,vs",	MIPS_VFPU4_12(0x13),		MA_PSP,	MO_VFPU },
	// TODO: vsBZ?
	{ "vlgb.S",		"vd,vs",	MIPS_VFPU4_12(0x17),		MA_PSP,	MO_VFPU },
	{ "vuc2i.S",	"vd,vs",	MIPS_VFPU4_12(0x18),		MA_PSP,	MO_VFPU },
	{ "vc2i.S",		"vd,vs",	MIPS_VFPU4_12(0x19),		MA_PSP,	MO_VFPU },
	{ "vus2i.S",	"vd,vs",	MIPS_VFPU4_12(0x1a),		MA_PSP,	MO_VFPU },
	{ "vs2i.S",		"vd,vs",	MIPS_VFPU4_12(0x1b),		MA_PSP,	MO_VFPU },
	{ "vi2uc.S",	"vd,vs",	MIPS_VFPU4_12(0x1c),		MA_PSP,	MO_VFPU },
	{ "vi2c.S",		"vd,vs",	MIPS_VFPU4_12(0x1d),		MA_PSP,	MO_VFPU },
	{ "vi2us.S",	"vd,vs",	MIPS_VFPU4_12(0x1e),		MA_PSP,	MO_VFPU },
	{ "vi2s.S",		"vd,vs",	MIPS_VFPU4_12(0x1f),		MA_PSP,	MO_VFPU },

//     31--------------21------16--------------------------------------0
//     |= VF4-1.3      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | vsRT1 | vsRT2 | VBFY1 | VBFY2 | VOCP  | vsOCP | VFAD  | VAVG  |
//  01 | vsRT3 | vsRT4 | vsGN  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 | VMFVC | VMTVC |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  11 |  ---  |VT4444 |VT5551 |VT5650 |  ---  |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vsrt1.S",	"vd,vs",	MIPS_VFPU4_13(0x00),		MA_PSP,	MO_VFPU },
	{ "vsrt2.S",	"vd,vs",	MIPS_VFPU4_13(0x01),		MA_PSP,	MO_VFPU },
	{ "vbfy1.S",	"vd,vs",	MIPS_VFPU4_13(0x02),		MA_PSP,	MO_VFPU },
	{ "vbfy2.S",	"vd,vs",	MIPS_VFPU4_13(0x03),		MA_PSP,	MO_VFPU },
	{ "vocp.S",		"vd,vs",	MIPS_VFPU4_13(0x04),		MA_PSP,	MO_VFPU },
	{ "vsocp.S",	"vd,vs",	MIPS_VFPU4_13(0x05),		MA_PSP,	MO_VFPU },
	{ "vfad.S",		"vd,vs",	MIPS_VFPU4_13(0x06),		MA_PSP,	MO_VFPU },
	{ "vavg.S",		"vd,vs",	MIPS_VFPU4_13(0x07),		MA_PSP,	MO_VFPU },
	{ "vsrt3.S",	"vd,vs",	MIPS_VFPU4_13(0x08),		MA_PSP,	MO_VFPU },
	{ "vsrt4.S",	"vd,vs",	MIPS_VFPU4_13(0x09),		MA_PSP,	MO_VFPU },
	{ "vsgn.S",		"vd,vs",	MIPS_VFPU4_13(0x0a),		MA_PSP,	MO_VFPU },
	{ "vmfv.S",		"vs,i7",	MIPS_VFPU4_13(0x10) | 0x00,	MA_PSP,	MO_VFPU },
	{ "vmtv.S",		"vs,i7",	MIPS_VFPU4_13(0x11) | 0x00,	MA_PSP,	MO_VFPU },
	{ "vmfvc.S",	"vs,i7",	MIPS_VFPU4_13(0x10) | 0x80,	MA_PSP,	MO_VFPU },
	{ "vmtvc.S",	"vs,i7",	MIPS_VFPU4_13(0x11) | 0x80,	MA_PSP,	MO_VFPU },
	{ "vt4444.S",	"vd,vs",	MIPS_VFPU4_13(0x19),		MA_PSP,	MO_VFPU },
	{ "vt5551.S",	"vd,vs",	MIPS_VFPU4_13(0x1a),		MA_PSP,	MO_VFPU },
	{ "vt5650.S",	"vd,vs",	MIPS_VFPU4_13(0x1b),		MA_PSP,	MO_VFPU },

//     31-------26-----23----------------------------------------------0
//     |= VFPU5| f     |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//     | VPFXS | VPFXS | VPFXT | VPFXT | VPFXD | VPFXD | VIIM  | VFIM  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vpfxs",		"Ws",		MIPS_VFPU5(0),				MA_PSP },
	{ "vpfxt",		"Ws",		MIPS_VFPU5(2),				MA_PSP },
	{ "vpfxd",		"Wd",		MIPS_VFPU5(4),				MA_PSP },
	{ "viim.s",		"vt,i16",	MIPS_VFPU5(6),				MA_PSP,	MO_VFPU_SINGLE },
	{ "vfim.s",		"vt,ih",	MIPS_VFPU5(7),				MA_PSP,	MO_VFPU_SINGLE },
	
//     31-------26-----23----------------------------------------------0
//     |= VFPU6| f     |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//     | VMMUL |     V(H)TFM2/3/4      | VMSCL |   *1  |  ---  |VF6-1.1|
//     |-------|-------|-------|-------|-------|-------|-------|-------|
//		*1: vcrsp.t/vqmul.q
	{ "vmmul.S",	"md,ms,mt",		MIPS_VFPU6(0),					MA_PSP, MO_VFPU|MO_TRANSPOSE_VS },
	{ "vtfm2.p",	"vd,ms,vt",		MIPS_VFPU6(1)|MIPS_VFPUSIZE(1),	MA_PSP, MO_VFPU|MO_VFPU_PAIR },
	{ "vhtfm2.p",	"vd,ms,vt",		MIPS_VFPU6(2)|MIPS_VFPUSIZE(1),	MA_PSP, MO_VFPU|MO_VFPU_PAIR },
	{ "vtfm3.t",	"vd,ms,vt",		MIPS_VFPU6(2)|MIPS_VFPUSIZE(2),	MA_PSP, MO_VFPU|MO_VFPU_TRIPLE },
	{ "vhtfm3.t",	"vd,ms,vt",		MIPS_VFPU6(3)|MIPS_VFPUSIZE(2),	MA_PSP, MO_VFPU|MO_VFPU_TRIPLE },
	{ "vtfm4.q",	"vd,ms,vt",		MIPS_VFPU6(3)|MIPS_VFPUSIZE(3),	MA_PSP, MO_VFPU|MO_VFPU_QUAD },
	{ "vmscl.S",	"md,ms,vSt",	MIPS_VFPU6(4),					MA_PSP, MO_VFPU },
	{ "vcrsp.t",	"vd,vs,vt",		MIPS_VFPU6(5)|MIPS_VFPUSIZE(2),	MA_PSP, MO_VFPU|MO_VFPU_TRIPLE },
	{ "vqmul.q",	"vd,vs,vt",		MIPS_VFPU6(5)|MIPS_VFPUSIZE(3),	MA_PSP, MO_VFPU|MO_VFPU_QUAD },

//     31--------23----20----------------------------------------------0
//     |= VF6-1.1 |  f |                                               |
//     -----9-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//     |VF6-1.2|  ---  |     VROT      |  ---  |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVVVVVVV iiiii z SSSSSSS z DDDDDDD
	{ "vrot.S",		"vd,vSs,Wr",	MIPS_VFPU6_1VROT(),		MA_PSP, MO_VFPU },

//     31--------20----16----------------------------------------------0
//     |= VF6-1.2 |  f |                                               |
//     -----6-------4---------------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//   0 | VMMOV |  ---  |  ---  | VMIDT |  ---  |  ---  |VMZERO | VMONE |
//   1 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVVVVVVVVVVVV z SSSSSSS z DDDDDDD
	{ "vmmov.S",	"md,ms",	MIPS_VFPU6_2(0),			MA_PSP, MO_VFPU },
	// VVVVVVVVVVVVVVVV z ------- z DDDDDDD
	{ "vmidt.S",	"md",		MIPS_VFPU6_2(3),			MA_PSP, MO_VFPU },
	{ "vmzero.S",	"md",		MIPS_VFPU6_2(6),			MA_PSP, MO_VFPU },
	{ "vmone.S",	"md",		MIPS_VFPU6_2(7),			MA_PSP, MO_VFPU },

//     31---------26------------------------------------------5--------0
//     |=       RSP|                                         | function|
//     ------6----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 | VMULF | VMULU | VRNDP | VMULQ | VMUDL | VMUDM | VMUDN | VMUDH | 00..07
// 001 | VMACF | VMACU | VRNDN | VMACQ | VMADL | VMADH | VMADN | VMADH | 08..0F
// 010 | VADD  | VSUB  | VSUT  | VABS  | VADDC | VSUBC | VADDB | VSUBB | 10..17
// 011 | VACCB | VSUCB | VSAD  | VSAC  | VSUM  | VSAR  | VACC  | VSUC  | 18..1F
// 100 | VLT   | VEQ   | VNE   | VGE   | VCL   | VCH   | VCR   | VMRG  | 20..27
// 101 | VAND  | VNAND | VOR   | VNOR  | VXOR  | VNXOR |  ---  |  ---  | 28..2F
// 110 | VRCP  | VRCPL | VRCPH | VMOV  | VRSQ  | VRSQL | VRSQH | VNOP  | 30..37
// 111 | VEXTT | VEXTQ | VEXTN |  ---  | VINST | VINSQ | VINSN | VNULL | 38..3F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "vmulf",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x00),		MA_RSP, 0 },
	{ "vmulf",	"Rs,RtRe",		MIPS_RSP_COP2(0x00),		MA_RSP, MO_RSPVRSD },
	{ "vmulu",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x01),		MA_RSP, 0 },
	{ "vmulu",	"Rs,RtRe",		MIPS_RSP_COP2(0x01),		MA_RSP, MO_RSPVRSD },
	{ "vrndp",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x02),		MA_RSP, 0 },
	{ "vrndp",	"Rs,RtRe",		MIPS_RSP_COP2(0x02),		MA_RSP, MO_RSPVRSD },
	{ "vmulq",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x03),		MA_RSP, 0 },
	{ "vmulq",	"Rs,RtRe",		MIPS_RSP_COP2(0x03),		MA_RSP, MO_RSPVRSD },
	{ "vmudl",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x04),		MA_RSP, 0 },
	{ "vmudl",	"Rs,RtRe",		MIPS_RSP_COP2(0x04),		MA_RSP, MO_RSPVRSD },
	{ "vmudm",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x05),		MA_RSP, 0 },
	{ "vmudm",	"Rs,RtRe",		MIPS_RSP_COP2(0x05),		MA_RSP, MO_RSPVRSD },
	{ "vmudn",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x06),		MA_RSP, 0 },
	{ "vmudn",	"Rs,RtRe",		MIPS_RSP_COP2(0x06),		MA_RSP, MO_RSPVRSD },
	{ "vmudh",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x07),		MA_RSP, 0 },
	{ "vmudh",	"Rs,RtRe",		MIPS_RSP_COP2(0x07),		MA_RSP, MO_RSPVRSD },
	{ "vmacf",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x08),		MA_RSP, 0 },
	{ "vmacf",	"Rs,RtRe",		MIPS_RSP_COP2(0x08),		MA_RSP, MO_RSPVRSD },
	{ "vmacu",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x09),		MA_RSP, 0 },
	{ "vmacu",	"Rs,RtRe",		MIPS_RSP_COP2(0x09),		MA_RSP, MO_RSPVRSD },
	{ "vrndn",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0a),		MA_RSP, 0 },
	{ "vrndn",	"Rs,RtRe",		MIPS_RSP_COP2(0x0a),		MA_RSP, MO_RSPVRSD },
	{ "vmacq",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0b),		MA_RSP, 0 },
	{ "vmacq",	"Rs,RtRe",		MIPS_RSP_COP2(0x0b),		MA_RSP, MO_RSPVRSD },
	{ "vmadl",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0c),		MA_RSP, 0 },
	{ "vmadl",	"Rs,RtRe",		MIPS_RSP_COP2(0x0c),		MA_RSP, MO_RSPVRSD },
	{ "vmadm",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0d),		MA_RSP, 0 },
	{ "vmadm",	"Rs,RtRe",		MIPS_RSP_COP2(0x0d),		MA_RSP, MO_RSPVRSD },
	{ "vmadn",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0e),		MA_RSP, 0 },
	{ "vmadn",	"Rs,RtRe",		MIPS_RSP_COP2(0x0e),		MA_RSP, MO_RSPVRSD },
	{ "vmadh",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x0f),		MA_RSP, 0 },
	{ "vmadh",	"Rs,RtRe",		MIPS_RSP_COP2(0x0f),		MA_RSP, MO_RSPVRSD },
	{ "vadd",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x10),		MA_RSP, 0 },
	{ "vadd",	"Rs,RtRe",		MIPS_RSP_COP2(0x10),		MA_RSP, MO_RSPVRSD },
	{ "vsub",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x11),		MA_RSP, 0 },
	{ "vsub",	"Rs,RtRe",		MIPS_RSP_COP2(0x11),		MA_RSP, MO_RSPVRSD },
	{ "vsut",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x12),		MA_RSP, 0 },
	{ "vsut",	"Rs,RtRe",		MIPS_RSP_COP2(0x12),		MA_RSP, MO_RSPVRSD },
	{ "vabs",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x13),		MA_RSP, 0 },
	{ "vabs",	"Rs,RtRe",		MIPS_RSP_COP2(0x13),		MA_RSP, MO_RSPVRSD },
	{ "vaddc",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x14),		MA_RSP, 0 },
	{ "vaddc",	"Rs,RtRe",		MIPS_RSP_COP2(0x14),		MA_RSP, MO_RSPVRSD },
	{ "vsubc",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x15),		MA_RSP, 0 },
	{ "vsubc",	"Rs,RtRe",		MIPS_RSP_COP2(0x15),		MA_RSP, MO_RSPVRSD },
	{ "vaddb",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x16),		MA_RSP, 0 },
	{ "vaddb",	"Rs,RtRe",		MIPS_RSP_COP2(0x16),		MA_RSP, MO_RSPVRSD },
	{ "vsubb",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x17),		MA_RSP, 0 },
	{ "vsubb",	"Rs,RtRe",		MIPS_RSP_COP2(0x17),		MA_RSP, MO_RSPVRSD },
	{ "vaccb",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x18),		MA_RSP, 0 },
	{ "vaccb",	"Rs,RtRe",		MIPS_RSP_COP2(0x18),		MA_RSP, MO_RSPVRSD },
	{ "vsucb",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x19),		MA_RSP, 0 },
	{ "vsucb",	"Rs,RtRe",		MIPS_RSP_COP2(0x19),		MA_RSP, MO_RSPVRSD },
	{ "vsad",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1a),		MA_RSP, 0 },
	{ "vsad",	"Rs,RtRe",		MIPS_RSP_COP2(0x1a),		MA_RSP, MO_RSPVRSD },
	{ "vsac",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1b),		MA_RSP, 0 },
	{ "vsac",	"Rs,RtRe",		MIPS_RSP_COP2(0x1b),		MA_RSP, MO_RSPVRSD },
	{ "vsum",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1c),		MA_RSP, 0 },
	{ "vsum",	"Rs,RtRe",		MIPS_RSP_COP2(0x1c),		MA_RSP, MO_RSPVRSD },
	{ "vsar",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1d),		MA_RSP, 0 },
	{ "vsar",	"Rs,RtRe",		MIPS_RSP_COP2(0x1d),		MA_RSP, MO_RSPVRSD },
	{ "vacc",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1e),		MA_RSP, 0 },
	{ "vacc",	"Rs,RtRe",		MIPS_RSP_COP2(0x1e),		MA_RSP, MO_RSPVRSD },
	{ "vsuc",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x1f),		MA_RSP, 0 },
	{ "vsuc",	"Rs,RtRe",		MIPS_RSP_COP2(0x1f),		MA_RSP, MO_RSPVRSD },
	{ "vlt",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x20),		MA_RSP, 0 },
	{ "vlt",	"Rs,RtRe",		MIPS_RSP_COP2(0x20),		MA_RSP, MO_RSPVRSD },
	{ "veq",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x21),		MA_RSP, 0 },
	{ "veq",	"Rs,RtRe",		MIPS_RSP_COP2(0x21),		MA_RSP, MO_RSPVRSD },
	{ "vne",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x22),		MA_RSP, 0 },
	{ "vne",	"Rs,RtRe",		MIPS_RSP_COP2(0x22),		MA_RSP, MO_RSPVRSD },
	{ "vge",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x23),		MA_RSP, 0 },
	{ "vge",	"Rs,RtRe",		MIPS_RSP_COP2(0x23),		MA_RSP, MO_RSPVRSD },
	{ "vcl",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x24),		MA_RSP, 0 },
	{ "vcl",	"Rs,RtRe",		MIPS_RSP_COP2(0x24),		MA_RSP, MO_RSPVRSD },
	{ "vch",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x25),		MA_RSP, 0 },
	{ "vch",	"Rs,RtRe",		MIPS_RSP_COP2(0x25),		MA_RSP, MO_RSPVRSD },
	{ "vcr",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x26),		MA_RSP, 0 },
	{ "vcr",	"Rs,RtRe",		MIPS_RSP_COP2(0x26),		MA_RSP, MO_RSPVRSD },
	{ "vmrg",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x27),		MA_RSP, 0 },
	{ "vmrg",	"Rs,RtRe",		MIPS_RSP_COP2(0x27),		MA_RSP, MO_RSPVRSD },
	{ "vand",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x28),		MA_RSP, 0 },
	{ "vand",	"Rs,RtRe",		MIPS_RSP_COP2(0x28),		MA_RSP, MO_RSPVRSD },
	{ "vnand",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x29),		MA_RSP, 0 },
	{ "vnand",	"Rs,RtRe",		MIPS_RSP_COP2(0x29),		MA_RSP, MO_RSPVRSD },
	{ "vor",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x2a),		MA_RSP, 0 },
	{ "vor",	"Rs,RtRe",		MIPS_RSP_COP2(0x2a),		MA_RSP, MO_RSPVRSD },
	{ "vnor",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x2b),		MA_RSP, 0 },
	{ "vnor",	"Rs,RtRe",		MIPS_RSP_COP2(0x2b),		MA_RSP, MO_RSPVRSD },
	{ "vxor",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x2c),		MA_RSP, 0 },
	{ "vxor",	"Rs,RtRe",		MIPS_RSP_COP2(0x2c),		MA_RSP, MO_RSPVRSD },
	{ "vnxor",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x2d),		MA_RSP, 0 },
	{ "vnxor",	"Rs,RtRe",		MIPS_RSP_COP2(0x2d),		MA_RSP, MO_RSPVRSD },
	{ "vrcp",	"RdRm,RtRl",	MIPS_RSP_COP2(0x30),		MA_RSP, 0 },
	{ "vrcpl",	"RdRm,RtRl",	MIPS_RSP_COP2(0x31),		MA_RSP, 0 },
	{ "vrcph",	"RdRm,RtRl",	MIPS_RSP_COP2(0x32),		MA_RSP, 0 },
	{ "vmov",	"RdRm,RtRl",	MIPS_RSP_COP2(0x33),		MA_RSP, 0 },
	{ "vrsq",	"RdRm,RtRl",	MIPS_RSP_COP2(0x34),		MA_RSP, 0 },
	{ "vrsql",	"RdRm,RtRl",	MIPS_RSP_COP2(0x35),		MA_RSP, 0 },
	{ "vrsqh",	"RdRm,RtRl",	MIPS_RSP_COP2(0x36),		MA_RSP, 0 },
	{ "vnop",	"",				MIPS_RSP_COP2(0x37),		MA_RSP, 0 },
	{ "vextt",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x38),		MA_RSP, 0 },
	{ "vextt",	"Rs,RtRe",		MIPS_RSP_COP2(0x38),		MA_RSP, MO_RSPVRSD },
	{ "vextq",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x39),		MA_RSP, 0 },
	{ "vextq",	"Rs,RtRe",		MIPS_RSP_COP2(0x39),		MA_RSP, MO_RSPVRSD },
	{ "vextn",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x3a),		MA_RSP, 0 },
	{ "vextn",	"Rs,RtRe",		MIPS_RSP_COP2(0x3a),		MA_RSP, MO_RSPVRSD },
	{ "vinst",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x3c),		MA_RSP, 0 },
	{ "vinst",	"Rs,RtRe",		MIPS_RSP_COP2(0x3c),		MA_RSP, MO_RSPVRSD },
	{ "vinsq",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x3d),		MA_RSP, 0 },
	{ "vinsq",	"Rs,RtRe",		MIPS_RSP_COP2(0x3d),		MA_RSP, MO_RSPVRSD },
	{ "vinsn",	"Rd,Rs,RtRe",	MIPS_RSP_COP2(0x3e),		MA_RSP, 0 },
	{ "vinsn",	"Rs,RtRe",		MIPS_RSP_COP2(0x3e),		MA_RSP, MO_RSPVRSD },
	{ "vnull",	"",				MIPS_RSP_COP2(0x3f),		MA_RSP, 0 },

//     31---------26--------------------15-------11--------------------0
//     |=      LWC2|                    |   rd    |                    |
//     -----6----------------------5------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  LBV  |  LSV  |  LLV  |  LDV  |  LQV  |  LRV  |  LPV  |  LUV  | 00..07
//  01 |  LHV  |  LFV  |  LWV  |  LTV  |  ---  |  ---  |  ---  |  ---  | 08..0F
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{"lbv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x00),		MA_RSP, 0 },
	{"lbv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x00),		MA_RSP, 0 },
	{"lsv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x01),		MA_RSP, MO_RSP_HWOFFSET },
	{"lsv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x01),		MA_RSP, MO_RSP_HWOFFSET },
	{"llv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x02),		MA_RSP, MO_RSP_WOFFSET  },
	{"llv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x02),		MA_RSP, MO_RSP_WOFFSET  },
	{"ldv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x03),		MA_RSP, MO_RSP_DWOFFSET },
	{"ldv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x03),		MA_RSP, MO_RSP_DWOFFSET },
	{"lqv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x04),		MA_RSP, MO_RSP_QWOFFSET },
	{"lqv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x04),		MA_RSP, MO_RSP_QWOFFSET },
	{"lrv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x05),		MA_RSP, MO_RSP_QWOFFSET },
	{"lrv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x05),		MA_RSP, MO_RSP_QWOFFSET },
	{"lpv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x06),		MA_RSP, MO_RSP_DWOFFSET },
	{"lpv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x06),		MA_RSP, MO_RSP_DWOFFSET },
	{"luv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x07),		MA_RSP, MO_RSP_DWOFFSET },
	{"luv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x07),		MA_RSP, MO_RSP_DWOFFSET },
	{"lhv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x08),		MA_RSP, MO_RSP_QWOFFSET },
	{"lhv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x08),		MA_RSP, MO_RSP_QWOFFSET },
	{"lfv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x09),		MA_RSP, MO_RSP_QWOFFSET },
	{"lfv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x09),		MA_RSP, MO_RSP_QWOFFSET },
	{"lwv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x0a),		MA_RSP, MO_RSP_QWOFFSET },
	{"lwv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x0a),		MA_RSP, MO_RSP_QWOFFSET },
	{"ltv",		"RtRo,i7(s)",	MIPS_RSP_LWC2(0x0b),		MA_RSP, MO_RSP_QWOFFSET },
	{"ltv",		"RtRo,(s)",		MIPS_RSP_LWC2(0x0b),		MA_RSP, MO_RSP_QWOFFSET },

//     31---------26--------------------15-------11--------------------0
//     |=      SWC2|                    |   rd    |                    |
//     -----6----------------------5------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  SBV  |  SSV  |  SLV  |  SDV  |  SQV  |  SRV  |  SPV  |  SUV  | 00..07
//  01 |  SHV  |  SFV  |  SWV  |  STV  |  ---  |  ---  |  ---  |  ---  | 08..0F
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 10..17
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | 18..1F
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{"sbv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x00),		MA_RSP, 0 },
	{"sbv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x00),		MA_RSP, 0 },
	{"ssv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x01),		MA_RSP, MO_RSP_HWOFFSET },
	{"ssv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x01),		MA_RSP, MO_RSP_HWOFFSET },
	{"slv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x02),		MA_RSP, MO_RSP_WOFFSET },
	{"slv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x02),		MA_RSP, MO_RSP_WOFFSET },
	{"sdv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x03),		MA_RSP, MO_RSP_DWOFFSET },
	{"sdv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x03),		MA_RSP, MO_RSP_DWOFFSET },
	{"sqv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x04),		MA_RSP, MO_RSP_QWOFFSET },
	{"sqv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x04),		MA_RSP, MO_RSP_QWOFFSET },
	{"srv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x05),		MA_RSP, MO_RSP_QWOFFSET },
	{"srv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x05),		MA_RSP, MO_RSP_QWOFFSET },
	{"spv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x06),		MA_RSP, MO_RSP_DWOFFSET },
	{"spv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x06),		MA_RSP, MO_RSP_DWOFFSET },
	{"suv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x07),		MA_RSP, MO_RSP_DWOFFSET },
	{"suv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x07),		MA_RSP, MO_RSP_DWOFFSET },
	{"shv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x08),		MA_RSP, MO_RSP_QWOFFSET },
	{"shv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x08),		MA_RSP, MO_RSP_QWOFFSET },
	{"sfv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x09),		MA_RSP, MO_RSP_QWOFFSET },
	{"sfv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x09),		MA_RSP, MO_RSP_QWOFFSET },
	{"swv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x0a),		MA_RSP, MO_RSP_QWOFFSET },
	{"swv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x0a),		MA_RSP, MO_RSP_QWOFFSET },
	{"stv",		"RtRo,i7(s)",	MIPS_RSP_SWC2(0x0b),		MA_RSP, MO_RSP_QWOFFSET },
	{"stv",		"RtRo,(s)",		MIPS_RSP_SWC2(0x0b),		MA_RSP, MO_RSP_QWOFFSET },
	// END
	{ NULL,		NULL,			0,							0 }
};

const MipsArchDefinition mipsArchs[] = {
	// MARCH_PSX
	{ "PSX",		MA_MIPS1,							MA_EXPSX,	0 },
	// MARCH_N64
	{ "N64",		MA_MIPS1|MA_MIPS2|MA_MIPS3,			MA_EXN64,	MO_64BIT|MO_FPU|MO_DFPU },
	// MARCH_PS2
	{ "PS2",		MA_MIPS1|MA_MIPS2|MA_MIPS3|MA_PS2,	MA_EXPS2,	MO_64BIT|MO_FPU },
	// MARCH_PSP
	{ "PSP",		MA_MIPS1|MA_MIPS2|MA_MIPS3|MA_PSP,	MA_EXPSP,	MO_FPU },
	// MARCH_RSP
	{ "RSP",		MA_MIPS1|MA_RSP,					MA_EXRSP,	0 },
	// MARCH_INVALID
	{ "Invalid",	0,									0,			0 },
};
