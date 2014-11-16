#ifdef _WIN32
#include "stdafx.h"
#else
#include <stddef.h>
#endif
#include "MIPSAsmTables.h"

namespace MIPSAsm
{

const tMipsRegister MipsRegister[] = {
	{ "r0", 0, 2 }, { "zero", 0, 4}, { "$0", 0, 2 },
	{ "at", 1, 2 }, { "r1", 1, 2 }, { "$1", 1, 2 },
	{ "v0", 2, 2 }, { "r2", 2, 2 }, { "$v0", 2, 3 },
	{ "v1", 3, 2 }, { "r3", 3, 2 }, { "$v1", 3, 3 },
	{ "a0", 4, 2 }, { "r4", 4, 2 }, { "$a0", 4, 3 },
	{ "a1", 5, 2 }, { "r5", 5, 2 }, { "$a1", 5, 3 },
	{ "a2", 6, 2 }, { "r6", 6, 2 }, { "$a2", 6, 3 },
	{ "a3", 7, 2 }, { "r7", 7, 2 }, { "$a3", 7, 3 },
	{ "t0", 8, 2 }, { "r8", 8, 2 }, { "$t0", 8, 3 },
	{ "t1", 9, 2 }, { "r9", 9, 2 }, { "$t1", 9, 3 },
	{ "t2", 10, 2 }, { "r10", 10, 3 }, { "$t2", 10, 3 },
	{ "t3", 11, 2 }, { "r11", 11, 3 }, { "$t3", 11, 3 },
	{ "t4", 12, 2 }, { "r12", 12, 3 }, { "$t4", 12, 3 },
	{ "t5", 13, 2 }, { "r13", 13, 3 }, { "$t5", 13, 3 },
	{ "t6", 14, 2 }, { "r14", 14, 3 }, { "$t6", 14, 3 },
	{ "t7", 15, 2 }, { "r15", 15, 3 }, { "$t7", 15, 3 },
	{ "s0", 16, 2 }, { "r16", 16, 3 }, { "$s0", 16, 3 },
	{ "s1", 17, 2 }, { "r17", 17, 3 }, { "$s1", 17, 3 },
	{ "s2", 18, 2 }, { "r18", 18, 3 }, { "$s2", 18, 3 },
	{ "s3", 19, 2 }, { "r19", 19, 3 }, { "$s3", 19, 3 },
	{ "s4", 20, 2 }, { "r20", 20, 3 }, { "$s4", 20, 3 },
	{ "s5", 21, 2 }, { "r21", 21, 3 }, { "$s5", 21, 3 },
	{ "s6", 22, 2 }, { "r22", 22, 3 }, { "$s6", 22, 3 },
	{ "s7", 23, 2 }, { "r23", 23, 3 }, { "$s7", 23, 3 },
	{ "t8", 24, 2 }, { "r24", 24, 3 }, { "$t8", 24, 3 },
	{ "t9", 25, 2 }, { "r25", 25, 3 }, { "$t9", 25, 3 },
	{ "k0", 26, 2 }, { "r26", 26, 3 }, { "$k0", 26, 3 },
	{ "k1", 27, 2 }, { "r27", 27, 3 }, { "$k1", 27, 3 },
	{ "gp", 28, 2 }, { "r28", 28, 3 }, { "$gp", 28, 3 },
	{ "sp", 29, 2 }, { "r29", 29, 3 }, { "$sp", 29, 3 },
	{ "fp", 30, 2 }, { "r30", 30, 3 }, { "$fp", 30, 3 },
	{ "ra", 31, 2 }, { "r31", 31, 3 }, { "$ra", 31, 3 },
	{ NULL, -1, 0}
};


const tMipsRegister MipsFloatRegister[] = {
	{ "f0", 0, 2},		{ "$f0", 0, 3 },
	{ "f1", 1, 2},		{ "$f1", 1, 3 },
	{ "f2", 2, 2},		{ "$f2", 2, 3 },
	{ "f3", 3, 2},		{ "$f3", 3, 3 },
	{ "f4", 4, 2},		{ "$f4", 4, 3 },
	{ "f5", 5, 2},		{ "$f5", 5, 3 },
	{ "f6", 6, 2},		{ "$f6", 6, 3 },
	{ "f7", 7, 2},		{ "$f7", 7, 3 },
	{ "f8", 8, 2},		{ "$f8", 8, 3 },
	{ "f9", 9, 2},		{ "$f9", 9, 3 },
	{ "f10", 10, 3},	{ "$f10", 10, 4 },
	{ "f11", 11, 3},	{ "$f11", 11, 4 },
	{ "f12", 12, 3},	{ "$f12", 12, 4 },
	{ "f13", 13, 3},	{ "$f13", 13, 4 },
	{ "f14", 14, 3},	{ "$f14", 14, 4 },
	{ "f15", 15, 3},	{ "$f15", 15, 4 },
	{ "f16", 16, 3},	{ "$f16", 16, 4 },
	{ "f17", 17, 3},	{ "$f17", 17, 4 },
	{ "f18", 18, 3},	{ "$f18", 18, 4 },
	{ "f19", 19, 3},	{ "$f19", 19, 4 },
	{ "f20", 20, 3},	{ "$f20", 20, 4 },
	{ "f21", 21, 3},	{ "$f21", 21, 4 },
	{ "f22", 22, 3},	{ "$f22", 22, 4 },
	{ "f23", 23, 3},	{ "$f23", 23, 4 },
	{ "f24", 24, 3},	{ "$f24", 24, 4 },
	{ "f25", 25, 3},	{ "$f25", 25, 4 },
	{ "f26", 26, 3},	{ "$f26", 26, 4 },
	{ "f27", 27, 3},	{ "$f27", 27, 4 },
	{ "f28", 28, 3},	{ "$f28", 28, 4 },
	{ "f29", 29, 3},	{ "$f29", 29, 4 },
	{ "f30", 30, 3},	{ "$f30", 30, 4 },
	{ "f31", 31, 3},	{ "$f31", 31, 4 }
};

/* Placeholders for encoding

	s	source register
	d	destination register
	t	target register
	S	float source reg
	D	float dest reg
	T	float traget reg
	i	16 bit immediate value
	I	32 bit immediate value
	u	Shifted 16 bit immediate (upper)
	n	negative 16 bit immediate (for subi/u aliases)
	b	26 bit immediate
	a	5 bit immediate
	V   prefix for S, D, T indicating vector register
	C   vector compare condition (EQ, etc.)
*/

const tMipsOpcode MipsOpcodes[] = {
//     31---------26------------------------------------------5--------0
//     |=   SPECIAL|                                         | function|
//     ------6----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 | SLL   | ---   | SRL*1 | SRA   | SLLV  |  ---  | SRLV*2| SRAV  |
// 001 | JR    | JALR  | MOVZ  | MOVN  |SYSCALL| BREAK |  ---  | SYNC  |
// 010 | MFHI  | MTHI  | MFLO  | MTLO  | DSLLV |  ---  |   *3  |  *4   |
// 011 | MULT  | MULTU | DIV   | DIVU  | MADD  | MADDU | ----  | ----- |
// 100 | ADD   | ADDU  | SUB   | SUBU  | AND   | OR    | XOR   | NOR   |
// 101 | mfsa  | mtsa  | SLT   | SLTU  |  *5   |  *6   |  *7   |  *8   |
// 110 | TGE   | TGEU  | TLT   | TLTU  | TEQ   |  ---  | TNE   |  ---  |
// 111 | dsll  |  ---  | dsrl  | dsra  |dsll32 |  ---  |dsrl32 |dsra32 |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
// *1:	rotr when rs = 1 (PSP only)		*2:	rotrv when sa = 1 (PSP only)
// *3:	dsrlv on PS2, clz on PSP		*4:	dsrav on PS2, clo on PSP
// *5:	dadd on PS2, max on PSP			*6:	daddu on PS2, min on PSP
// *7:	dsub on PS2, msub on PSP		*8:	dsubu on PS2, msubu on PSP
	{ "sll",	"d,t,a",	0x00000000,	O_RD|O_RT|O_I5 },
	{ "sll",	"d,a",		0x00000000,	O_RDT|O_I5 },
	{ "nop",	"",			0x00000000,	0 },
	{ "srl",	"d,t,a",	0x00000002,	O_RD|O_RT|O_I5 },
	{ "srl",	"d,a",		0x00000002,	O_RDT|O_I5 },
	{ "rotr",	"d,t,a",	0x00200002,	O_RD|O_RT|O_I5 },
	{ "rotr",	"d,a",		0x00200002,	O_RDT|O_I5 },
	{ "sra",	"d,t,a",	0x00000003,	O_RD|O_RT|O_I5 },
	{ "sra",	"d,a",		0x00000003,	O_RDT|O_I5 },
	{ "sllv",	"d,t,s",	0x00000004,	O_RD|O_RT|O_RS },
	{ "sllv",	"d,s",		0x00000004,	O_RDT|O_RS },
	{ "srlv",	"d,t,s",	0x00000006,	O_RD|O_RT|O_RS },
	{ "srlv",	"d,s",		0x00000006,	O_RDT|O_RS },
	{ "rotrv",	"d,t,s",	0x00000046,	O_RD|O_RT|O_RS },
	{ "rotrv",	"d,s",		0x00000046,	O_RDT|O_RS },
	{ "srav",	"d,t,s",	0x00000007,	O_RD|O_RT|O_RS },
	{ "srav",	"d,s",		0x00000007,	O_RDT|O_RS },
	{ "jr",		"s",		0x00000008,	O_RS|MO_DELAY|MO_NODELAY },
	{ "jalr",	"s,d",		0x00000009,	O_RD|O_RS|MO_DELAY|MO_NODELAY },
	{ "jalr",	"s",		0x0000F809,	O_RS|MO_DELAY|MO_NODELAY },
	{ "movz",	"d,s,t",	0x0000000A,	O_RD|O_RT|O_RS },
	{ "movn",	"d,s,t",	0x0000000B,	O_RD|O_RT|O_RS },
	{ "syscall","b",		0x0000000c,	O_I20|MO_DELAY|MO_NODELAY },
	{ "break",	"b",		0x0000000d,	O_I20|MO_DELAY|MO_NODELAY },
	{ "sync",	"",			0x0000000f,	0 },
	{ "mfhi",	"d",		0x00000010,	O_RD },
	{ "mthi",	"s",		0x00000011,	O_RS },
	{ "mflo",	"d",		0x00000012,	O_RD },
	{ "mtlo",	"s",		0x00000013,	O_RS },
	{ "clz",	"d,s",		0x00000016,	O_RD|O_RS },
	{ "clo",	"d,s",		0x00000017,	O_RD|O_RS },
	{ "mult",	"s,t",		0x00000018,	O_RS|O_RT },
	{ "mult",	"r\x0,s,t",	0x00000018,	O_RS|O_RT },
	{ "multu",	"s,t",		0x00000019,	O_RS|O_RT },
	{ "multu",	"r\x0,s,t",	0x00000019,	O_RS|O_RT },
	{ "div",	"s,t",		0x0000001a,	O_RS|O_RT },
	{ "div",	"r\x0,s,t",	0x0000001a,	O_RS|O_RT },
	{ "divu",	"s,t",		0x0000001b,	O_RS|O_RT },
	{ "divu",	"r\x0,s,t",	0x0000001b,	O_RS|O_RT },
	{ "madd",	"s,t",		0x0000001c,	O_RS|O_RT },
	{ "maddu",	"s,t",		0x0000001d,	O_RS|O_RT },
	{ "add",	"d,s,t",	0x00000020,	O_RD|O_RS|O_RT },
	{ "add",	"s,t",		0x00000020,	O_RSD|O_RT },
	{ "addu",	"d,s,t",	0x00000021,	O_RD|O_RS|O_RT },
	{ "addu",	"s,t",		0x00000021,	O_RSD|O_RT },
	{ "move",	"d,s",		0x00000021,	O_RD|O_RS },
	{ "sub",	"d,s,t",	0x00000022,	O_RD|O_RS|O_RT },
	{ "sub",	"s,t",		0x00000022,	O_RSD|O_RT },
	{ "neg",	"d,t",		0x00000022,	O_RD|O_RT },
	{ "subu",	"d,s,t",	0x00000023,	O_RD|O_RS|O_RT },
	{ "subu",	"s,t",		0x00000023,	O_RSD|O_RT },
	{ "negu",	"d,t",		0x00000023,	O_RD|O_RT },
	{ "and",	"d,s,t",	0x00000024,	O_RD|O_RS|O_RT },
	{ "and",	"s,t",		0x00000024,	O_RSD|O_RT },
	{ "or",		"d,s,t",	0x00000025,	O_RS|O_RT|O_RD },
	{ "or",		"s,t",		0x00000025,	O_RSD|O_RT },
	{ "xor",	"d,s,t",	0x00000026,	O_RS|O_RD|O_RT },
	{ "xor",	"s,t",		0x00000026,	O_RSD|O_RT },
	{ "nor",	"d,s,t",	0x00000027,	O_RS|O_RT|O_RD },
	{ "nor",	"s,t",		0x00000027,	O_RSD|O_RT },
	{ "slt",	"d,s,t",	0x0000002a,	O_RD|O_RT|O_RS },
	{ "slt",	"s,t",		0x0000002a,	O_RSD|O_RT},
	{ "sltu",	"d,s,t",	0x0000002b,	O_RD|O_RT|O_RS },
	{ "sltu",	"s,t",		0x0000002b,	O_RSD|O_RT },
	{ "max",	"d,s,t",	0x0000002C,	O_RD|O_RT|O_RS },
	{ "min",	"d,s,t",	0x0000002D,	O_RD|O_RT|O_RS },
	{ "msub",	"s,t",		0x0000002E,	O_RS|O_RT },
	{ "msubu",	"s,t",		0x0000002F,	O_RS|O_RT },

//     REGIMM: encoded by the rt field when opcode field = REGIMM.
//     31---------26----------20-------16------------------------------0
//     |=    REGIMM|          |   rt    |                              |
//     ------6---------------------5------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | BLTZ  | BGEZ  | BLTZL | BGEZL |  ---  |  ---  |  ---  |  ---  |
//  01 | tgei  | tgeiu | tlti  | tltiu | teqi  |  ---  | tnei  |  ---  |
//  10 | BLTZAL| BGEZAL|BLTZALL|BGEZALL|  ---  |  ---  |  ---  |  ---  |
//  11 | mtsab | mtsah |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "bltz",	"s,I",		0x04000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bgez",	"s,I",		0x04010000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bltzl",	"s,I",		0x04020000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bgezl",	"s,I",		0x04030000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bltzal",	"s,I",		0x04100000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bgezal",	"s,I",		0x04110000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bltzall","s,I",		0x04120000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bgezall","s,I",		0x04130000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },

	// OPCODE 02 - J
	{ "j",		"I",		0x08000000,	O_I26|O_IPCA|MO_DELAY|MO_NODELAY },
	{ "b",		"I",		0x08000000,	O_I26|O_IPCA|MO_DELAY|MO_NODELAY },
	// OPCODE 03 - JAL
	{ "jal",	"I",		0x0c000000,	O_I26|O_IPCA|MO_DELAY|MO_NODELAY },
	// OPCODE 04 - BEQ
	{ "beq",	"s,t,I",	0x10000000,	O_RS|O_RT|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "beqz",	"s,I",		0x10000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 05 - BNE
	{ "bne",	"s,t,I",	0x14000000,	O_RS|O_RT|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bnez",	"s,I",		0x14000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 06 - BLEZ
	{ "blez",	"s,I",		0x18000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 07 - BGTZ
	{ "bgtz",	"s,I",		0x1c000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 08 - ADDI
	{ "addi",	"t,s,i",	0x20000000,	O_RT|O_RS|O_I16 },
	{ "addi",	"s,i",		0x20000000,	O_RST|O_I16 },
	// OPCODE 09 - ADDIU
	{ "addiu",	"t,s,i",	0x24000000,	O_RT|O_RS|O_I16 },
	{ "addiu",	"s,i",		0x24000000,	O_RST|O_I16 },
	{ "li",		"t,i",		0x24000000,	O_RT|O_I16 },
	// OPCODE 0A - SLTI
	{ "slti",	"t,s,i",	0x28000000,	O_RT|O_RS|O_I16 },
	{ "slti",	"s,i",		0x28000000,	O_RST|O_I16 },
	// OPCODE 0B - SLTIU
	{ "sltiu",	"t,s,i",	0x2c000000,	O_RT|O_RS|O_I16 },
	{ "sltiu",	"s,i",		0x2c000000,	O_RST|O_I16 },
	// OPCODE 0C - ANDI
	{ "andi",	"t,s,i",	0x30000000,	O_RT|O_RS|O_I16 },
	{ "andi",	"s,i",		0x30000000,	O_RST|O_I16 },
	// OPCODE 0D - ORI
	{ "ori",	"t,s,i",	0x34000000,	O_RS|O_RT|O_I16 },
	{ "ori",	"s,i",		0x34000000,	O_RST|O_I16 },
	// OPCODE 0E - XORI
	{ "xori",	"t,s,i",	0x38000000,	O_RT|O_RS|O_I16 },
	{ "xori",	"s,i",		0x38000000,	O_RST|O_I16 },
	// OPCODE 0F - LUI
	{ "lui",	"t,i",		0x3c000000,	O_RT|O_I16 },
	// OPCODE 10 - COP0
	// OPCODE 11 - COP1
	// OPCODE 12 - COP2
	// OPCODE 13 - COP3
	// OPCODE 14 - BEQL		MIPS 2
	{ "beql",	"s,t,I",	0x50000000,	O_RS|O_RT|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "beqzl",	"s,I",		0x50000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 15 - BNEZL	MIPS 2
	{ "bnel",	"s,t,I",	0x54000000,	O_RS|O_RT|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bnezl",	"s,I",		0x54000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 16 - BLEZL	MIPS 2
	{ "blezl",	"s,I",		0x58000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 17 - BGTZL	MIPS 2
	{ "bgtzl",	"s,I",		0x5c000000,	O_RS|O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	// OPCODE 18 - VFPU0
	// OPCODE 19 - VFPU1
	// OPCODE 1A - UNDEF
	// OPCODE 1B - VFPU3
	// OPCODE 1C - SPECIAL2
	// OPCODE 1D - UNDEF
	// OPCODE 1E - UNDEF
	// OPCODE 1F - SPECIAL3
	// OPCODE 20 - LB
	{ "lb",		"t,i(s)",	0x80000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lb",		"t,(s)",	0x80000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 21 - LH
	{ "lh",		"t,i(s)",	0x84000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lh",		"t,(s)",	0x84000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 22 - LWL
	{ "lwl",	"t,i(s)",	0x88000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lwl",	"t,(s)",	0x88000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 23 - LW
	{ "lw",		"t,i(s)",	0x8c000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lw",		"t,(s)",	0x8c000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 24 - LBU
	{ "lbu",	"t,i(s)",	0x90000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lbu",	"t,(s)",	0x90000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 25 - LHU
	{ "lhu",	"t,i(s)",	0x94000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "lhu",	"t,(s)",	0x94000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 26 - LWR
	{ "lwr",	"t,i(s)",	0x98000000,	O_RT|O_I16|O_RS|MO_DELAYRT|MO_IGNORERTD },
	{ "lwr",	"t,(s)",	0x98000000,	O_RT|O_RS|MO_DELAYRT|MO_IGNORERTD },
	// OPCODE 27 - UNDEF
	// OPCODE 28 - SB
	{ "sb",		"t,i(s)",	0xa0000000,	O_RT|O_I16|O_RS },
	{ "sb",		"t,(s)",	0xa0000000,	O_RT|O_RS },
	// OPCODE 29 - SH
	{ "sh",		"t,i(s)",	0xa4000000,	O_RT|O_I16|O_RS },
	{ "sh",		"t,(s)",	0xa4000000,	O_RT|O_RS },
	// OPCODE 2A - SWL
	{ "swl",	"t,i(s)",	0xa8000000,	O_RT|O_I16|O_RS },
	{ "swl",	"t,(s)",	0xa8000000,	O_RT|O_RS },
	// OPCODE 2B - SW
	{ "sw",		"t,i(s)",	0xac000000,	O_RT|O_I16|O_RS },
	{ "sw",		"t,(s)",	0xac000000,	O_RT|O_RS },
	// OPCODE 2C - UNDEF
	// OPCODE 2D - UNDEF
	// OPCODE 2E - SWR
	{ "swr",	"t,i(s)",	0xb8000000,	O_RT|O_I16|O_RS },
	{ "swr",	"t,(s)",	0xb8000000,	O_RT|O_RS },
	// OPCODE 2F - CACHE
	// OPCODE 30 - LL
	{ "ll",		"t,i(s)",	0xc0000000,	O_RT|O_I16|O_RS|MO_DELAYRT },
	{ "ll",		"t,(s)",	0xc0000000,	O_RT|O_RS|MO_DELAYRT },
	// OPCODE 31 - LWC1
	{ "lwc1",	"T,i(s)",	0xc4000000, MO_FRT|O_I16|O_RS },
	{ "lwc1",	"T,(s)",	0xc4000000, MO_FRT|O_RS },
	// OPCODE 32 - LVS - VVVVVV SSSSS TTTTT IIIIIIIIIIIIII TT
	{ "lwc2",	"VT,i(s)",	0xc8000000, MO_VRTI|O_I16|O_RS },
	{ "lwc2",	"VT,(s)",	0xc8000000, MO_VRTI|O_RS },
	{ "lv.s",	"VT,i(s)",	0xc8000000, MO_VRTI|O_I16|O_RS },
	{ "lv.s",	"VT,(s)",	0xc8000000, MO_VRTI|O_RS },
	// OPCODE 33 - LWC3
	// OPCODE 34 - VFPU4
	// OPCODE 35 - ULVQ - VVVVVV SSSSS TTTTT IIIIIIIIIIIIII TT (WARNING: reportedly buggy on PSP-1000)
	{ "ulv.q",	"VT,i(s)",	0xd4000000, MO_VRTI|O_I16|O_RS },
	{ "lvl.q",	"VT,i(s)",	0xd4000000, MO_VRTI|O_I16|O_RS },
	{ "lvr.q",	"VT,i(s)",	0xd4000002, MO_VRTI|O_I16|O_RS },
	// OPCODE 36 - LVQ
	{ "lv.q",	"VT,i(s)",	0xd8000000, MO_VRTI|O_I16|O_RS },
	{ "lv.q",	"VT,(s)",	0xd8000000, MO_VRTI|O_RS },
	// OPCODE 37 - VFPU5
	// OPCODE 38 - SC
	{ "sc",	"t,i(s)",		0xe0000000,	O_RT|O_I16|O_RS },
	{ "sc",	"t,(s)",		0xe0000000,	O_RT|O_RS },
	// OPCODE 39 - SWC1
	{ "swc1",	"T,i(s)",	0xe4000000, MO_FRT|O_I16|O_RS },
	{ "swc1",	"T,(s)",	0xe4000000, MO_FRT|O_I16|O_RS },
	// OPCODE 3A - SVS - VVVVVV SSSSS TTTTT IIIIIIIIIIIIII TT
	{ "swc2",	"VT,i(s)",	0xe8000000, MO_VRTI|O_I16|O_RS },
	{ "swc2",	"VT,(s)",	0xe8000000, MO_VRTI|O_RS },
	{ "sv.s",	"VT,i(s)",	0xe8000000, MO_VRTI|O_I16|O_RS },
	{ "sv.s",	"VT,(s)",	0xe8000000, MO_VRTI|O_RS },
	// OPCODE 3B - SWC3
	// OPCODE 3C - VFPU6
	// OPCODE 3D - USVQ - VVVVVV SSSSS TTTTT IIIIIIIIIIIIII TT
	{ "usv.q",	"VT,i(s)",	0xf4000000, MO_VRTI|O_I16|O_RS },
	{ "svl.q",	"VT,i(s)",	0xf4000000, MO_VRTI|O_I16|O_RS },
	{ "svr.q",	"VT,i(s)",	0xf4000002, MO_VRTI|O_I16|O_RS },
	// OPCODE 3E - SVQ - VVVVVV SSSSS TTTTT IIIIIIIIIIIIII TT
	{ "sv.q",	"VT,i(s)",	0xf8000000, MO_VRTI|O_I16|O_RS },
	{ "sv.q",	"VT,(s)",	0xf8000000, MO_VRTI|O_RS },
	// OPCODE 3F - VFPU7


//     31-------26------21---------------------------------------------0
//     |=    COP1|  rs  |                                              |
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  MFC1 |  ---  |  CFC1 |  ---  |  MTC1 |  ---  |  CTC1 |  ---  |
//  01 |  BC*  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 |  S*   |  ---  |  ---  |  ---  |  W*   |  ---  |  ---  |  ---  |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "mfc1",	"t,S",		0x44000000,	O_RT|MO_FRS },
	{ "cfc1",	"t,S",		0x44400000,	O_RT|MO_FRS },
	{ "mtc1",	"t,S",		0x44800000,	O_RT|MO_FRS },
	{ "ctc1",	"t,S",		0x44C00000,	O_RT|MO_FRS },

//     31---------21-------16------------------------------------------0
//     |=    COP1BC|  rt   |                                           |
//     ------11---------5-----------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  BC1F | BC1T  | BC1FL | BC1TL |  ---  |  ---  |  ---  |  ---  |
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "bc1f",	"I",		0x45000000,	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bc1t",	"I",		0x45010000,	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bc1fl",	"I",		0x45020000,	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bc1tl",	"I",		0x45030000,	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },

//     31---------21------------------------------------------5--------0
//     |=  COP1S  |                                          | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  add  |  sub  |  mul  |  div  | sqrt  |  abs  |  mov  |  neg  |
// 001 |  ---  |  ---  |  ---  |  ---  |round.w|trunc.w|ceil.w |floor.w|
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  | rsqrt |  ---  |
// 011 |  adda |  suba | mula  |  ---  | madd  |  msub | madda | msuba |
// 100 |  ---  |  ---  |  ---  |  ---  | cvt.w |  ---  |  ---  |  ---  |
// 101 |  max  |  min  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 110 |  c.f  | c.un  | c.eq  | c.ueq |c.(o)lt| c.ult |c.(o)le| c.ule |
// 110 |  c.sf | c.ngle| c.seq | c.ngl | c.lt  | c.nge | c.le  | c.ngt |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "add.s",		"D,S,T",	0x46000000,	MO_FRT|MO_FRD|MO_FRS },
	{ "add.s",		"S,T",		0x46000000,	MO_FRT|MO_FRSD },
	{ "sub.s",		"D,S,T",	0x46000001,	MO_FRT|MO_FRD|MO_FRS },
	{ "sub.s",		"S,T",		0x46000001,	MO_FRT|MO_FRSD },
	{ "mul.s",		"D,S,T",	0x46000002,	MO_FRT|MO_FRD|MO_FRS },
	{ "mul.s",		"S,T",		0x46000002,	MO_FRT|MO_FRSD },
	{ "div.s",		"D,S,T",	0x46000003,	MO_FRT|MO_FRD|MO_FRS },
	{ "div.s",		"S,T",		0x46000003,	MO_FRT|MO_FRSD },
	{ "sqrt.s",		"D,S",		0x46000004,	MO_FRD|MO_FRS },
	{ "abs.s",		"D,S",		0x46000005,	MO_FRD|MO_FRS },
	{ "mov.s",		"D,S",		0x46000006,	MO_FRD|MO_FRS },
	{ "neg.s",		"D,S",		0x46000007,	MO_FRD|MO_FRS },
	{ "round.w.s",	"D,S",		0x4600000C,	MO_FRD|MO_FRS },
	{ "trunc.w.s",	"D,S",		0x4600000D,	MO_FRD|MO_FRS },
	{ "ceil.w.s",	"D,S",		0x4600000E,	MO_FRD|MO_FRS },
	{ "floor.w.s",	"D,S",		0x4600000F,	MO_FRD|MO_FRS },
	{ "cvt.w.s",	"D,S",		0x46000024,	MO_FRD|MO_FRS },
	{ "c.f.s",		"S,T",		0x46000030,	MO_FRT|MO_FRS },
	{ "c.un.s",		"S,T",		0x46000031,	MO_FRT|MO_FRS },
	{ "c.eq.s",		"S,T",		0x46000032,	MO_FRT|MO_FRS },
	{ "c.ueq.s",	"S,T",		0x46000033,	MO_FRT|MO_FRS },
	{ "c.olt.s",	"S,T",		0x46000034,	MO_FRT|MO_FRS },
	{ "c.ult.s",	"S,T",		0x46000035,	MO_FRT|MO_FRS },
	{ "c.ole.s",	"S,T",		0x46000036,	MO_FRT|MO_FRS },
	{ "c.ule.s",	"S,T",		0x46000037,	MO_FRT|MO_FRS },
	{ "c.sf.s",		"S,T",		0x46000038,	MO_FRT|MO_FRS },
	{ "c.ngle.s",	"S,T",		0x46000039,	MO_FRT|MO_FRS },
	{ "c.seq.s",	"S,T",		0x4600003A,	MO_FRT|MO_FRS },
	{ "c.ngl.s",	"S,T",		0x4600003B,	MO_FRT|MO_FRS },
	{ "c.lt.s",		"S,T",		0x4600003C,	MO_FRT|MO_FRS },
	{ "c.nge.s",	"S,T",		0x4600003D,	MO_FRT|MO_FRS },
	{ "c.le.s",		"S,T",		0x4600003E,	MO_FRT|MO_FRS },
	{ "c.ngt.s",	"S,T",		0x4600003F,	MO_FRT|MO_FRS },

//     COP1W: encoded by function field
//     31---------21------------------------------------------5--------0
//     |=  COP1W  |                                          | function|
//     -----11----------------------------------------------------6-----
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
// 000 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 001 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 010 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 011 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 100 |cvt.s.w|  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 101 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
// 110 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	{ "cvt.s.w",	"D,S",		0x46800020,	MO_FRD|MO_FRS },

//     31-------26------21---------------------------------------------0
//     |=    COP2|  rs  |                                              |
//     -----6-------5---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  ---  |  ---  |  ---  |  MFV  |  ---  |  ---  |  ---  |  MTV  |
//  01 |  BC*  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVVVV ttttt -------- C DDDDDDD
	{ "mfv",	"t,VD",		MIPS_COP2(3), MO_VRD|O_RT },
	{ "mfvc",	"t,i",		MIPS_COP2(3) | 0x80, O_I16|O_RT },
	{ "mtv",	"t,VD",		MIPS_COP2(7), MO_VRD|O_RT },
	{ "mtvc",	"t,i",		MIPS_COP2(7) | 0x80, O_I16|O_RT },

//     COP2BC: ? indicates any, * indicates all
//     31---------21-------16------------------------------------------0
//     |=    COP2BC|  rt   |                                           |
//     ------11---------5-----------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 |  BVFx |  BVTx | BVFLx | BVTLx |  BVFy |  BVTy | BVFLy | BVTLy |
//  01 |  BVFz |  BVTz | BVFLz | BVTLz |  BVFw |  BVTw | BVFLw | BVTLw |
//  10 |  BVF? |  BVT? | BVFL? | BVTL? |  BVF* |  BVT* | BVFL* | BVTL* |
//  11 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  hi |-------|-------|-------|-------|-------|-------|-------|-------|
	// TODO: Multiple immediates (format: bvf 0, label)...?  We'll just use .x for now.
	// VVVVVV VVVVV iii L C IIIIIIIIIIIIIIII
	{ "bvf.x",		"I",		MIPS_COP2BC(0x00),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.x",		"I",		MIPS_COP2BC(0x01),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.x",		"I",		MIPS_COP2BC(0x02),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.x",		"I",		MIPS_COP2BC(0x03),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvf.y",		"I",		MIPS_COP2BC(0x04),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.y",		"I",		MIPS_COP2BC(0x05),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.y",		"I",		MIPS_COP2BC(0x06),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.y",		"I",		MIPS_COP2BC(0x07),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvf.z",		"I",		MIPS_COP2BC(0x08),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.z",		"I",		MIPS_COP2BC(0x09),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.z",		"I",		MIPS_COP2BC(0x0a),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.z",		"I",		MIPS_COP2BC(0x0b),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvf.w",		"I",		MIPS_COP2BC(0x0c),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.w",		"I",		MIPS_COP2BC(0x0d),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.w",		"I",		MIPS_COP2BC(0x0e),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.w",		"I",		MIPS_COP2BC(0x0f),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvf.any",	"I",		MIPS_COP2BC(0x10),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.any",	"I",		MIPS_COP2BC(0x11),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.any",	"I",		MIPS_COP2BC(0x12),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.any",	"I",		MIPS_COP2BC(0x13),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvf.all",	"I",		MIPS_COP2BC(0x14),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvt.all",	"I",		MIPS_COP2BC(0x15),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvfl.all",	"I",		MIPS_COP2BC(0x16),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },
	{ "bvtl.all",	"I",		MIPS_COP2BC(0x17),	O_I16|O_IPCR|MO_DELAY|MO_NODELAY },

//     31-------26-----23----------------------------------------------0
//     |=   VFPU0|  f  |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
//     |  VADD |  VSUB |  VSBN |  ---  |  ---  |  ---  |  ---  |  VDIV |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVV TTTTTTT z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vadd", "VD,VS,VT", MIPS_VFPU0(0), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vsub", "VD,VS,VT", MIPS_VFPU0(1), MO_VRD|MO_VRS|MO_VRT),
	// Including encodings that may work but aren't technically valid/sane for easy testing.
	// TODO: Could remove or warn?
	MIPS_VFPU_ALLSIZES("vsbn", "VD,VS,VT", MIPS_VFPU0(2), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vdiv", "VD,VS,VT", MIPS_VFPU0(7), MO_VRD|MO_VRS|MO_VRT),

//     31-------26-----23----------------------------------------------0
//     |=   VFPU1|  f  |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
//     |  VMUL |  VDOT |  VSCL |  ---  |  VHDP |  VDET |  VCRS |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVV TTTTTTT z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vmul", "VD,VS,VT", MIPS_VFPU1(0), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vdot", "VD,VS,VT", MIPS_VFPU1(1), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vscl", "VD,VS,VT", MIPS_VFPU1(2), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vhdp", "VD,VS,VT", MIPS_VFPU1(4), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vdet", "VD,VS,VT", MIPS_VFPU1(5), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vcrs", "VD,VS,VT", MIPS_VFPU1(6), MO_VRD|MO_VRS|MO_VRT),

//     31-------26-----23----------------------------------------------0
//     |=   VFPU3|  f  |                                               |
//     -----6-------3---------------------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--|
//     |  VCMP |  ---  |  VMIN |  VMAX |  ---  | VSCMP |  VSGE |  VSLT |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVV TTTTTTT z SSSSSSS z --- CCCC
	MIPS_VFPU_ALLSIZES("vcmp", "C,VS,VT", MIPS_VFPU3(0), MO_VRS|MO_VRT|MO_VCOND),
	// VVVVVV VVV TTTTTTT z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vmin", "VD,VS,VT", MIPS_VFPU3(2), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vmax", "VD,VS,VT", MIPS_VFPU3(3), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vscmp", "VD,VS,VT", MIPS_VFPU3(5), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vsge", "VD,VS,VT", MIPS_VFPU3(6), MO_VRD|MO_VRS|MO_VRT),
	MIPS_VFPU_ALLSIZES("vslt", "VD,VS,VT", MIPS_VFPU3(7), MO_VRD|MO_VRS|MO_VRT),

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
	// VVVVVV sssss ttttt zzzzz ppppp VVVVVV
	// TODO: This has two immediates, size (z) and pos (p)...

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
	{ "wsbh",	"d,t",			MIPS_ALLEGREX0(0x02), O_RD|O_RT },
	{ "wsbh",	"d",			MIPS_ALLEGREX0(0x02), O_RDT },
	{ "wsbw",	"d,t",			MIPS_ALLEGREX0(0x03), O_RD|O_RT },
	{ "wsbw",	"d",			MIPS_ALLEGREX0(0x03), O_RDT },
	{ "seb",	"d,t",			MIPS_ALLEGREX0(0x10), O_RD|O_RT },
	{ "seb",	"d",			MIPS_ALLEGREX0(0x10), O_RDT },
	{ "bitrev",	"d,t",			MIPS_ALLEGREX0(0x14), O_RD|O_RT },
	{ "bitrev",	"d",			MIPS_ALLEGREX0(0x14), O_RDT },
	{ "seh",	"d,t",			MIPS_ALLEGREX0(0x18), O_RD|O_RT },
	{ "seh",	"d",			MIPS_ALLEGREX0(0x18), O_RDT },

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
	MIPS_VFPU_ALLSIZES("vcst", "VD,i", MIPS_VFPU4(0x03), MO_VRD|MO_VIMM),
	// VVVVVV VVVVV iiiii z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vf2in", "VD,VS,i", MIPS_VFPU4(0x10), MO_VRD|MO_VRS|MO_VIMM),
	MIPS_VFPU_ALLSIZES("vf2iz", "VD,VS,i", MIPS_VFPU4(0x11), MO_VRD|MO_VRS|MO_VIMM),
	MIPS_VFPU_ALLSIZES("vf2iu", "VD,VS,i", MIPS_VFPU4(0x12), MO_VRD|MO_VRS|MO_VIMM),
	MIPS_VFPU_ALLSIZES("vf2id", "VD,VS,i", MIPS_VFPU4(0x13), MO_VRD|MO_VRS|MO_VIMM),
	MIPS_VFPU_ALLSIZES("vi2f", "VD,VS,i", MIPS_VFPU4(0x14), MO_VRD|MO_VRS|MO_VIMM),
	// VVVVVV VVVVV - C iii z SSSSSSS z DDDDDDD
	// This is arguably a horrible syntax but it's the common one.
	MIPS_VFPU_ALLSIZES("vcmovt", "VD,VS,i", MIPS_VFPU4(0x15) | 0x00000000, MO_VRD|MO_VRS|MO_VIMM),
	MIPS_VFPU_ALLSIZES("vcmovf", "VD,VS,i", MIPS_VFPU4(0x15) | 0x00080000, MO_VRD|MO_VRS|MO_VIMM),
	// VVVVVV VV iiiiiiii z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vwbn", "VD,VS,i", MIPS_VFPU4(0x18), MO_VRD|MO_VRS|MO_VIMM),

//     31-------------21-------16--------------------------------------0
//     |= VF4-1.1      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | VMOV  | VABS  | VNEG  | VIDT  | VSAT0 | VSAT1 | VZERO | VONE  |
//  01 |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 | VRCP  | VRSQ  | VSIN  | VCOS  | VEXP2 | VLOG2 | VSQRT | VASIN |
//  11 | VNRCP |  ---  | VNSIN |  ---  |VREXP2 |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVVVV VVVVV z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vmov", "VD,VS", MIPS_VFPU4_11(0x00), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vabs", "VD,VS", MIPS_VFPU4_11(0x01), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vneg", "VD,VS", MIPS_VFPU4_11(0x02), MO_VRD|MO_VRS),
	// VVVVVV VVVVV VVVVV z ------- z DDDDDDD
	MIPS_VFPU_ALLSIZES("vidt", "VD", MIPS_VFPU4_11(0x03), MO_VRD),
	// VVVVVV VVVVV VVVVV z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vsat0", "VD,VS", MIPS_VFPU4_11(0x04), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsat1", "VD,VS", MIPS_VFPU4_11(0x05), MO_VRD|MO_VRS),
	// VVVVVV VVVVV VVVVV z ------- z DDDDDDD
	MIPS_VFPU_ALLSIZES("vzero", "VD", MIPS_VFPU4_11(0x06), MO_VRD),
	MIPS_VFPU_ALLSIZES("vone", "VD", MIPS_VFPU4_11(0x07), MO_VRD),
	// VVVVVV VVVVV VVVVV z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vrcp", "VD,VS", MIPS_VFPU4_11(0x10), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vrsq", "VD,VS", MIPS_VFPU4_11(0x11), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsin", "VD,VS", MIPS_VFPU4_11(0x12), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vcos", "VD,VS", MIPS_VFPU4_11(0x13), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vexp2", "VD,VS", MIPS_VFPU4_11(0x14), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vlog2", "VD,VS", MIPS_VFPU4_11(0x15), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsqrt", "VD,VS", MIPS_VFPU4_11(0x16), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vasin", "VD,VS", MIPS_VFPU4_11(0x17), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vnrcp", "VD,VS", MIPS_VFPU4_11(0x18), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vnsin", "VD,VS", MIPS_VFPU4_11(0x1a), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vrexp2", "VD,VS", MIPS_VFPU4_11(0x1c), MO_VRD|MO_VRS),

//     VFPU4 1.2: TODO: Unsure where VSBZ goes, no one uses it.
//     31-------------21-------16--------------------------------------0
//     |= VF4-1.2      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | VRNDS | VRNDI |VRNDF1 |VRNDF2 |  ---  |  ---  |  ---  |  ---  |
//  01 |  ---  |  ---  |  ---  |  ---  | VSBZ? |  ---  |  ---  |  ---  |
//  10 |  ---  |  ---  | VF2H  | VH2F  |  ---  |  ---  | VSBZ? | VLGB  |
//  11 | VUC2I | VC2I  | VUS2I | VS2I  | VI2UC | VI2C  | VI2US | VI2S  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVVVV VVVVV z ------- z DDDDDDD
	MIPS_VFPU_ALLSIZES("vrnds", "VD", MIPS_VFPU4_12(0x00), MO_VRD),
	MIPS_VFPU_ALLSIZES("vrndi", "VD", MIPS_VFPU4_12(0x01), MO_VRD),
	MIPS_VFPU_ALLSIZES("vrndf1", "VD", MIPS_VFPU4_12(0x02), MO_VRD),
	MIPS_VFPU_ALLSIZES("vrndf2", "VD", MIPS_VFPU4_12(0x03), MO_VRD),
	// VVVVVV VVVVV VVVVV z SSSSSSS z DDDDDDD
	// TODO: VSBZ?
	MIPS_VFPU_ALLSIZES("vf2h", "VD,VS", MIPS_VFPU4_12(0x12), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vh2f", "VD,VS", MIPS_VFPU4_12(0x13), MO_VRD|MO_VRS),
	// TODO: VSBZ?
	MIPS_VFPU_ALLSIZES("vlgb", "VD,VS", MIPS_VFPU4_12(0x17), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vuc2i", "VD,VS", MIPS_VFPU4_12(0x18), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vc2i", "VD,VS", MIPS_VFPU4_12(0x19), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vus2i", "VD,VS", MIPS_VFPU4_12(0x1a), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vs2i", "VD,VS", MIPS_VFPU4_12(0x1b), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vi2uc", "VD,VS", MIPS_VFPU4_12(0x1c), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vi2c", "VD,VS", MIPS_VFPU4_12(0x1d), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vi2us", "VD,VS", MIPS_VFPU4_12(0x1e), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vi2s", "VD,VS", MIPS_VFPU4_12(0x1f), MO_VRD|MO_VRS),

//     31--------------21------16--------------------------------------0
//     |= VF4-1.3      |   rt  |                                       |
//     --------11----------5--------------------------------------------
//  hi |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//  00 | VSRT1 | VSRT2 | VBFY1 | VBFY2 | VOCP  | VSOCP | VFAD  | VAVG  |
//  01 | VSRT3 | VSRT4 | VSGN  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  10 | VMFVC | VMTVC |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |
//  11 |  ---  |VT4444 |VT5551 |VT5650 |  ---  |  ---  |  ---  |  ---  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VVVVV VVVVV z SSSSSSS z DDDDDDD
	MIPS_VFPU_ALLSIZES("vsrt1", "VD,VS", MIPS_VFPU4_13(0x00), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsrt2", "VD,VS", MIPS_VFPU4_13(0x01), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vbfy1", "VD,VS", MIPS_VFPU4_13(0x02), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vbfy2", "VD,VS", MIPS_VFPU4_13(0x03), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vocp", "VD,VS", MIPS_VFPU4_13(0x04), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsocp", "VD,VS", MIPS_VFPU4_13(0x05), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vfad", "VD,VS", MIPS_VFPU4_13(0x06), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vavg", "VD,VS", MIPS_VFPU4_13(0x07), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsrt3", "VD,VS", MIPS_VFPU4_13(0x08), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsrt4", "VD,VS", MIPS_VFPU4_13(0x09), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vsgn", "VD,VS", MIPS_VFPU4_13(0x0a), MO_VRD|MO_VRS),
	// VVVVVV VVVVV VVVVV z SSSSSSS iiiiiiii
	// TODO: Actually a 7-bit imm (top bit is control register flag.)
	// TODO: Not 100% sure about syntax, or if vmfv is even valid.
	MIPS_VFPU_ALLSIZES("vmfv", "VS,i", MIPS_VFPU4_13(0x10) | 0x00, MO_VRS|O_I16),
	MIPS_VFPU_ALLSIZES("vmtv", "VS,i", MIPS_VFPU4_13(0x11) | 0x00, MO_VRD|O_I16),
	MIPS_VFPU_ALLSIZES("vmfvc", "VS,i", MIPS_VFPU4_13(0x10) | 0x80, MO_VRS|O_I16),
	MIPS_VFPU_ALLSIZES("vmtvc", "VS,i", MIPS_VFPU4_13(0x11) | 0x80, MO_VRD|O_I16),

	MIPS_VFPU_ALLSIZES("vt4444", "VD,VS", MIPS_VFPU4_13(0x19), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vt5551", "VD,VS", MIPS_VFPU4_13(0x1a), MO_VRD|MO_VRS),
	MIPS_VFPU_ALLSIZES("vt5650", "VD,VS", MIPS_VFPU4_13(0x1b), MO_VRD|MO_VRS),

//     31-------26-----23----------------------------------------------0
//     |=   VFPU5|  f  |                                               |
//     -----6-------3---------00----------------------------------------
//     |--000--|--001--|--010--|--011--|--100--|--101--|--110--|--111--| lo
//     | VPFXS | VPFXS | VPFXT | VPFXT | VPFXD | VPFXD | VIIM  | VFIM  |
//     |-------|-------|-------|-------|-------|-------|-------|-------|
	// VVVVVV VV -------- iiiiiiiiiiiiiiii
	// TODO: This is a whole world of complication for proper syntax.  Will allow integer directly for now.
	{ "vpfxs",	"i",			MIPS_VFPU5(0), O_I16 },
	{ "vpfxt",	"i",			MIPS_VFPU5(2), O_I16 },
	{ "vpfxd",	"i",			MIPS_VFPU5(4), O_I16 },
	// VVVVVV VVV TTTTTTT iiiiiiiiiiiiiiii
	{ "viim.s",	"VT,i",			MIPS_VFPU5(6), MO_VRT|O_I16 },
	// TODO: This should actually encode a half-float, not integer...
	{ "vfim.s",	"VT,i",			MIPS_VFPU5(7), MO_VRT|O_I16 },


	// END
	{ NULL,		NULL,		0,			0 }
};

}
