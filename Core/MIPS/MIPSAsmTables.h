#pragma once

namespace MIPSAsm
{

typedef struct {
	const char *name;
	const char *encoding;
	unsigned int destencoding;
	unsigned int flags;
} tMipsOpcode;

typedef struct {
	const char *name;
	short num;
	short len;
} tMipsRegister;

#define O_RS          0x00000001 // source reg
#define O_RT          0x00000002 // target reg
#define O_RD          0x00000004 // dest reg
#define O_I16         0x00000008 // 16 bit immediate
#define O_I20         0x00000010 // 16 bit immediate
#define O_IPCA        0x00000020 // pc >> 2
#define O_IPCR        0x00000080 // PC, -> difference >> 2
#define O_I26         0x00000200 // 26 bit immediate
#define O_I5          0x00000400 // 5 bit immediate
#define O_RSD         0x00000800 // rs = rd
#define O_RST         0x00001000 // rs = rt
#define O_RDT         0x00002000 // rd = rt
#define MO_DELAY      0x00004000 // delay slot follows
#define MO_NODELAY    0x00008000 // can't be in a delay slot
#define MO_DELAYRT    0x00010000 // rt won't be available for one instruction
#define MO_IGNORERTD  0x00020000 // don't care for rt delay

#define MO_FRS        0x00040000 // float rs
#define MO_FRD        0x00080000 // float rd
#define MO_FRT        0x00100000 // float rt
#define MO_FRSD       0x00200000 // float rs + rd
#define MO_FRST       0x00400000 // float rs + rt
#define MO_FRDT       0x00800000 // float rt + rd

// TODO: Ideally we should specify the sizes thereof, matrices, etc... too many bits.
#define MO_VRS        0x01000000 // vector rs
#define MO_VRD        0x02000000 // vector rd
#define MO_VRT        0x04000000 // vector rt
#define MO_VRTI       0x08000000 // vector rt, used with load/store encodings
#define MO_VCOND      0x10000000 // vector condition
#define MO_VIMM       0x10000000 // vector 3-8 bit imm

#define BITFIELD(START,LENGTH,VALUE)	(((VALUE) & ((1 << (LENGTH)) - 1)) << (START))
#define MIPS_FUNC(VALUE)				BITFIELD(0,6,(VALUE))
#define MIPS_SA(VALUE)					BITFIELD(6,5,(VALUE))
#define MIPS_SECFUNC(VALUE)				MIPS_SA((VALUE))
#define MIPS_OP(VALUE)					BITFIELD(26,6,(VALUE))

#define MIPS_RS(VALUE)					BITFIELD(21,5,(VALUE))
#define MIPS_RT(VALUE)					BITFIELD(16,5,(VALUE))
#define MIPS_RD(VALUE)					BITFIELD(11,5,(VALUE))
#define MIPS_FS(VALUE)					MIPS_RD((VALUE))
#define MIPS_FT(VALUE)					MIPS_RT((VALUE))
#define MIPS_FD(VALUE)					MIPS_SA((VALUE))

#define MIPS_SPECIAL(VALUE)				(MIPS_OP(0) | MIPS_FUNC(VALUE))
#define MIPS_REGIMM(VALUE)				(MIPS_OP(1) | MIPS_RT(VALUE))
#define MIPS_COP0(VALUE)				(MIPS_OP(16) | MIPS_RS(VALUE))
#define MIPS_COP1(VALUE)				(MIPS_OP(17) | MIPS_RS(VALUE))
#define MIPS_COP1BC(VALUE)				(MIPS_COP1(8) | MIPS_RT(VALUE))
#define MIPS_COP1S(VALUE)				(MIPS_COP1(16) | MIPS_FUNC(VALUE))
#define MIPS_COP1W(VALUE)				(MIPS_COP1(20) | MIPS_FUNC(VALUE))

#define MIPS_VFPUSIZE(VALUE)			( (((VALUE) & 1) << 7) | (((VALUE) & 2) << 14) )
#define MIPS_VFPUFUNC(VALUE)			BITFIELD(23, 3, (VALUE))
#define MIPS_COP2(VALUE)				(MIPS_OP(18) | MIPS_RS(VALUE))
#define MIPS_COP2BC(VALUE)				(MIPS_COP2(8) | MIPS_RT(VALUE))
#define MIPS_VFPU0(VALUE)				(MIPS_OP(24) | MIPS_VFPUFUNC(VALUE))
#define MIPS_VFPU1(VALUE)				(MIPS_OP(25) | MIPS_VFPUFUNC(VALUE))
#define MIPS_VFPU3(VALUE)				(MIPS_OP(27) | MIPS_VFPUFUNC(VALUE))
#define MIPS_SPECIAL3(VALUE)			(MIPS_OP(31) | MIPS_FUNC(VALUE))
#define MIPS_ALLEGREX0(VALUE)			(MIPS_SPECIAL3(32) | MIPS_SECFUNC(VALUE))
#define MIPS_VFPU4(VALUE)				(MIPS_OP(52) | MIPS_RS(VALUE))
#define MIPS_VFPU4_11(VALUE)			(MIPS_VFPU4(0) | MIPS_RT(VALUE))
#define MIPS_VFPU4_12(VALUE)			(MIPS_VFPU4(1) | MIPS_RT(VALUE))
#define MIPS_VFPU4_13(VALUE)			(MIPS_VFPU4(2) | MIPS_RT(VALUE))
#define MIPS_VFPU5(VALUE)				(MIPS_OP(55) | MIPS_VFPUFUNC(VALUE))

#define MIPS_VFPU_ALLSIZES(name, args, code, flags) \
	{ name ".s",	args,	code | MIPS_VFPUSIZE(0), flags }, \
	{ name ".p",	args,	code | MIPS_VFPUSIZE(1), flags }, \
	{ name ".t",	args,	code | MIPS_VFPUSIZE(2), flags }, \
	{ name ".q",	args,	code | MIPS_VFPUSIZE(3), flags }

extern const tMipsRegister MipsRegister[];
extern const tMipsRegister MipsFloatRegister[];
extern const tMipsOpcode MipsOpcodes[];

}