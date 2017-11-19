#pragma once
#include "Mips.h"

#define MA_MIPS1		0x0000001
#define MA_MIPS2		0x0000002
#define MA_MIPS3		0x0000004
#define MA_MIPS4		0x0000008
#define MA_PS2			0x0000010
#define MA_PSP			0x0000020
#define MA_RSP			0x0000040

#define MA_EXPSX		0x0000100
#define MA_EXN64		0x0000200
#define MA_EXPS2		0x0000400
#define MA_EXPSP		0x0000800
#define MA_EXRSP		0x0001000

#define MO_IPCA			0x00000001	// pc >> 2
#define MO_IPCR			0x00000002	// PC, -> difference >> 2
#define MO_RSD			0x00000004	// rs = rd
#define MO_RST			0x00000008	// rs = rt
#define MO_RDT			0x00000010	// rd = rt
#define MO_DELAY		0x00000020	// delay slot follows
#define MO_NODELAYSLOT	0x00000040	// can't be in a delay slot
#define MO_DELAYRT		0x00000080	// rt won't be available for one instruction
#define MO_IGNORERTD	0x00000100	// don't care for rt delay
#define MO_FRSD			0x00000200	// float rs + rd
#define MO_IMMALIGNED	0x00000400	// immediate 4 byte aligned
#define MO_VFPU_MIXED	0x00000800	// mixed mode vfpu register
#define MO_VFPU_6BIT	0x00001000	// vfpu register can have 6 bits max
#define MO_VFPU_SINGLE	0x00002000	// single vfpu reg
#define MO_VFPU_QUAD	0x00004000	// quad vfpu reg
#define MO_VFPU			0x00008000	// vfpu type opcode
#define MO_64BIT		0x00010000	// only available on 64 bit cpus
#define MO_FPU			0x00020000	// only available with an fpu
#define MO_TRANSPOSE_VS	0x00040000	// matrix vs has to be transposed
#define MO_VFPU_PAIR	0x00080000	// pair vfpu reg
#define MO_VFPU_TRIPLE	0x00100000	// triple vfpu reg
#define MO_DFPU			0x00200000	// double-precision fpu opcodes
#define MO_RSPVRSD		0x00400000	// rsp vector rs + rd
#define MO_NEGIMM		0x00800000 	// negated immediate (for subi)
#define MO_RSP_HWOFFSET	0x01000000	// RSP halfword load/store offset
#define MO_RSP_WOFFSET	0x02000000	// RSP word load/store offset
#define MO_RSP_DWOFFSET	0x04000000	// RSP doubleword load/store offset
#define MO_RSP_QWOFFSET	0x08000000	// RSP quadword load/store offset

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
#define MIPS_COP0FUNCT(VALUE)			(MIPS_COP0(16) | MIPS_FUNC(VALUE))
#define MIPS_COP1(VALUE)				(MIPS_OP(17) | MIPS_RS(VALUE))
#define MIPS_COP1BC(VALUE)				(MIPS_COP1(8) | MIPS_RT(VALUE))
#define MIPS_COP1S(VALUE)				(MIPS_COP1(16) | MIPS_FUNC(VALUE))
#define MIPS_COP1D(VALUE)				(MIPS_COP1(17) | MIPS_FUNC(VALUE))
#define MIPS_COP1W(VALUE)				(MIPS_COP1(20) | MIPS_FUNC(VALUE))
#define MIPS_COP1L(VALUE)				(MIPS_COP1(21) | MIPS_FUNC(VALUE))

#define MIPS_VFPUSIZE(VALUE)			( (((VALUE) & 1) << 7) | (((VALUE) & 2) << 14) )
#define MIPS_VFPUFUNC(VALUE)			BITFIELD(23, 3, (VALUE))
#define MIPS_COP2(VALUE)				(MIPS_OP(18) | MIPS_RS(VALUE))
#define MIPS_COP2BC(VALUE)				(MIPS_COP2(8) | MIPS_RT(VALUE))
#define MIPS_RSP_COP2(VALUE)			(MIPS_OP(18) | (1 << 25) | MIPS_FUNC(VALUE))
#define MIPS_RSP_LWC2(VALUE)			(MIPS_OP(50) | MIPS_RD(VALUE))
#define MIPS_RSP_SWC2(VALUE)			(MIPS_OP(58) | MIPS_RD(VALUE))
#define MIPS_RSP_VE(VALUE)				BITFIELD(21, 4, (VALUE))
#define MIPS_RSP_VDE(VALUE)				BITFIELD(11, 4, (VALUE))
#define MIPS_RSP_VEALT(VALUE)			BITFIELD(7, 4, (VALUE))
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
#define MIPS_VFPU6(VALUE)				(MIPS_OP(60) | MIPS_VFPUFUNC(VALUE))
#define MIPS_VFPU6_1(VALUE)				(MIPS_VFPU6(7) | BITFIELD(20, 3, VALUE))
// This is a bit ugly, VFPU opcodes are encoded strangely.
#define MIPS_VFPU6_1VROT()				(MIPS_VFPU6(7) | BITFIELD(21, 2, 1))
#define MIPS_VFPU6_2(VALUE)				(MIPS_VFPU6_1(0) | MIPS_RT(VALUE))


struct MipsArchDefinition
{
	const char* name;
	int supportSets;
	int excludeMask;
	int flags;
};

extern const MipsArchDefinition mipsArchs[];

typedef struct {
	const char* name;
	const char* encoding;
	int destencoding;
	int archs;
	int flags;
} tMipsOpcode;

extern const tMipsOpcode MipsOpcodes[];
