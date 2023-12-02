// Copyright (c) 2022- PPSSPP Project.

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

#include "ppsspp_config.h"
#include <algorithm>
#include <cstring>
#if PPSSPP_ARCH(RISCV64) && PPSSPP_PLATFORM(LINUX)
#include <sys/cachectl.h>
#endif
#include "Common/BitScan.h"
#include "Common/CPUDetect.h"
#include "Common/RiscVEmitter.h"

namespace RiscVGen {

static inline bool SupportsCompressed(char zcx = '\0') {
	if (!cpu_info.RiscV_C)
		return false;

	switch (zcx) {
	case 'b': return cpu_info.RiscV_Zcb;
	case '\0': return true;
	default: return false;
	}
}

static inline uint8_t BitsSupported() {
	return cpu_info.OS64bit ? 64 : 32;
}

static inline uint8_t FloatBitsSupported() {
	if (cpu_info.RiscV_D)
		return 64;
	if (cpu_info.RiscV_F)
		return 32;
	return 0;
}

static inline bool SupportsMulDiv(bool allowZmmul = false) {
	// TODO allowZmmul?
	return cpu_info.RiscV_M;
}

static inline bool SupportsAtomic() {
	return cpu_info.RiscV_A;
}

static inline bool SupportsZicsr() {
	return cpu_info.RiscV_Zicsr;
}

static inline bool SupportsVector() {
	return cpu_info.RiscV_V;
}

static inline bool SupportsVectorBitmanip(char zvxb) {
	switch (zvxb) {
	case 'b': return cpu_info.RiscV_Zvbb;
	case 'k': return cpu_info.RiscV_Zvkb;
	default: return false;
	}
}

static inline bool SupportsBitmanip(char zbx) {
	switch (zbx) {
	case 'a': return cpu_info.RiscV_Zba;
	case 'b': return cpu_info.RiscV_Zbb;
	case 'c': return cpu_info.RiscV_Zbc;
	case 's': return cpu_info.RiscV_Zbs;
	default: return false;
	}
}

static inline bool SupportsIntConditional() {
	return cpu_info.RiscV_Zicond;
}

static inline bool SupportsFloatHalf(bool allowMin = false) {
	return cpu_info.RiscV_Zfh || (cpu_info.RiscV_Zfhmin && allowMin);
}

static inline bool SupportsFloatExtra() {
	return cpu_info.RiscV_Zfa;
}

enum class Opcode32 {
	// Note: invalid, just used for FixupBranch.
	ZERO = 0b0000000,
	LOAD = 0b0000011,
	LOAD_FP = 0b0000111,
	MISC_MEM = 0b0001111,
	OP_IMM = 0b0010011,
	AUIPC = 0b0010111,
	OP_IMM_32 = 0b0011011,
	STORE = 0b0100011,
	STORE_FP = 0b0100111,
	AMO = 0b0101111,
	OP = 0b0110011,
	LUI = 0b0110111,
	OP_32 = 0b0111011,
	FMADD = 0b1000011,
	FMSUB = 0b1000111,
	FNMSUB = 0b1001011,
	FNMADD = 0b1001111,
	OP_FP = 0b1010011,
	OP_V = 0b1010111,
	BRANCH = 0b1100011,
	JALR = 0b1100111,
	JAL = 0b1101111,
	SYSTEM = 0b1110011,
};

enum class Opcode16 {
	C0 = 0b00,
	C1 = 0b01,
	C2 = 0b10,
};

enum class Funct3 {
	// Note: invalid, just used for FixupBranch.
	ZERO = 0b000,

	PRIV = 0b000,

	FENCE = 0b000,
	FENCE_I = 0b001,

	BEQ = 0b000,
	BNE = 0b001,
	BLT = 0b100,
	BGE = 0b101,
	BLTU = 0b110,
	BGEU = 0b111,

	LS_B = 0b000,
	LS_H = 0b001,
	LS_W = 0b010,
	LS_D = 0b011,
	LS_BU = 0b100,
	LS_HU = 0b101,
	LS_WU = 0b110,

	ADD = 0b000,
	SLL = 0b001,
	SLT = 0b010,
	SLTU = 0b011,
	XOR = 0b100,
	SRL = 0b101,
	OR = 0b110,
	AND = 0b111,

	MUL = 0b000,
	MULH = 0b001,
	MULHSU = 0b010,
	MULHU = 0b011,
	DIV = 0b100,
	DIVU = 0b101,
	REM = 0b110,
	REMU = 0b111,

	FSGNJ = 0b000,
	FSGNJN = 0b001,
	FSGNJX = 0b010,

	FMIN = 0b000,
	FMAX = 0b001,
	FMINM = 0b010,
	FMAXM = 0b011,

	FMV = 0b000,
	FCLASS = 0b001,

	FLE = 0b000,
	FLT = 0b001,
	FEQ = 0b010,

	CSRRW = 0b001,
	CSRRS = 0b010,
	CSRRC = 0b011,
	CSRRWI = 0b101,
	CSRRSI = 0b110,
	CSRRCI = 0b111,

	OPIVV = 0b000,
	OPFVV = 0b001,
	OPMVV = 0b010,
	OPIVI = 0b011,
	OPIVX = 0b100,
	OPFVF = 0b101,
	OPMVX = 0b110,
	OPCFG = 0b111,

	VLS_8 = 0b000,
	VLS_16 = 0b101,
	VLS_32 = 0b110,
	VLS_64 = 0b111,

	CLMUL = 0b001,
	CLMULR = 0b010,
	CLMULH = 0b011,
	MIN = 0b100,
	MINU = 0b101,
	MAX = 0b110,
	MAXU = 0b111,

	SH1ADD = 0b010,
	SH2ADD = 0b100,
	SH3ADD = 0b110,

	COUNT_SEXT_ROL = 0b001,
	ZEXT = 0b100,
	ROR = 0b101,

	BSET = 0b001,
	BEXT = 0b101,

	CZERO_EQZ = 0b101,
	CZERO_NEZ = 0b111,

	C_ADDI4SPN = 0b000,
	C_FLD = 0b001,
	C_LW = 0b010,
	C_FLW = 0b011,
	C_LD = 0b011,
	C_FSD = 0b101,
	C_SW = 0b110,
	C_FSW = 0b111,
	C_SD = 0b111,

	C_ADDI = 0b000,
	C_JAL = 0b001,
	C_ADDIW = 0b001,
	C_LI = 0b010,
	C_LUI = 0b011,
	C_ARITH = 0b100,
	C_J = 0b101,
	C_BEQZ = 0b110,
	C_BNEZ = 0b111,

	C_SLLI = 0b000,
	C_FLDSP = 0b001,
	C_LWSP = 0b010,
	C_FLWSP = 0b011,
	C_LDSP = 0b011,
	C_ADD = 0b100,
	C_FSDSP = 0b101,
	C_SWSP = 0b110,
	C_FSWSP = 0b111,
	C_SDSP = 0b111,
};

enum class Funct2 {
	S = 0b00,
	D = 0b01,
	H = 0b10,
	Q = 0b11,

	C_SRLI = 0b00,
	C_SRAI = 0b01,
	C_ANDI = 0b10,
	C_REGARITH = 0b11,

	C_SUB = 0b00,
	C_XOR = 0b01,
	C_OR = 0b10,
	C_AND = 0b11,

	C_SUBW = 0b00,
	C_ADDW = 0b01,
	C_MUL = 0b10,
};

enum class Funct7 {
	ZERO = 0b0000000,

	SUB = 0b0100000,
	SRA = 0b0100000,

	MULDIV = 0b0000001,

	ADDUW_ZEXT = 0b0000100,
	MINMAX_CLMUL = 0b0000101,
	CZERO = 0b0000111,
	SH_ADD = 0b0010000,
	BSET_ORC = 0b0010100,
	NOT = 0b0100000,
	BCLREXT = 0b0100100,
	COUNT_SEXT_ROT = 0b0110000,
	BINV_REV = 0b0110100,
};

enum class Funct5 {
	AMOADD = 0b00000,
	AMOSWAP = 0b00001,
	LR = 0b00010,
	SC = 0b00011,
	AMOXOR = 0b00100,
	AMOAND = 0b01100,
	AMOOR = 0b01000,
	AMOMIN = 0b10000,
	AMOMAX = 0b10100,
	AMOMINU = 0b11000,
	AMOMAXU = 0b11100,

	FADD = 0b00000,
	FSUB = 0b00001,
	FMUL = 0b00010,
	FDIV = 0b00011,
	FSGNJ = 0b00100,
	FMINMAX = 0b00101,
	FCVT_SZ = 0b01000,
	FSQRT = 0b01011,
	FCMP = 0b10100,
	FCVT_TOX = 0b11000,
	FCVT_FROMX = 0b11010,
	FMV_TOX = 0b11100,
	FMV_FROMX = 0b11110,

	VZEXT_VF8 = 0b00010,
	VSEXT_VF8 = 0b00011,
	VZEXT_VF4 = 0b00100,
	VSEXT_VF4 = 0b00101,
	VZEXT_VF2 = 0b00110,
	VSEXT_VF2 = 0b00111,

	VFSQRT = 0b00000,
	VFRSQRT7 = 0b00100,
	VFREC7 = 0b00101,
	VFCLASS = 0b10000,

	VFCVT_XU_F = 0b00000,
	VFCVT_X_F = 0b00001,
	VFCVT_F_XU = 0b00010,
	VFCVT_F_X = 0b00011,
	VFCVT_RTZ_XU_F = 0b00110,
	VFCVT_RTZ_X_F = 0b00111,
	VFWCVT_XU_F = 0b01000,
	VFWCVT_X_F = 0b01001,
	VFWCVT_F_XU = 0b01010,
	VFWCVT_F_X = 0b01011,
	VFWCVT_F_F = 0b01100,
	VFWCVT_RTZ_XU_F = 0b01110,
	VFWCVT_RTZ_X_F = 0b01111,
	VFNCVT_XU_F = 0b10000,
	VFNCVT_X_F = 0b10001,
	VFNCVT_F_XU = 0b10010,
	VFNCVT_F_X = 0b10011,
	VFNCVT_F_F = 0b10100,
	VFNCVT_ROD_F_F = 0b10101,
	VFNCVT_RTZ_XU_F = 0b10110,
	VFNCVT_RTZ_X_F = 0b10111,

	VMV_S = 0b00000,
	VBREV8 = 0b01000,
	VREV8 = 0b01001,
	VBREV = 0b01010,
	VCLZ = 0b01100,
	VCTZ = 0b01101,
	VCPOP_V = 0b01110,
	VCPOP = 0b10000,
	VFIRST = 0b10001,

	VMSBF = 0b00001,
	VMSOF = 0b00010,
	VMSIF = 0b00011,
	VIOTA = 0b10000,
	VID = 0b10001,

	CLZ = 0b00000,
	CTZ = 0b00001,
	CPOP = 0b00010,
	SEXT_B = 0b00100,
	SEXT_H = 0b00101,
	ORC_B = 0b00111,

	C_ZEXT_B = 0b11000,
	C_SEXT_B = 0b11001,
	C_ZEXT_H = 0b11010,
	C_SEXT_H = 0b11011,
	C_ZEXT_W = 0b11100,
	C_NOT = 0b11101,
};

enum class Funct4 {
	C_JR = 0b1000,
	C_MV = 0b1000,
	C_JALR = 0b1001,
	C_ADD = 0b1001,
};

enum class Funct6 {
	C_OP = 0b100011,
	C_OP_32 = 0b100111,
	C_LBU = 0b100000,
	C_LH = 0b100001,
	C_SB = 0b100010,
	C_SH = 0b100011,

	VADD = 0b000000,
	VANDN = 0b000001,
	VSUB = 0b000010,
	VRSUB = 0b000011,
	VMINU = 0b000100,
	VMIN = 0b000101,
	VMAXU = 0b000110,
	VMAX = 0b000111,
	VAND = 0b001001,
	VOR = 0b001010,
	VXOR = 0b001011,
	VRGATHER = 0b001100,
	VSLIDEUP = 0b001110,
	VRGATHEREI16 = 0b001110,
	VSLIDEDOWN = 0b001111,
	VROR = 0b010100,
	VROL = 0b010101,
	VWSLL = 0b110101,

	VREDSUM = 0b000000,
	VREDAND = 0b000001,
	VREDOR = 0b000010,
	VREDXOR = 0b000011,
	VAADDU = 0b001000,
	VAADD = 0b001001,
	VASUBU = 0b001010,
	VASUB = 0b001011,

	VFREDUSUM = 0b000001,
	VFREDOSUM = 0b000011,
	VFMIN = 0b000100,
	VFMAX = 0b000110,
	VFSGNJ = 0b001000,
	VFSGNJN = 0b001001,
	VFSGNJX = 0b001010,

	VADC = 0b010000,
	VMADC = 0b010001,
	VSBC = 0b010010,
	VMSBC = 0b010011,
	VMV = 0b010111,
	VMSEQ = 0b011000,
	VMSNE = 0b011001,
	VMSLTU = 0b011010,
	VMSLT = 0b011011,
	VMSLEU = 0b011100,
	VMSLE = 0b011101,
	VMSGTU = 0b011110,
	VMSGT = 0b011111,

	VMFEQ = 0b011000,
	VMFLE = 0b011001,
	VMFLT = 0b011011,
	VMFNE = 0b011100,
	VMFGT = 0b011101,
	VMFGE = 0b011111,

	VRWUNARY0 = 0b010000,
	VFXUNARY0 = 0b010010,
	VFXUNARY1 = 0b010011,
	VMUNARY0 = 0b010100,

	VCOMPRESS = 0b010111,
	VMANDNOT = 0b011000,
	VMAND = 0b011001,
	VMOR = 0b011010,
	VMXOR = 0b011011,
	VMORNOT = 0b011100,
	VMNAND = 0b011101,
	VMNOR = 0b011110,
	VMXNOR = 0b011111,

	VSADDU = 0b100000,
	VSADD = 0b100001,
	VSSUBU = 0b100010,
	VSSUB = 0b100011,
	VSLL = 0b100101,
	VSMUL_VMVR = 0b100111,
	VSRL = 0b101000,
	VSRA = 0b101001,
	VSSRL = 0b101010,
	VSSRA = 0b101011,
	VNSRL = 0b101100,
	VNSRA = 0b101101,
	VNCLIPU = 0b101110,
	VNCLIP = 0b101111,

	VDIVU = 0b100000,
	VDIV = 0b100001,
	VREMU = 0b100010,
	VREM = 0b100011,
	VMULHU = 0b100100,
	VMUL = 0b100101,
	VMULHSU = 0b100110,
	VMULH = 0b100111,
	VMADD = 0b101001,
	VNMSUB = 0b101011,
	VMACC = 0b101101,
	VNMSAC = 0b101111,

	VFDIV = 0b100000,
	VFRDIV = 0b100001,
	VFMUL = 0b100100,
	VFRSUB = 0b100111,
	VFMADD = 0b101000,
	VFNMADD = 0b101001,
	VFMSUB = 0b101010,
	VFNMSUB = 0b101011,
	VFMACC = 0b101100,
	VFNMACC = 0b101101,
	VFMSAC = 0b101110,
	VFNMSAC = 0b101111,

	VWREDSUMU = 0b110000,
	VWREDSUM = 0b110001,

	VWADDU = 0b110000,
	VWADD = 0b110001,
	VWSUBU = 0b110010,
	VWSUB = 0b110011,
	VWADDU_W = 0b110100,
	VWADD_W = 0b110101,
	VWSUBU_W = 0b110110,
	VWSUB_W = 0b110111,
	VWMULU = 0b111000,
	VWMULSU = 0b111010,
	VWMUL = 0b111011,
	VWMACCU = 0b111100,
	VWMACC = 0b111101,
	VWMACCUS = 0b111110,
	VWMACCSU = 0b111111,

	VFWADD = 0b110000,
	VFWREDUSUM = 0b110001,
	VFWSUB = 0b110010,
	VFWREDOSUM = 0b110011,
	VFWADD_W = 0b110100,
	VFWSUB_W = 0b110110,
	VFWMUL = 0b111000,
	VFWMACC = 0b111100,
	VFWNMACC = 0b111101,
	VFWMSAC = 0b111110,
	VFWNMSAC = 0b111111,
};

enum class Funct12 {
	ECALL = 0b000000000000,
	EBREAK = 0b000000000001,
};

enum class RiscCReg {
	X8, X9, X10, X11, X12, X13, X14, X15,
};

enum class VLSUMop {
	ELEMS = 0b00000,
	REG = 0b01000,
	MASK = 0b01011,
	ELEMS_LOAD_FF = 0b10000,
};

enum class VMop {
	UNIT = 0b00,
	INDEXU = 0b01,
	STRIDE = 0b10,
	INDEXO = 0b11,
};

static inline RiscVReg DecodeReg(RiscVReg reg) { return (RiscVReg)(reg & 0x1F); }
static inline bool IsGPR(RiscVReg reg) { return (reg & ~0x1F) == 0; }
static inline bool IsFPR(RiscVReg reg) { return (reg & ~0x1F) == 0x20; }
static inline bool IsVPR(RiscVReg reg) { return (reg & ~0x1F) == 0x40; }

static inline bool CanCompress(RiscVReg reg) {
	return (DecodeReg(reg) & 0x18) == 0x08;
}
static inline RiscCReg CompressReg(RiscVReg reg) {
	_assert_msg_(CanCompress(reg), "Compressed reg must be between 8 and 15");
	return (RiscCReg)(reg & 0x07);
}

static inline s32 SignReduce32(s32 v, int width) {
	int shift = 32 - width;
	return (v << shift) >> shift;
}

static inline s64 SignReduce64(s64 v, int width) {
	int shift = 64 - width;
	return (v << shift) >> shift;
}

// Compressed encodings have weird immediate bit order, trying to make it more readable.
static inline u8 ImmBit8(int imm, int bit) {
	return (imm >> bit) & 1;
}
static inline u8 ImmBits8(int imm, int start, int sz) {
	int mask = (1 << sz) - 1;
	return (imm >> start) & mask;
}
static inline u16 ImmBit16(int imm, int bit) {
	return (imm >> bit) & 1;
}
static inline u16 ImmBits16(int imm, int start, int sz) {
	int mask = (1 << sz) - 1;
	return (imm >> start) & mask;
}
static inline u32 ImmBit32(int imm, int bit) {
	return (imm >> bit) & 1;
}
static inline u32 ImmBits32(int imm, int start, int sz) {
	int mask = (1 << sz) - 1;
	return (imm >> start) & mask;
}

static inline u32 EncodeR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct7 funct7) {
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)funct7 << 25);
}

static inline u32 EncodeGR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct7 funct7) {
	_assert_msg_(IsGPR(rd), "R instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "R instruction rs1 must be GPR");
	_assert_msg_(IsGPR(rs2), "R instruction rs2 must be GPR");
	return EncodeR(opcode, rd, funct3, rs1, rs2, funct7);
}

static inline u32 EncodeGR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct5 funct5, Funct7 funct7) {
	_assert_msg_(IsGPR(rd), "R instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "R instruction rs1 must be GPR");
	return EncodeR(opcode, rd, funct3, rs1, (RiscVReg)funct5, funct7);
}

static inline u32 EncodeAtomicR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Atomic ordering, Funct5 funct5) {
	u32 funct7 = ((u32)funct5 << 2) | (u32)ordering;
	return EncodeGR(opcode, rd, funct3, rs1, rs2, (Funct7)funct7);
}

static inline u32 EncodeR4(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, RiscVReg rs3) {
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)funct2 << 25) | ((u32)DecodeReg(rs3) << 27);
}

static inline u32 EncodeFR4(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, RiscVReg rs3) {
	_assert_msg_(IsFPR(rd), "R4 instruction rd must be FPR");
	_assert_msg_(IsFPR(rs1), "R4 instruction rs1 must be FPR");
	_assert_msg_(IsFPR(rs2), "R4 instruction rs2 must be FPR");
	_assert_msg_(IsFPR(rs3), "R4 instruction rs3 must be FPR");
	return EncodeR4(opcode, rd, funct3, rs1, rs2, funct2, rs3);
}

static inline u32 EncodeR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, Funct5 funct5) {
	return EncodeR(opcode, rd, funct3, rs1, rs2, (Funct7)(((u32)funct5 << 2) | (u32)funct2));
}

static inline u32 EncodeFR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, Funct5 funct5) {
	_assert_msg_(IsFPR(rd), "FR instruction rd must be FPR");
	_assert_msg_(IsFPR(rs1), "FR instruction rs1 must be FPR");
	_assert_msg_(IsFPR(rs2), "FR instruction rs2 must be FPR");
	return EncodeR(opcode, rd, funct3, rs1, rs2, (Funct7)(((u32)funct5 << 2) | (u32)funct2));
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, s32 simm12) {
	_assert_msg_(SignReduce32(simm12, 12) == simm12, "I immediate must be signed s11.0: %d", simm12);
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)simm12 << 20);
}

static inline u32 EncodeGI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, s32 simm12) {
	_assert_msg_(IsGPR(rd), "I instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "I instruction rs1 must be GPR");
	return EncodeI(opcode, rd, funct3, rs1, simm12);
}

static inline u32 EncodeGIShift(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, u32 shamt, Funct7 funct7) {
	_assert_msg_(IsGPR(rd), "IShift instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "IShift instruction rs1 must be GPR");
	_assert_msg_(shamt < BitsSupported(), "IShift instruction shift out of range %d", shamt);
	// Low bits of funct7 must be 0 to allow for shift amounts.
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)shamt << 20) | ((u32)funct7 << 25);
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct12 funct12) {
	return EncodeI(opcode, rd, funct3, rs1, SignReduce32((s32)funct12, 12));
}

static inline u32 EncodeGI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct12 funct12) {
	_assert_msg_(IsGPR(rd), "I instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "I instruction rs1 must be GPR");
	return EncodeI(opcode, rd, funct3, rs1, funct12);
}

static inline u32 EncodeS(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm12) {
	_assert_msg_(SignReduce32(simm12, 12) == simm12, "S immediate must be signed s11.0: %d", simm12);
	u32 imm4_0 = ImmBits32(simm12, 0, 5);
	u32 imm11_5 = ImmBits32(simm12, 5, 7);
	return (u32)opcode | (imm4_0 << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | (imm11_5 << 25);
}

static inline u32 EncodeGS(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm12) {
	_assert_msg_(IsGPR(rs1), "S instruction rs1 must be GPR");
	_assert_msg_(IsGPR(rs2), "S instruction rs2 must be GPR");
	return EncodeS(opcode, funct3, rs1, rs2, simm12);
}

static inline u32 EncodeB(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm13) {
	_assert_msg_(SignReduce32(simm13, 13) == simm13, "B immediate must be signed s12.0: %d", simm13);
	_assert_msg_((simm13 & 1) == 0, "B immediate must be even");
	// This weird encoding scheme is to keep most bits the same as S, but keep sign at 31.
	u32 imm4_1_11 = (ImmBits32(simm13, 1, 4) << 1) | ImmBit32(simm13, 11);
	u32 imm12_10_5 = (ImmBit32(simm13, 12) << 6) | ImmBits32(simm13, 5, 6);
	return (u32)opcode | ((u32)imm4_1_11 << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)imm12_10_5 << 25);
}

static inline u32 EncodeGB(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm13) {
	_assert_msg_(IsGPR(rs1), "B instruction rs1 must be GPR");
	_assert_msg_(IsGPR(rs2), "B instruction rs2 must be GPR");
	return EncodeB(opcode, funct3, rs1, rs2, simm13);
}

static inline u32 EncodeU(Opcode32 opcode, RiscVReg rd, s32 simm32) {
	_assert_msg_((simm32 & 0x0FFF) == 0, "U immediate must not have lower 12 bits set");
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | (u32)simm32;
}

static inline u32 EncodeGU(Opcode32 opcode, RiscVReg rd, s32 simm32) {
	_assert_msg_(IsGPR(rd), "I instruction rd must be GPR");
	return EncodeU(opcode, rd, simm32);
}

static inline u32 EncodeJ(Opcode32 opcode, RiscVReg rd, s32 simm21) {
	_assert_msg_(SignReduce32(simm21, 21) == simm21, "J immediate must be signed s20.0: %d", simm21);
	_assert_msg_((simm21 & 1) == 0, "J immediate must be even");
	u32 imm11 = ImmBit32(simm21, 11);
	u32 imm20 = ImmBit32(simm21, 20);
	u32 imm10_1 = ImmBits32(simm21, 1, 10);
	u32 imm19_12 = ImmBits32(simm21, 12, 8);
	// This encoding scheme tries to keep the bits from B in the same places, plus sign.
	u32 imm20_10_1_11_19_12 = (imm20 << 19) | (imm10_1 << 9) | (imm11 << 8) | imm19_12;
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | (imm20_10_1_11_19_12 << 12);
}

static inline u32 EncodeGJ(Opcode32 opcode, RiscVReg rd, s32 simm21) {
	_assert_msg_(IsGPR(rd), "J instruction rd must be GPR");
	return EncodeJ(opcode, rd, simm21);
}

static inline u32 EncodeV(RiscVReg vd, Funct3 funct3, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(SupportsVector(), "V instruction not supported");
	_assert_msg_(IsVPR(vs2), "V instruction vs2 must be VPR");
	return EncodeR(Opcode32::OP_V, vd, funct3, vs1, vs2, (Funct7)(((s32)funct6 << 1) | (s32)vm));
}

static inline u32 EncodeVV(RiscVReg vd, Funct3 funct3, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(IsVPR(vd), "VV instruction vd must be VPR");
	_assert_msg_(IsVPR(vs1), "VV instruction vs1 must be VPR");
	return EncodeV(vd, funct3, vs1, vs2, vm, funct6);
}

static inline u32 EncodeIVV_M(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	return EncodeVV(vd, Funct3::OPIVV, vs1, vs2, vm, funct6);
}

static inline u32 EncodeIVV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "IVV instruction vd overlap with mask");
	return EncodeIVV_M(vd, vs1, vs2, vm, funct6);
}

static inline u32 EncodeMVV_M(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	return EncodeVV(vd, Funct3::OPMVV, vs1, vs2, vm, funct6);
}

static inline u32 EncodeMVV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "MVV instruction vd overlap with mask");
	return EncodeMVV_M(vd, vs1, vs2, vm, funct6);
}

static inline u32 EncodeFVV_M(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(FloatBitsSupported() >= 32, "FVV instruction requires vector float support");
	return EncodeVV(vd, Funct3::OPFVV, vs1, vs2, vm, funct6);
}

static inline u32 EncodeFVV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "FVV instruction vd overlap with mask");
	return EncodeFVV_M(vd, vs1, vs2, vm, funct6);
}

static inline u32 EncodeFVV(RiscVReg vd, Funct5 funct5, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(FloatBitsSupported() >= 32, "FVV instruction requires vector float support");
	_assert_msg_(IsVPR(vd), "VV instruction vd must be VPR");
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "FVV instruction vd overlap with mask");
	return EncodeV(vd, Funct3::OPFVV, (RiscVReg)funct5, vs2, vm, funct6);
}

static inline u32 EncodeIVI_M(RiscVReg vd, s8 simm5, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(IsVPR(vd), "IVI instruction vd must be VPR");
	_assert_msg_(SignReduce32(simm5, 5) == simm5, "VI immediate must be signed 5-bit: %d", simm5);
	return EncodeV(vd, Funct3::OPIVI, (RiscVReg)(simm5 & 0x1F), vs2, vm, funct6);
}

static inline u32 EncodeIVI(RiscVReg vd, s8 simm5, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "IVI instruction vd overlap with mask");
	return EncodeIVI_M(vd, simm5, vs2, vm, funct6);
}

static inline u32 EncodeIVX_M(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(IsVPR(vd), "IVX instruction vd must be VPR");
	_assert_msg_(IsGPR(rs1), "IVX instruction rs1 must be GPR");
	return EncodeV(vd, Funct3::OPIVX, rs1, vs2, vm, funct6);
}

static inline u32 EncodeIVX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "IVX instruction vd overlap with mask");
	return EncodeIVX_M(vd, rs1, vs2, vm, funct6);
}

static inline u32 EncodeMVX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(IsVPR(vd), "MVX instruction vd must be VPR");
	_assert_msg_(IsGPR(rs1), "MVX instruction rs1 must be GPR");
	return EncodeV(vd, Funct3::OPMVX, rs1, vs2, vm, funct6);
}

static inline u32 EncodeFVF_M(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(FloatBitsSupported() >= 32, "FVF instruction requires vector float support");
	_assert_msg_(IsVPR(vd), "FVF instruction vd must be VPR");
	_assert_msg_(IsFPR(rs1), "FVF instruction rs1 must be FPR");
	return EncodeV(vd, Funct3::OPFVF, rs1, vs2, vm, funct6);
}

static inline u32 EncodeFVF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm, Funct6 funct6) {
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "FVF instruction vd overlap with mask");
	return EncodeFVF_M(vd, rs1, vs2, vm, funct6);
}

static inline u16 EncodeCR(Opcode16 op, RiscVReg rs2, RiscVReg rd, Funct4 funct4) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	return (u16)op | ((u16)rs2 << 2) | ((u16)rd << 7) | ((u16)funct4 << 12);
}

static inline u16 EncodeCI(Opcode16 op, u8 uimm6, RiscVReg rd, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(uimm6 <= 0x3F, "CI immediate overflow: %04x", uimm6);
	u16 imm4_0 = ImmBits16(uimm6, 0, 5);
	u16 imm5 = ImmBit16(uimm6, 5);
	return (u16)op | (imm4_0 << 2) | ((u16)rd << 7) | (imm5 << 12) | ((u16)funct3 << 13);
}

static inline u16 EncodeCSS(Opcode16 op, RiscVReg rs2, u8 uimm6, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(uimm6 <= 0x3F, "CI immediate overflow: %04x", uimm6);
	return (u16)op | ((u16)rs2 << 2) | ((u16)uimm6 << 7) | ((u16)funct3 << 13);
}

static inline u16 EncodeCIW(Opcode16 op, RiscCReg rd, u8 uimm8, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	return (u16)op | ((u16)rd << 2) | ((u16)uimm8 << 5) | ((u16)funct3 << 13);
}

static inline u16 EncodeCL(Opcode16 op, RiscCReg rd, u8 uimm2, RiscCReg rs1, u8 uimm3, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(uimm2 <= 3, "CL immediate1 overflow: %04x", uimm2);
	_assert_msg_(uimm3 <= 7, "CL immediate2 overflow: %04x", uimm3);
	return (u16)op | ((u16)rd << 2) | ((u16)uimm2 << 5) | ((u16)rs1 << 7) | ((u16)uimm3 << 10) | ((u16)funct3 << 13);
}

static inline u16 EncodeCL8(Opcode16 op, RiscCReg rd, RiscCReg rs1, u8 uimm8, Funct3 funct3) {
	_assert_msg_((uimm8 & 0xF8) == uimm8, "CL immediate must fit in 8 bits and be a multiple of 8: %d", (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3 = ImmBits8(uimm8, 3, 3);
	return EncodeCL(op, rd, imm7_6, rs1, imm5_4_3, funct3);
}

static inline u16 EncodeCL4(Opcode16 op, RiscCReg rd, RiscCReg rs1, u8 uimm7, Funct3 funct3) {
	_assert_msg_((uimm7 & 0x7C) == uimm7, "CL immediate must fit in 7 bits and be a multiple of 4: %d", (int)uimm7);
	u8 imm2_6 = (ImmBit8(uimm7, 2) << 1) | ImmBit8(uimm7, 6);
	u8 imm5_4_3 = ImmBits8(uimm7, 3, 3);
	return EncodeCL(op, rd, imm2_6, rs1, imm5_4_3, funct3);
}

static inline u16 EncodeCS(Opcode16 op, RiscCReg rs2, u8 uimm2, RiscCReg rs1, u8 uimm3, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(uimm2 <= 3, "CS immediate1 overflow: %04x", uimm2);
	_assert_msg_(uimm3 <= 7, "CS immediate2 overflow: %04x", uimm3);
	return (u16)op | ((u16)rs2 << 2) | ((u16)uimm2 << 5) | ((u16)rs1 << 7) | ((u16)uimm3 << 10) | ((u16)funct3 << 13);
}

static inline u16 EncodeCS8(Opcode16 op, RiscCReg rd, RiscCReg rs1, u8 uimm8, Funct3 funct3) {
	_assert_msg_((uimm8 & 0xF8) == uimm8, "CS immediate must fit in 8 bits and be a multiple of 8: %d", (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3 = ImmBits8(uimm8, 3, 3);
	return EncodeCS(op, rd, imm7_6, rs1, imm5_4_3, funct3);
}

static inline u16 EncodeCS4(Opcode16 op, RiscCReg rd, RiscCReg rs1, u8 uimm7, Funct3 funct3) {
	_assert_msg_((uimm7 & 0x7C) == uimm7, "CS immediate must fit in 7 bits and be a multiple of 4: %d", (int)uimm7);
	u8 imm2_6 = (ImmBit8(uimm7, 2) << 1) | ImmBit8(uimm7, 6);
	u8 imm5_4_3 = ImmBits8(uimm7, 3, 3);
	return EncodeCS(op, rd, imm2_6, rs1, imm5_4_3, funct3);
}

static inline u16 EncodeCA(Opcode16 op, RiscCReg rs2, Funct2 funct2a, RiscCReg rd, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	return (u16)op | ((u16)rs2 << 2) | ((u16)funct2a << 5) | ((u16)rd << 7) | ((u16)funct6 << 10);
}

static inline u16 EncodeCB(Opcode16 op, u8 uimm6, RiscCReg rd, Funct2 funct2, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(uimm6 <= 0x3F, "CI immediate overflow: %04x", uimm6);
	u16 imm4_0 = ImmBits16(uimm6, 0, 5);
	u16 imm5 = ImmBit16(uimm6, 5);
	return (u16)op | (imm4_0 << 2) | ((u16)rd << 7) | ((u16)funct2 << 10) | (imm5 << 12) | ((u16)funct3 << 13);
}
static inline u16 EncodeCB(Opcode16 op, s32 simm9, RiscCReg rs1, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(SignReduce32(simm9, 9) == simm9, "CB immediate must be signed s8.0: %d", simm9);
	_assert_msg_((simm9 & 1) == 0, "CB immediate must be even: %d", simm9);
	u16 imm76_21_5 = (ImmBits16(simm9, 6, 2) << 3) | (ImmBits16(simm9, 1, 2) << 1) | ImmBit16(simm9, 5);
	u16 imm8_43 = (ImmBit16(simm9, 8) << 2) | ImmBits16(simm9, 3, 2);
	return (u16)op | (imm76_21_5 << 2) | ((u16)rs1 << 7) | (imm8_43 << 10) | ((u16)funct3 << 13);
}

static inline u16 EncodeCJ(Opcode16 op, s32 simm12, Funct3 funct3) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_(SignReduce32(simm12, 12) == simm12, "CJ immediate must be signed s11.0: %d", simm12);
	_assert_msg_((simm12 & 1) == 0, "CJ immediate must be even: %d", simm12);
	u16 imm7_3_2_1_5 = (ImmBit16(simm12, 7) << 4) | (ImmBits16(simm12, 1, 3) << 1) | ImmBit16(simm12, 5);
	u16 imm9_8_10_6 = (ImmBits16(simm12, 8, 2) << 2) | (ImmBit16(simm12, 10) << 1) | ImmBit16(simm12, 6);
	u16 imm11_4 = (ImmBit16(simm12, 11) << 1) | ImmBit16(simm12, 4);
	u16 imm11_4_9_8_10_6_7_3_2_1_5 = (imm11_4 << 9) | (imm9_8_10_6 << 5) | imm7_3_2_1_5;
	return (u16)op | (imm11_4_9_8_10_6_7_3_2_1_5 << 2) | ((u16)funct3 << 13);
}

static inline u16 EncodeCLB(Opcode16 op, RiscCReg rd, u8 uimm2, RiscCReg rs1, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_((uimm2 & 3) == uimm2, "CLB immediate must be 2 bit: %d", uimm2);
	return (u16)op | ((u16)rd << 2) | ((u16)uimm2 << 5) | ((u16)rs1 << 7) | ((u16)funct6 << 10);
}

static inline u16 EncodeCSB(Opcode16 op, RiscCReg rs2, u8 uimm2, RiscCReg rs1, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_((uimm2 & 3) == uimm2, "CSB immediate must be 2 bit: %d", uimm2);
	return (u16)op | ((u16)rs2 << 2) | ((u16)uimm2 << 5) | ((u16)rs1 << 7) | ((u16)funct6 << 10);
}

static inline u16 EncodeCLH(Opcode16 op, RiscCReg rd, u8 uimm1, bool funct1, RiscCReg rs1, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_((uimm1 & 1) == uimm1, "CLH immediate must be 1 bit: %d", uimm1);
	return (u16)op | ((u16)rd << 2) | ((u16)uimm1 << 5) | ((u16)funct1 << 6) | ((u16)rs1 << 7) | ((u16)funct6 << 10);
}

static inline u16 EncodeCSH(Opcode16 op, RiscCReg rs2, u8 uimm1, bool funct1, RiscCReg rs1, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	_assert_msg_((uimm1 & 1) == uimm1, "CSH immediate must be 1 bit: %d", uimm1);
	return (u16)op | ((u16)rs2 << 2) | ((u16)uimm1 << 5) | ((u16)funct1 << 6) | ((u16)rs1 << 7) | ((u16)funct6 << 10);
}

static inline u16 EncodeCU(Opcode16 op, Funct5 funct5, RiscCReg rd, Funct6 funct6) {
	_assert_msg_(SupportsCompressed(), "Compressed instructions unsupported");
	return (u16)op | ((u16)funct5 << 2) | ((u16)rd << 7) | ((u16)funct6 << 10);
}

static inline Funct3 BitsToFunct3(int bits, bool useFloat = false, bool allowHalfMin = false) {
	int bitsSupported = useFloat ? FloatBitsSupported() : BitsSupported();
	_assert_msg_(bitsSupported >= bits, "Cannot use funct3 width %d, only have %d", bits, bitsSupported);
	switch (bits) {
	case 16:
		_assert_msg_(SupportsFloatHalf(allowHalfMin), "Cannot use width 16 without Zfh/Zfhmin");
		return Funct3::LS_H;
	case 32:
		return Funct3::LS_W;
	case 64:
		return Funct3::LS_D;
	default:
		_assert_msg_(false, "Invalid funct3 width %d", bits);
		return Funct3::LS_W;
	}
}

static inline Funct2 BitsToFunct2(int bits, bool allowHalfMin = false) {
	_assert_msg_(FloatBitsSupported() >= bits, "Cannot use funct2 width %d, only have %d", bits, FloatBitsSupported());
	switch (bits) {
	case 16:
		_assert_msg_(SupportsFloatHalf(allowHalfMin), "Cannot use width 16 without Zfh/Zfhmin");
		return Funct2::H;
	case 32:
		return Funct2::S;
	case 64:
		return Funct2::D;
	case 128:
		return Funct2::Q;
	default:
		_assert_msg_(false, "Invalid funct2 width %d", bits);
		return Funct2::S;
	}
}

static inline int FConvToFloatBits(FConv c) {
	switch (c) {
	case FConv::W:
	case FConv::WU:
	case FConv::L:
	case FConv::LU:
		break;

	case FConv::S:
		return 32;
	case FConv::D:
		return 64;
	case FConv::H:
		_assert_msg_(SupportsFloatHalf(true), "Cannot use width 16 without Zfh/Zfhmin");
		return 16;
	case FConv::Q:
		return 128;
	}
	return 0;
}

static inline int FConvToIntegerBits(FConv c) {
	switch (c) {
	case FConv::S:
	case FConv::D:
	case FConv::H:
	case FConv::Q:
		break;

	case FConv::W:
	case FConv::WU:
		return 32;
	case FConv::L:
	case FConv::LU:
		return 64;
	}
	return 0;
}

Funct3 VecBitsToFunct3(int bits) {
	int bitsSupported = SupportsVector() ? 64 : 0;
	_assert_msg_(bitsSupported >= bits, "Cannot use funct3 width %d, only have %d", bits, bitsSupported);
	switch (bits) {
	case 8:
		return Funct3::VLS_8;
	case 16:
		return Funct3::VLS_16;
	case 32:
		return Funct3::VLS_32;
	case 64:
		return Funct3::VLS_64;
	default:
		_assert_msg_(false, "Invalid funct3 width %d", bits);
		return Funct3::VLS_8;
	}
}

static s32 VecLSToSimm12(RiscVReg vrs2, VUseMask vm, VMop mop, int bits, int nf) {
	_assert_msg_(nf >= 1 && nf <= 8, "Cannot encode field count %d (must be <= 8)", nf);
	int mew = bits >= 128 ? 1 : 0;
	int nf3 = nf > 4 ? (0xFFFFFFF8 | (nf - 1)) : (nf - 1);
	return (s32)DecodeReg(vrs2) | ((s32)vm << 5) | ((s32)mop << 6) | (mew << 8) | (nf3 << 9);
}

static s32 VecLSToSimm12(VLSUMop lsumop, VUseMask vm, VMop mop, int bits, int nf) {
	return VecLSToSimm12((RiscVReg)(int)lsumop, vm, mop, bits, nf);
}

static Funct5 VExtFracToFunct5(int frac, bool sign) {
	_assert_msg_(SupportsVector(), "v%cext instruction not supported", sign ? 's' : 'z');
	switch (frac) {
	case 8:
		return sign ? Funct5::VSEXT_VF8 : Funct5::VZEXT_VF8;
	case 4:
		return sign ? Funct5::VSEXT_VF4 : Funct5::VZEXT_VF4;
	case 2:
		return sign ? Funct5::VSEXT_VF2 : Funct5::VZEXT_VF2;
	default:
		_assert_msg_(false, "Invalid v%cext frac %d", sign ? 's' : 'z', frac);
		return Funct5::VZEXT_VF8;
	}
}

RiscVEmitter::RiscVEmitter(const u8 *ptr, u8 *writePtr) {
	SetCodePointer(ptr, writePtr);
}

void RiscVEmitter::SetCodePointer(const u8 *ptr, u8 *writePtr) {
	code_ = ptr;
	writable_ = writePtr;
	lastCacheFlushEnd_ = ptr;
}

const u8 *RiscVEmitter::GetCodePointer() const {
	return code_;
}

u8 *RiscVEmitter::GetWritableCodePtr() {
	return writable_;
}

void RiscVEmitter::ReserveCodeSpace(u32 bytes) {
	_assert_msg_((bytes & 1) == 0, "Code space should be aligned");
	_assert_msg_((bytes & 3) == 0 || SupportsCompressed(), "Code space should be aligned (no compressed)");
	for (u32 i = 0; i < bytes / 4; i++)
		EBREAK();
	if (bytes & 2) {
		if (SupportsCompressed())
			C_EBREAK();
		else
			Write16(0);
	}
}

const u8 *RiscVEmitter::AlignCode16() {
	int c = int((u64)code_ & 15);
	if (c)
		ReserveCodeSpace(16 - c);
	return code_;
}

const u8 *RiscVEmitter::AlignCodePage() {
	int page_size = GetMemoryProtectPageSize();
	int c = int((intptr_t)code_ & ((intptr_t)page_size - 1));
	if (c)
		ReserveCodeSpace(page_size - c);
	return code_;
}

void RiscVEmitter::FlushIcache() {
	FlushIcacheSection(lastCacheFlushEnd_, code_);
	lastCacheFlushEnd_ = code_;
}

void RiscVEmitter::FlushIcacheSection(const u8 *start, const u8 *end) {
#if PPSSPP_ARCH(RISCV64)
#if PPSSPP_PLATFORM(LINUX)
	__riscv_flush_icache((char *)start, (char *)end, 0);
#else
	// TODO: This might only correspond to a local hart icache clear, which is no good.
	__builtin___clear_cache((char *)start, (char *)end);
#endif
#endif
}

FixupBranch::FixupBranch(FixupBranch &&other) {
	ptr = other.ptr;
	type = other.type;
	other.ptr = nullptr;
}

FixupBranch::~FixupBranch() {
	_assert_msg_(ptr == nullptr, "FixupBranch never set (left infinite loop)");
}

FixupBranch &FixupBranch::operator =(FixupBranch &&other) {
	ptr = other.ptr;
	type = other.type;
	other.ptr = nullptr;
	return *this;
}

void RiscVEmitter::SetJumpTarget(FixupBranch &branch) {
	SetJumpTarget(branch, code_);
}

void RiscVEmitter::SetJumpTarget(FixupBranch &branch, const void *dst) {
	_assert_msg_(branch.ptr != nullptr, "Invalid FixupBranch (SetJumpTarget twice?)");

	const intptr_t srcp = (intptr_t)branch.ptr;
	const intptr_t dstp = (intptr_t)dst;
	const ptrdiff_t writable_delta = writable_ - code_;
	u32 *writableSrc = (u32 *)(branch.ptr + writable_delta);

	// If compressed, this may be an unaligned 32-bit value, so we modify a copy.
	u32 fixup;
	u16 fixup16;

	_assert_msg_((dstp & 1) == 0, "Destination should be aligned");
	_assert_msg_((dstp & 3) == 0 || SupportsCompressed(), "Destination should be aligned (no compressed)");

	ptrdiff_t distance = dstp - srcp;
	_assert_msg_((distance & 1) == 0, "Distance should be aligned");
	_assert_msg_((distance & 3) == 0 || SupportsCompressed(), "Distance should be aligned (no compressed)");

	switch (branch.type) {
	case FixupBranchType::B:
		_assert_msg_(BInRange(branch.ptr, dst), "B destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup, writableSrc, sizeof(u32));
		fixup = (fixup & 0x01FFF07F) | EncodeB(Opcode32::ZERO, Funct3::ZERO, R_ZERO, R_ZERO, (s32)distance);
		memcpy(writableSrc, &fixup, sizeof(u32));
		break;

	case FixupBranchType::J:
		_assert_msg_(JInRange(branch.ptr, dst), "J destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup, writableSrc, sizeof(u32));
		fixup = (fixup & 0x00000FFF) | EncodeJ(Opcode32::ZERO, R_ZERO, (s32)distance);
		memcpy(writableSrc, &fixup, sizeof(u32));
		break;

	case FixupBranchType::CB:
		_assert_msg_(CBInRange(branch.ptr, dst), "C.B destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup16, writableSrc, sizeof(u16));
		fixup16 = (fixup16 & 0xE383) | EncodeCB(Opcode16::C0, (s32)distance, RiscCReg::X8, Funct3::ZERO);
		memcpy(writableSrc, &fixup16, sizeof(u16));
		break;

	case FixupBranchType::CJ:
		_assert_msg_(CJInRange(branch.ptr, dst), "C.J destination is too far away (%p -> %p)", branch.ptr, dst);
		memcpy(&fixup16, writableSrc, sizeof(u16));
		fixup16 = (fixup16 & 0xE003) | EncodeCJ(Opcode16::C0, (s32)distance, Funct3::ZERO);
		memcpy(writableSrc, &fixup16, sizeof(u16));
		break;
	}

	branch.ptr = nullptr;
}

bool RiscVEmitter::BInRange(const void *func) const {
	return BInRange(code_, func);
}

bool RiscVEmitter::JInRange(const void *func) const {
	return JInRange(code_, func);
}

bool RiscVEmitter::CBInRange(const void *func) const {
	return CBInRange(code_, func);
}

bool RiscVEmitter::CJInRange(const void *func) const {
	return CJInRange(code_, func);
}

static inline bool BJInRange(const void *src, const void *dst, int bits) {
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)src;
	// Get rid of bits and sign extend to validate range.
	s32 encodable = SignReduce32((s32)distance, bits);
	return distance == encodable;
}

bool RiscVEmitter::BInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 13);
}

bool RiscVEmitter::JInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 21);
}

bool RiscVEmitter::CBInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 9);
}

bool RiscVEmitter::CJInRange(const void *src, const void *dst) const {
	return BJInRange(src, dst, 12);
}

void RiscVEmitter::QuickJAL(RiscVReg scratchreg, RiscVReg rd, const u8 *dst) {
	if (!JInRange(GetCodePointer(), dst)) {
		int32_t lower = 0;
		static_assert(sizeof(intptr_t) <= sizeof(int64_t));
		// If it's near PC, we're better off shooting for AUIPC.  Should take 8 bytes.
		int64_t pcdelta = (int64_t)dst - (int64_t)GetCodePointer();
		if (pcdelta < 0x100000000LL && pcdelta >= -0x100000000LL) {
			lower = (int32_t)SignReduce64(pcdelta, 12);
			uintptr_t upper = ((pcdelta - lower) >> 12) << 12;
			LI(scratchreg, (uintptr_t)GetCodePointer() + upper);
		} else {
			lower = (int32_t)SignReduce64((int64_t)dst, 12);
			// Abuse rd as a temporary if we need to.
			LI(scratchreg, dst - lower, rd == scratchreg ? R_ZERO : rd);
		}
		JALR(rd, scratchreg, lower);
	} else {
		JAL(rd, dst);
	}
}

void RiscVEmitter::SetRegToImmediate(RiscVReg rd, uint64_t value, RiscVReg temp) {
	int64_t svalue = (int64_t)value;
	_assert_msg_(IsGPR(rd) && IsGPR(temp), "SetRegToImmediate only supports GPRs");
	_assert_msg_(rd != temp, "SetRegToImmediate cannot use same register for temp and rd");
	_assert_msg_(SignReduce64(svalue, 32) == svalue || (value & 0xFFFFFFFF) == value || BitsSupported() >= 64, "64-bit immediate unsupported");

	if (SignReduce64(svalue, 12) == svalue) {
		// Nice and simple, small immediate fits in a single ADDI against zero.
		ADDI(rd, R_ZERO, (s32)svalue);
		return;
	}

	auto useUpper = [&](int64_t v, void (RiscVEmitter::*upperOp)(RiscVReg, s32), bool force = false) {
		if (SignReduce64(v, 32) == v || force) {
			int32_t lower = (int32_t)SignReduce64(v, 12);
			int32_t upper = ((v - lower) >> 12) << 12;
			bool clearUpper = v >= 0 && upper < 0;
			if (clearUpper) {
				_assert_msg_(BitsSupported() >= 64, "Shouldn't be possible on 32-bit");
				_assert_msg_(force || (((int64_t)upper + lower) & 0xFFFFFFFF) == v, "Upper + ADDI immediate math mistake?");
				// This isn't safe to do using AUIPC.  We can't have the high bit set this way.
				if (upperOp == &RiscVEmitter::AUIPC)
					return false;
			} else {
				_assert_msg_(force || (int64_t)upper + lower == v, "Upper + ADDI immediate math mistake?");
			}

			// Should be fused on some processors.
			(this->*upperOp)(rd, upper);
			if (clearUpper)
				ADDIW(rd, rd, lower);
			else if (lower != 0)
				ADDI(rd, rd, lower);
			return true;
		}
		return false;
	};

	// If this is a simple 32-bit immediate, we can use LUI + ADDI.
	if (useUpper(svalue, &RiscVEmitter::LUI, BitsSupported() == 32))
		return;
	_assert_msg_(BitsSupported() > 32, "Should have stopped at LUI + ADDI on 32-bit");

	// Common case, within 32 bits of PC, use AUIPC + ADDI.
	intptr_t pc = (intptr_t)GetCodePointer();
	if (sizeof(pc) <= 8 && useUpper(svalue - (int64_t)pc, &RiscVEmitter::AUIPC))
		return;

	// Check if it's just a shifted 32 bit immediate, those are cheap.
	for (uint32_t start = 1; start <= 32; ++start) {
		// Take the value (shifted by start) and extend sign from 32 bits.
		int32_t simm32 = (int32_t)(svalue >> start);
		if (((int64_t)simm32 << start) == svalue) {
			LI(rd, simm32);
			SLLI(rd, rd, start);
			return;
		}
	}

	// If this is just a 32-bit unsigned value, use a wall to mask.
	if ((svalue >> 32) == 0) {
		LI(rd, (int32_t)(svalue & 0xFFFFFFFF));
		if (SupportsBitmanip('a')) {
			ZEXT_W(rd, rd);
		} else {
			SLLI(rd, rd, BitsSupported() - 32);
			SRLI(rd, rd, BitsSupported() - 32);
		}
		return;
	}

	// If we have a temporary, let's use it to shorten.
	if (temp != R_ZERO) {
		int32_t lower = (int32_t)svalue;
		int32_t upper = (svalue - lower) >> 32;
		_assert_msg_(((int64_t)upper << 32) + lower == svalue, "LI + SLLI + LI + ADD immediate math mistake?");

		// This could be a bit more optimal, in case a different shamt could simplify an LI.
		LI(rd, (int64_t)upper);
		SLLI(rd, rd, 32);
		LI(temp, (int64_t)lower);
		ADD(rd, rd, temp);
		return;
	}

	// Okay, let's just start with the upper 32 bits and add the rest via ADDI.
	int64_t upper = svalue >> 32;
	LI(rd, upper);

	uint32_t remaining = svalue & 0xFFFFFFFF;
	uint32_t shifted = 0;

	while (remaining != 0) {
		// Skip any zero bits, just set the first ones actually needed.
		uint32_t zeroBits = clz32_nonzero(remaining);
		// We do chunks of 11 to avoid compensating for sign.
		uint32_t targetShift = std::min(zeroBits + 11, 32U);
		uint32_t sourceShift = 32 - targetShift;
		int32_t chunk = (remaining >> sourceShift) & 0x07FF;

		SLLI(rd, rd, targetShift - shifted);
		ADDI(rd, rd, chunk);

		// Okay, increase shift and clear the bits we've deposited.
		shifted = targetShift;
		remaining &= ~(chunk << sourceShift);
	}

	// Move into place in case the lowest bits weren't set.
	if (shifted < 32)
		SLLI(rd, rd, 32 - shifted);
}

void RiscVEmitter::LUI(RiscVReg rd, s32 simm32) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && rd != R_SP && simm32 != 0 && SignReduce32(simm32 & 0x0003F000, 18) == simm32) {
		C_LUI(rd, simm32);
		return;
	}

	Write32(EncodeGU(Opcode32::LUI, rd, simm32));
}

void RiscVEmitter::AUIPC(RiscVReg rd, s32 simm32) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGU(Opcode32::AUIPC, rd, simm32));
}

void RiscVEmitter::JAL(RiscVReg rd, const void *dst) {
	if (AutoCompress() && CJInRange(GetCodePointer(), dst)) {
		if (BitsSupported() == 32 && rd == R_RA) {
			C_JAL(dst);
			return;
		} else if (rd == R_ZERO) {
			C_J(dst);
			return;
		}
	}

	_assert_msg_(JInRange(GetCodePointer(), dst), "JAL destination is too far away (%p -> %p)", GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 1) == 0, "JAL destination should be aligned");
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "JAL destination should be aligned (no compressed)");
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGJ(Opcode32::JAL, rd, (s32)distance));
}

void RiscVEmitter::JALR(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && rs1 != R_ZERO && simm12 == 0) {
		if (rd == R_ZERO) {
			C_JR(rs1);
			return;
		} else if (rd == R_RA) {
			C_JALR(rs1);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::JALR, rd, Funct3::ZERO, rs1, simm12));
}

FixupBranch RiscVEmitter::JAL(RiscVReg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::J };
	Write32(EncodeGJ(Opcode32::JAL, rd, 0));
	return fixup;
}

void RiscVEmitter::BEQ(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	if (AutoCompress() && CBInRange(GetCodePointer(), dst)) {
		if (rs2 == R_ZERO) {
			C_BEQZ(rs1, dst);
			return;
		} else if (rs1 == R_ZERO) {
			C_BEQZ(rs2, dst);
			return;
		}
	}

	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BEQ, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BNE(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	if (AutoCompress() && CBInRange(GetCodePointer(), dst)) {
		if (rs2 == R_ZERO) {
			C_BNEZ(rs1, dst);
			return;
		} else if (rs1 == R_ZERO) {
			C_BNEZ(rs2, dst);
			return;
		}
	}

	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BNE, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BLT(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BLT, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BGE(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BGE, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BLTU(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BLTU, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BGEU(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BGEU, rs1, rs2, (s32)distance));
}

FixupBranch RiscVEmitter::BEQ(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BEQ, rs1, rs2, 0));
	return fixup;
}

FixupBranch RiscVEmitter::BNE(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BNE, rs1, rs2, 0));
	return fixup;
}

FixupBranch RiscVEmitter::BLT(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BLT, rs1, rs2, 0));
	return fixup;
}

FixupBranch RiscVEmitter::BGE(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BGE, rs1, rs2, 0));
	return fixup;
}

FixupBranch RiscVEmitter::BLTU(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BLTU, rs1, rs2, 0));
	return fixup;
}

FixupBranch RiscVEmitter::BGEU(RiscVReg rs1, RiscVReg rs2) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::B };
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BGEU, rs1, rs2, 0));
	return fixup;
}

void RiscVEmitter::LB(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_B, rs1, simm12));
}

void RiscVEmitter::LH(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && SupportsCompressed('b')) {
		if (CanCompress(rd) && CanCompress(rs1) && (simm12 & 2) == simm12) {
			C_LH(rd, rs1, simm12 & 3);
			return;
		}
	}
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_H, rs1, simm12));
}

void RiscVEmitter::LW(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (AutoCompress()) {
		if (CanCompress(rd) && CanCompress(rs1) && (simm12 & 0x7C) == simm12) {
			C_LW(rd, rs1, (u8)simm12);
			return;
		} else if (rd != R_ZERO && rs1 == R_SP && (simm12 & 0xFC) == simm12) {
			C_LWSP(rd, (u8)simm12);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_W, rs1, simm12));
}

void RiscVEmitter::LBU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && SupportsCompressed('b')) {
		if (CanCompress(rd) && CanCompress(rs1) && (simm12 & 3) == simm12) {
			C_LBU(rd, rs1, simm12 & 3);
			return;
		}
	}
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_BU, rs1, simm12));
}

void RiscVEmitter::LHU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && SupportsCompressed('b')) {
		if (CanCompress(rd) && CanCompress(rs1) && (simm12 & 2) == simm12) {
			C_LHU(rd, rs1, simm12 & 3);
			return;
		}
	}
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_HU, rs1, simm12));
}

void RiscVEmitter::SB(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && SupportsCompressed('b')) {
		if (CanCompress(rs2) && CanCompress(rs1) && (simm12 & 3) == simm12) {
			C_SB(rs2, rs1, simm12 & 3);
			return;
		}
	}
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_B, rs1, rs2, simm12));
}

void RiscVEmitter::SH(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	if (AutoCompress() && SupportsCompressed('b')) {
		if (CanCompress(rs2) && CanCompress(rs1) && (simm12 & 2) == simm12) {
			C_SH(rs2, rs1, simm12 & 3);
			return;
		}
	}
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_H, rs1, rs2, simm12));
}

void RiscVEmitter::SW(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	if (AutoCompress()) {
		if (CanCompress(rs2) && CanCompress(rs1) && (simm12 & 0x7C) == simm12) {
			C_SW(rs2, rs1, (u8)simm12);
			return;
		} else if (rs1 == R_SP && (simm12 & 0xFC) == simm12) {
			C_LWSP(rs2, (u8)simm12);
			return;
		}
	}

	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_W, rs1, rs2, simm12));
}

void RiscVEmitter::ADDI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	// Allow NOP form of ADDI.
	_assert_msg_(rd != R_ZERO || (rs1 == R_ZERO && simm12 == 0), "%s write to zero is a HINT", __func__);

	if (AutoCompress()) {
		if (CanCompress(rd) && rs1 == R_SP && simm12 != 0 && (simm12 & 0x03FC) == simm12) {
			C_ADDI4SPN(rd, (u32)simm12);
			return;
		} else if (rd != R_ZERO && rd == rs1 && simm12 != 0 && SignReduce32(simm12, 6) == simm12) {
			C_ADDI(rd, (s8)simm12);
			return;
		} else if (rd != R_ZERO && rs1 == R_ZERO && SignReduce32(simm12, 6) == simm12) {
			C_LI(rd, (s8)simm12);
			return;
		} else if (rd == R_SP && rd == rs1 && simm12 != 0 && SignReduce32(simm12 & ~0xF, 10) == simm12) {
			C_ADDI16SP(simm12);
			return;
		} else if (rd != R_ZERO && rs1 != R_ZERO && simm12 == 0) {
			C_MV(rd, rs1);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::ADD, rs1, simm12));
}

void RiscVEmitter::SLTI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::SLT, rs1, simm12));
}

void RiscVEmitter::SLTIU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::SLTU, rs1, simm12));
}

void RiscVEmitter::XORI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd) && rd == rs1 && simm12 == -1) {
		C_NOT(rd);
		return;
	}

	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::XOR, rs1, simm12));
}

void RiscVEmitter::ORI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress()) {
		if (rd != R_ZERO && rs1 != R_ZERO && simm12 == 0) {
			C_MV(rd, rs1);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::OR, rs1, simm12));
}

void RiscVEmitter::ANDI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && CanCompress(rd) && rd == rs1) {
		if (SignReduce32(simm12, 6) == simm12) {
			C_ANDI(rd, (s8)simm12);
			return;
		} else if (SupportsCompressed('b') && simm12 == 0xFF) {
			C_ZEXT_B(rd);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::AND, rs1, simm12));
}

void RiscVEmitter::SLLI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");

	if (AutoCompress() && rd == rs1 && shamt != 0 && shamt <= (u32)(BitsSupported() == 64 ? 63 : 31)) {
		C_SLLI(rd, (u8)shamt);
		return;
	}

	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::SLL, rs1, shamt, Funct7::ZERO));
}

void RiscVEmitter::SRLI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && shamt <= (u32)(BitsSupported() == 64 ? 63 : 31)) {
		C_SRLI(rd, (u8)shamt);
		return;
	}

	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::SRL, rs1, shamt, Funct7::ZERO));
}

void RiscVEmitter::SRAI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && shamt <= (u32)(BitsSupported() == 64 ? 63 : 31)) {
		C_SRAI(rd, (u8)shamt);
		return;
	}

	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::SRL, rs1, shamt, Funct7::SRA));
}

void RiscVEmitter::ADD(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress()) {
		if (rs1 != R_ZERO && rs2 == R_ZERO) {
			C_MV(rd, rs1);
			return;
		} else if (rs1 == R_ZERO && rs2 != R_ZERO) {
			C_MV(rd, rs2);
			return;
		} else if (rd == rs1 && rs2 != R_ZERO) {
			C_ADD(rd, rs2);
			return;
		} else if (rd == rs2 && rs1 != R_ZERO) {
			C_ADD(rd, rs1);
			return;
		}
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::ADD, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SUB(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
		C_SUB(rd, rs2);
		return;
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::ADD, rs1, rs2, Funct7::SUB));
}

void RiscVEmitter::SLL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::SLL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SLT(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::SLT, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SLTU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::SLTU, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::XOR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
		C_XOR(rd, rs2);
		return;
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::XOR, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::SRL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRA(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::SRL, rs1, rs2, Funct7::SRA));
}

void RiscVEmitter::OR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress()) {
		if (CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
			C_OR(rd, rs2);
			return;
		} else if (rs1 != R_ZERO && rs2 == R_ZERO) {
			C_MV(rd, rs1);
			return;
		} else if (rs1 == R_ZERO && rs2 != R_ZERO) {
			C_MV(rd, rs2);
			return;
		}
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::OR, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::AND(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
		C_AND(rd, rs2);
		return;
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::AND, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::FENCE(Fence predecessor, Fence successor) {
	_assert_msg_((u32)predecessor != 0 && (u32)successor != 0, "FENCE missing pred/succ");
	s32 simm12 = ((u32)predecessor << 4) | (u32)successor;
	Write32(EncodeI(Opcode32::MISC_MEM, R_ZERO, Funct3::FENCE, R_ZERO, simm12));
}

void RiscVEmitter::FENCE_TSO() {
	s32 simm12 = (0b1000 << 28) | ((u32)Fence::RW << 4) | (u32)Fence::RW;
	Write32(EncodeI(Opcode32::MISC_MEM, R_ZERO, Funct3::FENCE, R_ZERO, simm12));
}

void RiscVEmitter::ECALL() {
	Write32(EncodeI(Opcode32::SYSTEM, R_ZERO, Funct3::PRIV, R_ZERO, Funct12::ECALL));
}

void RiscVEmitter::EBREAK() {
	Write32(EncodeI(Opcode32::SYSTEM, R_ZERO, Funct3::PRIV, R_ZERO, Funct12::EBREAK));
}

void RiscVEmitter::LWU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (BitsSupported() == 32) {
		LW(rd, rs1, simm12);
		return;
	}
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_WU, rs1, simm12));
}

void RiscVEmitter::LD(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);

	if (AutoCompress() && (BitsSupported() == 64 || BitsSupported() == 128)) {
		if (CanCompress(rd) && CanCompress(rs1) && (simm12 & 0xF8) == simm12) {
			C_LD(rd, rs1, (u8)simm12);
			return;
		} else if (rd != R_ZERO && rs1 == R_SP && (simm12 & 0x01F8) == simm12) {
			C_LDSP(rd, (u8)simm12);
			return;
		}
	}

	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_D, rs1, simm12));
}

void RiscVEmitter::SD(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);

	if (AutoCompress() && (BitsSupported() == 64 || BitsSupported() == 128)) {
		if (CanCompress(rs2) && CanCompress(rs1) && (simm12 & 0xF8) == simm12) {
			C_SD(rs2, rs1, (u8)simm12);
			return;
		} else if (rs1 == R_SP && (simm12 & 0x01F8) == simm12) {
			C_SDSP(rs2, (u8)simm12);
			return;
		}
	}

	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_D, rs1, rs2, simm12));
}

void RiscVEmitter::ADDIW(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (BitsSupported() == 32) {
		ADDI(rd, rs1, simm12);
		return;
	}

	if (AutoCompress() && rd != R_ZERO && rd == rs1 && SignReduce32(simm12, 6) == simm12) {
		C_ADDIW(rd, (s8)simm12);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::ADD, rs1, simm12));
}

void RiscVEmitter::SLLIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SLLI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGIShift(Opcode32::OP_IMM_32, rd, Funct3::SLL, rs1, shamt, Funct7::ZERO));
}

void RiscVEmitter::SRLIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SRLI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGIShift(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt, Funct7::ZERO));
}

void RiscVEmitter::SRAIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SRAI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGIShift(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt, Funct7::SRA));
}

void RiscVEmitter::ADDW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		ADD(rd, rs1, rs2);
		return;
	}

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
		C_ADDW(rd, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ADD, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SUBW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		SUB(rd, rs1, rs2);
		return;
	}

	if (AutoCompress() && CanCompress(rd) && rd == rs1 && CanCompress(rs2)) {
		C_SUBW(rd, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ADD, rs1, rs2, Funct7::SUB));
}

void RiscVEmitter::SLLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		SLL(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SLL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		SRL(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SRL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRAW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		SRA(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SRL, rs1, rs2, Funct7::SRA));
}

void RiscVEmitter::MUL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(true), "%s instruction unsupported without M/Zmmul", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd)) {
		if (rd == rs1 && CanCompress(rs2)) {
			C_MUL(rd, rs2);
			return;
		} else if (rd == rs2 && CanCompress(rs1)) {
			C_MUL(rd, rs1);
			return;
		}
	}

	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MUL, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULH(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(true), "%s instruction unsupported without M/Zmmul", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULH, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULHSU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(true), "%s instruction unsupported without M/Zmmul", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULHSU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULHU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(true), "%s instruction unsupported without M/Zmmul", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULHU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIV(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s instruction unsupported without M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::DIV, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIVU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s instruction unsupported without M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::DIVU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REM(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s instruction unsupported without M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::REM, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REMU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s instruction unsupported without M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::REMU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(true), "%s is only valid with R64M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::MUL, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIVW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(), "%s is only valid with R64M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::DIV, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIVUW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(), "%s is only valid with R64M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::DIVU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REMW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(), "%s is only valid with R64M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::REM, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REMUW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(), "%s is only valid with R64M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::REMU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::LR(int bits, RiscVReg rd, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	_assert_msg_(ordering != Atomic::RELEASE, "%s should not use RELEASE ordering", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, R_ZERO, ordering, Funct5::LR));
}

void RiscVEmitter::SC(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	_assert_msg_(ordering != Atomic::ACQUIRE, "%s should not use ACQUIRE ordering", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::SC));
}

void RiscVEmitter::AMOSWAP(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOSWAP));
}

void RiscVEmitter::AMOADD(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOADD));
}

void RiscVEmitter::AMOAND(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOAND));
}

void RiscVEmitter::AMOOR(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOOR));
}

void RiscVEmitter::AMOXOR(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOXOR));
}

void RiscVEmitter::AMOMIN(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOMIN));
}

void RiscVEmitter::AMOMAX(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOMAX));
}

void RiscVEmitter::AMOMINU(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOMINU));
}

void RiscVEmitter::AMOMAXU(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg rs1, Atomic ordering) {
	_assert_msg_(SupportsAtomic(), "%s is only valid with R32A", __func__);
	Write32(EncodeAtomicR(Opcode32::AMO, rd, BitsToFunct3(bits), rs1, rs2, ordering, Funct5::AMOMAXU));
}

void RiscVEmitter::FL(int bits, RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(IsGPR(rs1) && IsFPR(rd), "FL with incorrect register types");

	if (AutoCompress() && CanCompress(rd) && CanCompress(rs1)) {
		if (bits == 64 && BitsSupported() <= 64 && (simm12 & 0xF8) == simm12) {
			C_FLD(rd, rs1, (u8)simm12);
			return;
		} else if (bits == 32 && BitsSupported() == 32 && (simm12 & 0x7C) == simm12) {
			C_FLW(rd, rs1, (u8)simm12);
			return;
		}
	} else if (AutoCompress() && rs1 == R_SP) {
		if (bits == 64 && BitsSupported() <= 64 && (simm12 & 0x01F8) == simm12) {
			C_FLDSP(rd, (u32)simm12);
			return;
		} else if (bits == 32 && BitsSupported() == 32 && (simm12 & 0xFC) == simm12) {
			C_FLWSP(rd, (u8)simm12);
			return;
		}
	}

	Write32(EncodeI(Opcode32::LOAD_FP, rd, BitsToFunct3(bits, true, true), rs1, simm12));
}

void RiscVEmitter::FS(int bits, RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	_assert_msg_(IsGPR(rs1) && IsFPR(rs2), "FS with incorrect register types");

	if (AutoCompress() && CanCompress(rs2) && CanCompress(rs1)) {
		if (bits == 64 && BitsSupported() <= 64 && (simm12 & 0xF8) == simm12) {
			C_FSD(rs2, rs1, (u8)simm12);
			return;
		} else if (bits == 32 && BitsSupported() == 32 && (simm12 & 0x7C) == simm12) {
			C_FSW(rs2, rs1, (u8)simm12);
			return;
		} else if (AutoCompress() && rs1 == R_SP) {
			if (bits == 64 && BitsSupported() <= 64 && (simm12 & 0x01F8) == simm12) {
				C_FSDSP(rs2, (u32)simm12);
				return;
			} else if (bits == 32 && BitsSupported() == 32 && (simm12 & 0xFC) == simm12) {
				C_FSWSP(rs2, (u8)simm12);
				return;
			}
		}
	}

	Write32(EncodeS(Opcode32::STORE_FP, BitsToFunct3(bits, true, true), rs1, rs2, simm12));
}

void RiscVEmitter::FMADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm) {
	Write32(EncodeFR4(Opcode32::FMADD, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), rs3));
}

void RiscVEmitter::FMSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm) {
	Write32(EncodeFR4(Opcode32::FMSUB, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), rs3));
}

void RiscVEmitter::FNMSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm) {
	Write32(EncodeFR4(Opcode32::FNMSUB, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), rs3));
}

void RiscVEmitter::FNMADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm) {
	Write32(EncodeFR4(Opcode32::FNMADD, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), rs3));
}

void RiscVEmitter::FADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), Funct5::FADD));
}

void RiscVEmitter::FSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), Funct5::FSUB));
}

void RiscVEmitter::FMUL(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), Funct5::FMUL));
}

void RiscVEmitter::FDIV(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, rs2, BitsToFunct2(bits), Funct5::FDIV));
}

void RiscVEmitter::FSQRT(int bits, RiscVReg rd, RiscVReg rs1, Round rm) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, F0, BitsToFunct2(bits), Funct5::FSQRT));
}

void RiscVEmitter::FSGNJ(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FSGNJ, rs1, rs2, BitsToFunct2(bits), Funct5::FSGNJ));
}

void RiscVEmitter::FSGNJN(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FSGNJN, rs1, rs2, BitsToFunct2(bits), Funct5::FSGNJ));
}

void RiscVEmitter::FSGNJX(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FSGNJX, rs1, rs2, BitsToFunct2(bits), Funct5::FSGNJ));
}

void RiscVEmitter::FMIN(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FMIN, rs1, rs2, BitsToFunct2(bits), Funct5::FMINMAX));
}

void RiscVEmitter::FMAX(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FMAX, rs1, rs2, BitsToFunct2(bits), Funct5::FMINMAX));
}

void RiscVEmitter::FCVT(FConv to, FConv from, RiscVReg rd, RiscVReg rs1, Round rm) {
	int floatBits = std::max(FConvToFloatBits(from), FConvToFloatBits(to));
	int integerBits = std::max(FConvToIntegerBits(from), FConvToIntegerBits(to));

	_assert_msg_(floatBits > 0, "FCVT can't be used with only GPRs");
	_assert_msg_(integerBits <= BitsSupported(), "FCVT for %d integer bits, only %d supported", integerBits, BitsSupported());
	_assert_msg_(floatBits <= FloatBitsSupported(), "FCVT for %d float bits, only %d supported", floatBits, FloatBitsSupported());

	if (integerBits == 0) {
		// Convert between float widths.
		Funct2 fromFmt = BitsToFunct2(FConvToFloatBits(from), true);
		Funct2 toFmt = BitsToFunct2(FConvToFloatBits(to), true);
		if (FConvToFloatBits(to) > FConvToFloatBits(from)) {
			_assert_msg_(rm == Round::DYNAMIC || rm == Round::NEAREST_EVEN, "Invalid rounding mode for widening FCVT");
			rm = Round::NEAREST_EVEN;
		}
		_assert_msg_(fromFmt != toFmt, "FCVT cannot convert to same float type");
		Write32(EncodeR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, (RiscVReg)fromFmt, toFmt, Funct5::FCVT_SZ));
	} else {
		Funct5 funct5 = FConvToIntegerBits(to) == 0 ? Funct5::FCVT_FROMX : Funct5::FCVT_TOX;
		FConv integerFmt = FConvToIntegerBits(to) == 0 ? from : to;
		Funct2 floatFmt = BitsToFunct2(floatBits);
		_assert_msg_(((int)integerFmt & ~3) == 0, "Got wrong integer bits");
		Write32(EncodeR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, (RiscVReg)integerFmt, floatFmt, funct5));
	}
}

void RiscVEmitter::FMV(FMv to, FMv from, RiscVReg rd, RiscVReg rs1) {
	int bits = 0;
	switch (to == FMv::X ? from : to) {
	case FMv::D: bits = 64; break;
	case FMv::W: bits = 32; break;
	case FMv::H: bits = 16; break;
	case FMv::X: bits = 0; break;
	}

	_assert_msg_(BitsSupported() >= bits && FloatBitsSupported() >= bits, "FMV cannot be used for %d bits, only %d/%d supported", bits, BitsSupported(), FloatBitsSupported());
	_assert_msg_((to == FMv::X && from != FMv::X) || (to != FMv::X && from == FMv::X), "%s can only transfer between FPR/GPR", __func__);
	_assert_msg_(to == FMv::X ? IsGPR(rd) : IsFPR(rd), "%s rd of wrong type", __func__);
	_assert_msg_(from == FMv::X ? IsGPR(rs1) : IsFPR(rs1), "%s rs1 of wrong type", __func__);

	Funct5 funct5 = to == FMv::X ? Funct5::FMV_TOX : Funct5::FMV_FROMX;
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FMV, rs1, F0, BitsToFunct2(bits, true), funct5));
}

void RiscVEmitter::FEQ(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd), "%s rd must be GPR", __func__);
	_assert_msg_(IsFPR(rs1), "%s rs1 must be FPR", __func__);
	_assert_msg_(IsFPR(rs2), "%s rs2 must be FPR", __func__);
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FEQ, rs1, rs2, BitsToFunct2(bits), Funct5::FCMP));
}

void RiscVEmitter::FLT(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd), "%s rd must be GPR", __func__);
	_assert_msg_(IsFPR(rs1), "%s rs1 must be FPR", __func__);
	_assert_msg_(IsFPR(rs2), "%s rs2 must be FPR", __func__);
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FLT, rs1, rs2, BitsToFunct2(bits), Funct5::FCMP));
}

void RiscVEmitter::FLE(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd), "%s rd must be GPR", __func__);
	_assert_msg_(IsFPR(rs1), "%s rs1 must be FPR", __func__);
	_assert_msg_(IsFPR(rs2), "%s rs2 must be FPR", __func__);
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FLE, rs1, rs2, BitsToFunct2(bits), Funct5::FCMP));
}

void RiscVEmitter::FCLASS(int bits, RiscVReg rd, RiscVReg rs1) {
	_assert_msg_(IsGPR(rd), "%s rd must be GPR", __func__);
	_assert_msg_(IsFPR(rs1), "%s rs1 must be FPR", __func__);
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FCLASS, rs1, F0, BitsToFunct2(bits), Funct5::FMV_TOX));
}

static const uint32_t FLIvalues[32] = {
	0xBF800000, // -1.0
	0x00800000, // FLT_MIN (note: a bit special)
	0x37800000, // pow(2, -16)
	0x38000000, // pow(2, -15)
	0x3B800000, // pow(2, -8)
	0x3C000000, // pow(2, -7)
	0x3D800000, // 0.0625
	0x3E000000, // 0.125
	0x3E800000, // 0.25
	0x3EA00000, // 0.3125
	0x3EC00000, // 0.375
	0x3EE00000, // 0.4375
	0x3F000000, // 0.5
	0x3F200000, // 0.625
	0x3F400000, // 0.75
	0x3F600000, // 0.875
	0x3F800000, // 1.0
	0x3FA00000, // 1.25
	0x3FC00000, // 1.5
	0x3FE00000, // 1.75
	0x40000000, // 2.0
	0x40200000, // 2.5
	0x40400000, // 3.0
	0x40800000, // 4.0
	0x41000000, // 8.0
	0x41800000, // 16.0
	0x43000000, // 128.0
	0x43800000, // 256.0
	0x47000000, // pow(2, 15)
	0x47800000, // pow(2, 16)
	0x7F800000, // INFINITY
	0x7FC00000, // NAN
};

static RiscVReg EncodeFLImm(int bits, double v) {
	float f = (float)v;
	int index = -1;
	for (size_t i = 0; i < ARRAY_SIZE(FLIvalues); ++i) {
		if (memcmp(&f, &FLIvalues[i], sizeof(float)) == 0) {
			index = (int)i;
			break;
		}
	}

	// For 16-bit, 2/3 are subnormal and 29 is not possible.  Just avoid for now.
	if (index != -1 && index != 1 && (bits > 16 || (index != 2 && index != 3 && index != 29)))
		return (RiscVReg)index;

	if (bits == 64) {
		uint64_t dmin = 0x0010000000000000ULL;
		if (memcmp(&v, &dmin, 8) == 0)
			return F1;
	} else if (bits == 32 && index == 1) {
		return F1;
	} else if (bits == 16) {
		uint64_t hmin = 0x3F10000000000000ULL;
		if (memcmp(&v, &hmin, 8) == 0)
			return F1;
	}

	return INVALID_REG;
}

bool RiscVEmitter::CanFLI(int bits, double v) const {
	if (!SupportsFloatExtra())
		return false;
	if (bits == 16 && !SupportsFloatHalf())
		return false;
	if (bits > FloatBitsSupported())
		return false;
	return EncodeFLImm(bits, v) != INVALID_REG;
}

bool RiscVEmitter::CanFLI(int bits, uint32_t pattern) const {
	float f;
	memcpy(&f, &pattern, sizeof(f));
	return CanFLI(bits, f);
}

void RiscVEmitter::FLI(int bits, RiscVReg rd, double v) {
	_assert_msg_(SupportsFloatExtra(), "%s cannot be used without Zfa", __func__);
	_assert_msg_(bits <= FloatBitsSupported(), "FLI cannot be used for %d bits, only %d/%d supported", bits, BitsSupported(), FloatBitsSupported());
	_assert_msg_(IsFPR(rd), "%s rd of wrong type", __func__);

	RiscVReg imm = EncodeFLImm(bits, v);
	_assert_msg_(imm != INVALID_REG, "FLI with unsupported constant %f for %d bits", v, bits);
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FMV, imm, F1, BitsToFunct2(bits, false), Funct5::FMV_FROMX));
}

void RiscVEmitter::FLI(int bits, RiscVReg rd, uint32_t pattern) {
	float f;
	memcpy(&f, &pattern, sizeof(f));
	FLI(bits, rd, f);
}

void RiscVEmitter::FMINM(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsFloatExtra(), "%s cannot be used without Zfa", __func__);
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FMINM, rs1, rs2, BitsToFunct2(bits), Funct5::FMINMAX));
}

void RiscVEmitter::FMAXM(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsFloatExtra(), "%s cannot be used without Zfa", __func__);
	Write32(EncodeFR(Opcode32::OP_FP, rd, Funct3::FMAXM, rs1, rs2, BitsToFunct2(bits), Funct5::FMINMAX));
}

void RiscVEmitter::FROUND(int bits, RiscVReg rd, RiscVReg rs1, Round rm) {
	_assert_msg_(SupportsFloatExtra(), "%s cannot be used without Zfa", __func__);
	_assert_msg_(bits <= FloatBitsSupported(), "FROUND for %d float bits, only %d supported", bits, FloatBitsSupported());
	_assert_msg_(IsFPR(rd), "%s rd of wrong type", __func__);
	_assert_msg_(IsFPR(rs1), "%s rs1 of wrong type", __func__);

	Funct2 toFmt = BitsToFunct2(bits, false);
	Write32(EncodeR(Opcode32::OP_FP, rd, (Funct3)rm, rs1, F4, toFmt, Funct5::FCVT_SZ));
}

void RiscVEmitter::QuickFLI(int bits, RiscVReg rd, double v, RiscVReg scratchReg) {
	if (CanFLI(bits, v)) {
		FLI(bits, rd, v);
	} else if (bits == 64) {
		LI(scratchReg, v);
		FMV(FMv::D, FMv::X, rd, scratchReg);
	} else if (bits <= 32) {
		QuickFLI(32, rd, (float)v, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void RiscVEmitter::QuickFLI(int bits, RiscVReg rd, uint32_t pattern, RiscVReg scratchReg) {
	if (CanFLI(bits, pattern)) {
		FLI(bits, rd, pattern);
	} else if (bits == 32) {
		LI(scratchReg, (int32_t)pattern);
		FMV(FMv::W, FMv::X, rd, scratchReg);
	} else if (bits == 16) {
		LI(scratchReg, (int16_t)pattern);
		FMV(FMv::H, FMv::X, rd, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void RiscVEmitter::QuickFLI(int bits, RiscVReg rd, float v, RiscVReg scratchReg) {
	if (CanFLI(bits, v)) {
		FLI(bits, rd, v);
	} else if (bits == 64) {
		QuickFLI(32, rd, (double)v, scratchReg);
	} else if (bits == 32) {
		LI(scratchReg, v);
		FMV(FMv::D, FMv::X, rd, scratchReg);
	} else {
		_assert_msg_(false, "Unsupported QuickFLI bits");
	}
}

void RiscVEmitter::CSRRW(RiscVReg rd, Csr csr, RiscVReg rs1) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRW, rs1, (Funct12)csr));
}

void RiscVEmitter::CSRRS(RiscVReg rd, Csr csr, RiscVReg rs1) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRS, rs1, (Funct12)csr));
}

void RiscVEmitter::CSRRC(RiscVReg rd, Csr csr, RiscVReg rs1) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRC, rs1, (Funct12)csr));
}

void RiscVEmitter::CSRRWI(RiscVReg rd, Csr csr, u8 uimm5) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	_assert_msg_((u32)uimm5 <= 0x1F, "%s can only specify lowest 5 bits", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRWI, (RiscVReg)uimm5, (Funct12)csr));
}

void RiscVEmitter::CSRRSI(RiscVReg rd, Csr csr, u8 uimm5) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	_assert_msg_((u32)uimm5 <= 0x1F, "%s can only set lowest 5 bits", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRSI, (RiscVReg)uimm5, (Funct12)csr));
}

void RiscVEmitter::CSRRCI(RiscVReg rd, Csr csr, u8 uimm5) {
	_assert_msg_(SupportsZicsr(), "%s instruction not supported", __func__);
	_assert_msg_((u32)csr <= 0x00000FFF, "%s with invalid CSR number", __func__);
	_assert_msg_((u32)uimm5 <= 0x1F, "%s can only clear lowest 5 bits", __func__);
	Write32(EncodeGI(Opcode32::SYSTEM, rd, Funct3::CSRRCI, (RiscVReg)uimm5, (Funct12)csr));
}

void RiscVEmitter::VSETVLI(RiscVReg rd, RiscVReg rs1, VType vtype) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_((vtype.value & ~0xFF) == 0, "%s with invalid vtype", __func__);
	_assert_msg_(IsGPR(rd), "%s rd (VL) must be GPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (AVL) must be GPR", __func__);
	Write32(EncodeI(Opcode32::OP_V, rd, Funct3::OPCFG, rs1, (s32)vtype.value));
}

void RiscVEmitter::VSETIVLI(RiscVReg rd, u8 uimm5, VType vtype) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_((vtype.value & ~0xFF) == 0, "%s with invalid vtype", __func__);
	_assert_msg_(IsGPR(rd), "%s rd (VL) must be GPR", __func__);
	_assert_msg_((u32)uimm5 <= 0x1F, "%s (AVL) can only set up to 31", __func__);
	s32 simm12 = 0xFFFFFC00 | vtype.value;
	Write32(EncodeI(Opcode32::OP_V, rd, Funct3::OPCFG, (RiscVReg)uimm5, (s32)vtype.value));
}

void RiscVEmitter::VSETVL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsGPR(rd), "%s rd (VL) must be GPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (AVL) must be GPR", __func__);
	_assert_msg_(IsGPR(rs2), "%s rs2 (vtype) must be GPR", __func__);
	Write32(EncodeI(Opcode32::OP_V, rd, Funct3::OPCFG, rs1, rs2));
}

void RiscVEmitter::VLM_V(RiscVReg vd, RiscVReg rs1) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::MASK, VUseMask::NONE, VMop::UNIT, 8, 1);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, Funct3::VLS_8, rs1, simm12));
}

void RiscVEmitter::VLSEGE_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s vd cannot overlap mask", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 must be GPR", __func__);
	// Of course, if LMUL > 1, it could still be wrong, but this is a good basic check.
	_assert_msg_((int)DecodeReg(vd) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::ELEMS, vm, VMop::UNIT, dataBits, fields);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(dataBits), rs1, simm12));
}

void RiscVEmitter::VLSSEGE_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, RiscVReg rs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s vd cannot overlap mask", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsGPR(rs2), "%s rs2 (stride) must be GPR", __func__);
	_assert_msg_((int)DecodeReg(vd) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(rs2, vm, VMop::STRIDE, dataBits, fields);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(dataBits), rs1, simm12));
}

void RiscVEmitter::VLUXSEGEI_V(int fields, int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s vd cannot overlap mask", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsVPR(vs2), "%s vs2 (stride) must be VPR", __func__);
	_assert_msg_((int)DecodeReg(vd) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(vs2, vm, VMop::INDEXU, indexBits, fields);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(indexBits), rs1, simm12));
}

void RiscVEmitter::VLOXSEGEI_V(int fields, int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s vd cannot overlap mask", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsVPR(vs2), "%s vs2 (stride) must be VPR", __func__);
	_assert_msg_((int)DecodeReg(vd) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(vs2, vm, VMop::INDEXO, indexBits, fields);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(indexBits), rs1, simm12));
}

void RiscVEmitter::VLSEGEFF_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s vd cannot overlap mask", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 must be GPR", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::ELEMS_LOAD_FF, vm, VMop::UNIT, dataBits, fields);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(dataBits), rs1, simm12));
}

void RiscVEmitter::VLR_V(int regs, int hintBits, RiscVReg vd, RiscVReg rs1) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vd), "%s vd must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 must be GPR", __func__);
	_assert_msg_(regs == 1 || regs == 2 || regs == 4 || regs == 8, "%s can only access count=1/2/4/8 at a time, not %d", __func__, regs);
	_assert_msg_(regs == 1 || ((int)DecodeReg(vd) & (regs - 1)) == 0, "%s base reg must align to reg count", __func__);
	_assert_msg_((int)DecodeReg(vd) + regs <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::REG, VUseMask::NONE, VMop::UNIT, hintBits, regs);
	Write32(EncodeI(Opcode32::LOAD_FP, vd, VecBitsToFunct3(hintBits), rs1, simm12));
}

void RiscVEmitter::VSM_V(RiscVReg vs3, RiscVReg rs1) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::MASK, VUseMask::NONE, VMop::UNIT, 8, 1);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, Funct3::VLS_8, rs1, simm12));
}

void RiscVEmitter::VSSEGE_V(int fields, int dataBits, RiscVReg vs3, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 must be GPR", __func__);
	_assert_msg_((int)DecodeReg(vs3) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::ELEMS, vm, VMop::UNIT, dataBits, fields);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, VecBitsToFunct3(dataBits), rs1, simm12));
}

void RiscVEmitter::VSSSEGE_V(int fields, int dataBits, RiscVReg vs3, RiscVReg rs1, RiscVReg rs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsGPR(rs2), "%s rs2 (stride) must be GPR", __func__);
	_assert_msg_((int)DecodeReg(vs3) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(rs2, vm, VMop::STRIDE, dataBits, fields);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, VecBitsToFunct3(dataBits), rs1, simm12));
}

void RiscVEmitter::VSUXSEGEI_V(int fields, int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsVPR(vs2), "%s vs2 (stride) must be VPR", __func__);
	_assert_msg_((int)DecodeReg(vs3) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(vs2, vm, VMop::INDEXU, indexBits, fields);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, VecBitsToFunct3(indexBits), rs1, simm12));
}

void RiscVEmitter::VSOXSEGEI_V(int fields, int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 (base) must be GPR", __func__);
	_assert_msg_(IsVPR(vs2), "%s vs2 (stride) must be VPR", __func__);
	_assert_msg_((int)DecodeReg(vs3) + fields <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(vs2, vm, VMop::INDEXO, indexBits, fields);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, VecBitsToFunct3(indexBits), rs1, simm12));
}

void RiscVEmitter::VSR_V(int regs, RiscVReg vs3, RiscVReg rs1) {
	_assert_msg_(SupportsVector(), "%s instruction not supported", __func__);
	_assert_msg_(IsVPR(vs3), "%s vs3 must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s rs1 must be GPR", __func__);
	_assert_msg_(regs == 1 || regs == 2 || regs == 4 || regs == 8, "%s can only access count=1/2/4/8 at a time, not %d", __func__, regs);
	_assert_msg_(regs == 1 || ((int)DecodeReg(vs3) & (regs - 1)) == 0, "%s base reg must align to reg count", __func__);
	_assert_msg_((int)DecodeReg(vs3) + regs <= 32, "%s cannot access beyond V31", __func__);
	s32 simm12 = VecLSToSimm12(VLSUMop::REG, VUseMask::NONE, VMop::UNIT, 8, regs);
	Write32(EncodeI(Opcode32::STORE_FP, vs3, VecBitsToFunct3(8), rs1, simm12));
}

void RiscVEmitter::VADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VADD));
}

void RiscVEmitter::VADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VADD));
}

void RiscVEmitter::VADD_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VADD));
}

void RiscVEmitter::VSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSUB));
}

void RiscVEmitter::VSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSUB));
}

void RiscVEmitter::VRSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VRSUB));
}

void RiscVEmitter::VRSUB_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	if (simm5 == 0) {
		// Normalize, this is the preferred form.
		VRSUB_VX(vd, vs2, X0, vm);
		return;
	}
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VRSUB));
}

void RiscVEmitter::VWADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWADDU));
}

void RiscVEmitter::VWADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWADDU));
}

void RiscVEmitter::VWSUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWSUBU));
}

void RiscVEmitter::VWSUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWSUBU));
}

void RiscVEmitter::VWADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWADD));
}

void RiscVEmitter::VWADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWADD));
}

void RiscVEmitter::VWSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWSUB));
}

void RiscVEmitter::VWSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWSUB));
}

void RiscVEmitter::VWADDU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWADDU_W));
}

void RiscVEmitter::VWADDU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWADDU_W));
}

void RiscVEmitter::VWSUBU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWSUBU_W));
}

void RiscVEmitter::VWSUBU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWSUBU_W));
}

void RiscVEmitter::VWADD_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWADD_W));
}

void RiscVEmitter::VWADD_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWADD_W));
}

void RiscVEmitter::VWSUB_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWSUB_W));
}

void RiscVEmitter::VWSUB_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWSUB_W));
}

void RiscVEmitter::VZEXT_V(int frac, RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, (RiscVReg)VExtFracToFunct5(frac, false), vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VSEXT_V(int frac, RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, (RiscVReg)VExtFracToFunct5(frac, true), vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VADC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::V0_T, Funct6::VADC));
}

void RiscVEmitter::VADC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::V0_T, Funct6::VADC));
}

void RiscVEmitter::VADC_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVI(vd, simm5, vs2, VUseMask::V0_T, Funct6::VADC));
}

void RiscVEmitter::VMADC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::V0_T, Funct6::VMADC));
}

void RiscVEmitter::VMADC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::V0_T, Funct6::VMADC));
}

void RiscVEmitter::VMADC_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVI(vd, simm5, vs2, VUseMask::V0_T, Funct6::VMADC));
}

void RiscVEmitter::VMADC_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::NONE, Funct6::VMADC));
}

void RiscVEmitter::VMADC_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1) {
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::NONE, Funct6::VMADC));
}

void RiscVEmitter::VMADC_VI(RiscVReg vd, RiscVReg vs2, s8 simm5) {
	Write32(EncodeIVI(vd, simm5, vs2, VUseMask::NONE, Funct6::VMADC));
}

void RiscVEmitter::VSBC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::V0_T, Funct6::VSBC));
}

void RiscVEmitter::VSBC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::V0_T, Funct6::VSBC));
}

void RiscVEmitter::VMSBC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::V0_T, Funct6::VMSBC));
}

void RiscVEmitter::VMSBC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::V0_T, Funct6::VMSBC));
}

void RiscVEmitter::VMSBC_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::NONE, Funct6::VMSBC));
}

void RiscVEmitter::VMSBC_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1) {
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::NONE, Funct6::VMSBC));
}

void RiscVEmitter::VAND_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VAND));
}

void RiscVEmitter::VAND_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VAND));
}

void RiscVEmitter::VAND_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VAND));
}

void RiscVEmitter::VOR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VOR));
}

void RiscVEmitter::VOR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VOR));
}

void RiscVEmitter::VOR_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VOR));
}

void RiscVEmitter::VXOR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VXOR));
}

void RiscVEmitter::VXOR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VXOR));
}

void RiscVEmitter::VXOR_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VXOR));
}

void RiscVEmitter::VSLL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSLL));
}

void RiscVEmitter::VSLL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSLL));
}

void RiscVEmitter::VSLL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s shift must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VSLL));
}

void RiscVEmitter::VSRL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSRL));
}

void RiscVEmitter::VSRL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSRL));
}

void RiscVEmitter::VSRL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s shift must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VSRL));
}

void RiscVEmitter::VSRA_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSRA));
}

void RiscVEmitter::VSRA_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSRA));
}

void RiscVEmitter::VSRA_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s shift must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VSRA));
}

void RiscVEmitter::VNSRL_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VNSRL));
}

void RiscVEmitter::VNSRL_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VNSRL));
}

void RiscVEmitter::VNSRL_WI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s shift must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VNSRL));
}

void RiscVEmitter::VNSRA_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VNSRA));
}

void RiscVEmitter::VNSRA_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VNSRA));
}

void RiscVEmitter::VNSRA_WI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s shift must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VNSRA));
}

void RiscVEmitter::VMSEQ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSEQ));
}

void RiscVEmitter::VMSNE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSNE));
}

void RiscVEmitter::VMSLTU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSLTU));
}

void RiscVEmitter::VMSLT_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSLT));
}

void RiscVEmitter::VMSLEU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSLEU));
}

void RiscVEmitter::VMSLE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VMSLE));
}

void RiscVEmitter::VMSEQ_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSEQ));
}

void RiscVEmitter::VMSNE_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSNE));
}

void RiscVEmitter::VMSLTU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSLTU));
}

void RiscVEmitter::VMSLT_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSLT));
}

void RiscVEmitter::VMSLEU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSLEU));
}

void RiscVEmitter::VMSLE_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSLE));
}

void RiscVEmitter::VMSGTU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSGTU));
}

void RiscVEmitter::VMSGT_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX_M(vd, rs1, vs2, vm, Funct6::VMSGT));
}

void RiscVEmitter::VMSEQ_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSEQ));
}

void RiscVEmitter::VMSNE_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSNE));
}

void RiscVEmitter::VMSLEU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSLEU));
}

void RiscVEmitter::VMSLE_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSLE));
}

void RiscVEmitter::VMSGTU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSGTU));
}

void RiscVEmitter::VMSGT_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI_M(vd, simm5, vs2, vm, Funct6::VMSGT));
}

void RiscVEmitter::VMINU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VMINU));
}

void RiscVEmitter::VMINU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVV(vd, rs1, vs2, vm, Funct6::VMINU));
}

void RiscVEmitter::VMIN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VMIN));
}

void RiscVEmitter::VMIN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVV(vd, rs1, vs2, vm, Funct6::VMIN));
}

void RiscVEmitter::VMAXU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VMAXU));
}

void RiscVEmitter::VMAXU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVV(vd, rs1, vs2, vm, Funct6::VMAXU));
}

void RiscVEmitter::VMAX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VMAX));
}

void RiscVEmitter::VMAX_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVV(vd, rs1, vs2, vm, Funct6::VMAX));
}

void RiscVEmitter::VMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMUL));
}

void RiscVEmitter::VMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMUL));
}

void RiscVEmitter::VMULH_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMULH));
}

void RiscVEmitter::VMULH_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMULH));
}

void RiscVEmitter::VMULHU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMULHU));
}

void RiscVEmitter::VMULHU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMULHU));
}

void RiscVEmitter::VMULHSU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMULHSU));
}

void RiscVEmitter::VMULHSU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMULHSU));
}

void RiscVEmitter::VDIVU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VDIVU));
}

void RiscVEmitter::VDIVU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VDIVU));
}

void RiscVEmitter::VDIV_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VDIV));
}

void RiscVEmitter::VDIV_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VDIV));
}

void RiscVEmitter::VREMU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VREMU));
}

void RiscVEmitter::VREMU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VREMU));
}

void RiscVEmitter::VREM_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VREM));
}

void RiscVEmitter::VREM_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VREM));
}

void RiscVEmitter::VWMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMUL));
}

void RiscVEmitter::VWMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMUL));
}

void RiscVEmitter::VWMULU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMULU));
}

void RiscVEmitter::VWMULU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMULU));
}

void RiscVEmitter::VWMULSU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMULSU));
}

void RiscVEmitter::VWMULSU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMULSU));
}

void RiscVEmitter::VMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMACC));
}

void RiscVEmitter::VMACC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMACC));
}

void RiscVEmitter::VNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VNMSAC));
}

void RiscVEmitter::VNMSAC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VNMSAC));
}

void RiscVEmitter::VMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VMADD));
}

void RiscVEmitter::VMADD_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VMADD));
}

void RiscVEmitter::VNMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VNMSUB));
}

void RiscVEmitter::VNMSUB_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VNMSUB));
}

void RiscVEmitter::VWMACCU_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMACCU));
}

void RiscVEmitter::VWMACCU_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMACCU));
}

void RiscVEmitter::VWMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMACC));
}

void RiscVEmitter::VWMACC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMACC));
}

void RiscVEmitter::VWMACCSU_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VWMACCSU));
}

void RiscVEmitter::VWMACCSU_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMACCSU));
}

void RiscVEmitter::VWMACCUS_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VWMACCUS));
}

void RiscVEmitter::VMERGE_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVV(vd, vs1, vs2, VUseMask::V0_T, Funct6::VMV));
}

void RiscVEmitter::VMERGE_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVX(vd, rs1, vs2, VUseMask::V0_T, Funct6::VMV));
}

void RiscVEmitter::VMERGE_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeIVI(vd, simm5, vs2, VUseMask::V0_T, Funct6::VMV));
}

void RiscVEmitter::VMV_VV(RiscVReg vd, RiscVReg vs1) {
	Write32(EncodeIVV(vd, vs1, V0, VUseMask::NONE, Funct6::VMV));
}

void RiscVEmitter::VMV_VX(RiscVReg vd, RiscVReg rs1) {
	Write32(EncodeIVX(vd, rs1, V0, VUseMask::NONE, Funct6::VMV));
}

void RiscVEmitter::VMV_VI(RiscVReg vd, s8 simm5) {
	Write32(EncodeIVI(vd, simm5, V0, VUseMask::NONE, Funct6::VMV));
}

void RiscVEmitter::VSADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSADDU));
}

void RiscVEmitter::VSADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSADDU));
}

void RiscVEmitter::VSADDU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VSADDU));
}

void RiscVEmitter::VSADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSADD));
}

void RiscVEmitter::VSADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSADD));
}

void RiscVEmitter::VSADD_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VSADD));
}

void RiscVEmitter::VSSUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSSUBU));
}

void RiscVEmitter::VSSUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSSUBU));
}

void RiscVEmitter::VSSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSSUB));
}

void RiscVEmitter::VSSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSSUB));
}

void RiscVEmitter::VAADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VAADDU));
}

void RiscVEmitter::VAADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VAADDU));
}

void RiscVEmitter::VAADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VAADD));
}

void RiscVEmitter::VAADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VAADD));
}

void RiscVEmitter::VASUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VASUBU));
}

void RiscVEmitter::VASUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VASUBU));
}

void RiscVEmitter::VASUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV(vd, vs1, vs2, vm, Funct6::VASUB));
}

void RiscVEmitter::VASUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VASUB));
}

void RiscVEmitter::VSMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSMUL_VMVR));
}

void RiscVEmitter::VSMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSMUL_VMVR));
}

void RiscVEmitter::VSSRL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSSRL));
}

void RiscVEmitter::VSSRL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSSRL));
}

void RiscVEmitter::VSSRL_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VSSRL));
}

void RiscVEmitter::VSSRA_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VSSRA));
}

void RiscVEmitter::VSSRA_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSSRA));
}

void RiscVEmitter::VSSRA_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VSSRA));
}

void RiscVEmitter::VNCLIPU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VNCLIPU));
}

void RiscVEmitter::VNCLIPU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VNCLIPU));
}

void RiscVEmitter::VNCLIPU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VNCLIPU));
}

void RiscVEmitter::VNCLIP_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VNCLIP));
}

void RiscVEmitter::VNCLIP_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VNCLIP));
}

void RiscVEmitter::VNCLIP_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm) {
	Write32(EncodeIVI(vd, simm5, vs2, vm, Funct6::VNCLIP));
}

void RiscVEmitter::VFADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VADD));
}

void RiscVEmitter::VFADD_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VADD));
}

void RiscVEmitter::VFSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VSUB));
}

void RiscVEmitter::VFSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VSUB));
}

void RiscVEmitter::VFRSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFRSUB));
}

void RiscVEmitter::VFWADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWADD));
}

void RiscVEmitter::VFWADD_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWADD));
}

void RiscVEmitter::VFWSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWSUB));
}

void RiscVEmitter::VFWSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWSUB));
}

void RiscVEmitter::VFWADD_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWADD_W));
}

void RiscVEmitter::VFWADD_WF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWADD_W));
}

void RiscVEmitter::VFWSUB_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWSUB_W));
}

void RiscVEmitter::VFWSUB_WF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWSUB_W));
}

void RiscVEmitter::VFMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMUL));
}

void RiscVEmitter::VFMUL_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMUL));
}

void RiscVEmitter::VFDIV_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFDIV));
}

void RiscVEmitter::VFDIV_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFDIV));
}

void RiscVEmitter::VFRDIV_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFRDIV));
}

void RiscVEmitter::VFWMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWMUL));
}

void RiscVEmitter::VFWMUL_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWMUL));
}

void RiscVEmitter::VFMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMACC));
}

void RiscVEmitter::VFMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMACC));
}

void RiscVEmitter::VFNMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFNMACC));
}

void RiscVEmitter::VFNMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFNMACC));
}

void RiscVEmitter::VFMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMSAC));
}

void RiscVEmitter::VFMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMSAC));
}

void RiscVEmitter::VFNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFNMSAC));
}

void RiscVEmitter::VFNMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFNMSAC));
}

void RiscVEmitter::VFMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMADD));
}

void RiscVEmitter::VFMADD_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMADD));
}

void RiscVEmitter::VFNMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFNMADD));
}

void RiscVEmitter::VFNMADD_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFNMADD));
}

void RiscVEmitter::VFMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMSUB));
}

void RiscVEmitter::VFMSUB_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMSUB));
}

void RiscVEmitter::VFNMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFNMSUB));
}

void RiscVEmitter::VFNMSUB_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFNMSUB));
}

void RiscVEmitter::VFWMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWMACC));
}

void RiscVEmitter::VFWMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWMACC));
}

void RiscVEmitter::VFWNMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWNMACC));
}

void RiscVEmitter::VFWNMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWNMACC));
}

void RiscVEmitter::VFWMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWMSAC));
}

void RiscVEmitter::VFWMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWMSAC));
}

void RiscVEmitter::VFWNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFWNMSAC));
}

void RiscVEmitter::VFWNMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFWNMSAC));
}

void RiscVEmitter::VFSQRT_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFSQRT, vs2, vm, Funct6::VFXUNARY1));
}

void RiscVEmitter::VFRSQRT7_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFRSQRT7, vs2, vm, Funct6::VFXUNARY1));
}

void RiscVEmitter::VFREC7_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFREC7, vs2, vm, Funct6::VFXUNARY1));
}

void RiscVEmitter::VFMIN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMIN));
}

void RiscVEmitter::VFMIN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMIN));
}

void RiscVEmitter::VFMAX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFMAX));
}

void RiscVEmitter::VFMAX_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFMAX));
}

void RiscVEmitter::VFSGNJ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFSGNJ));
}

void RiscVEmitter::VFSGNJ_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFSGNJ));
}

void RiscVEmitter::VFSGNJN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFSGNJN));
}

void RiscVEmitter::VFSGNJN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFSGNJN));
}

void RiscVEmitter::VFSGNJX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV(vd, vs1, vs2, vm, Funct6::VFSGNJX));
}

void RiscVEmitter::VFSGNJX_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VFSGNJX));
}

void RiscVEmitter::VMFEQ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMFEQ));
}

void RiscVEmitter::VMFEQ_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFEQ));
}

void RiscVEmitter::VMFNE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMFNE));
}

void RiscVEmitter::VMFNE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFNE));
}

void RiscVEmitter::VMFLT_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMFLT));
}

void RiscVEmitter::VMFLT_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFLT));
}

void RiscVEmitter::VMFLE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMFLE));
}

void RiscVEmitter::VMFLE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFLE));
}

void RiscVEmitter::VMFGT_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFGT));
}

void RiscVEmitter::VMFGE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	Write32(EncodeFVF_M(vd, rs1, vs2, vm, Funct6::VMFGE));
}

void RiscVEmitter::VFCLASS_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCLASS, vs2, vm, Funct6::VFXUNARY1));
}

void RiscVEmitter::VFMERGE_VFM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask) {
	_assert_msg_(vmask == V0, "vmask must be V0");
	Write32(EncodeFVF(vd, rs1, vs2, VUseMask::V0_T, Funct6::VMV));
}

void RiscVEmitter::VFMV_VF(RiscVReg vd, RiscVReg rs1) {
	Write32(EncodeFVF(vd, rs1, V0, VUseMask::NONE, Funct6::VMV));
}

void RiscVEmitter::VFCVT_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFCVT_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFCVT_RTZ_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_RTZ_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFCVT_RTZ_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_RTZ_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFCVT_F_XU_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_F_XU, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFCVT_F_X_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFCVT_F_X, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_RTZ_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_RTZ_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_RTZ_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_RTZ_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_F_XU_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_F_XU, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_F_X_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_F_X, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFWCVT_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFWCVT_F_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_XU_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_X_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_RTZ_XU_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_RTZ_XU_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_RTZ_X_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_RTZ_X_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_F_XU_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_F_XU, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_F_X_W(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_F_X, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_F_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VFNCVT_ROD_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	Write32(EncodeFVV(vd, Funct5::VFNCVT_ROD_F_F, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VREDSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VREDSUM));
}

void RiscVEmitter::VREDMAXU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VMAXU));
}

void RiscVEmitter::VREDMAX_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VMAX));
}

void RiscVEmitter::VREDMINU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VMINU));
}

void RiscVEmitter::VREDMIN_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VMIN));
}

void RiscVEmitter::VREDAND_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VREDAND));
}

void RiscVEmitter::VREDOR_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VREDOR));
}

void RiscVEmitter::VREDXOR_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeMVV_M(vd, vs1, vs2, vm, Funct6::VREDXOR));
}

void RiscVEmitter::VWREDSUMU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VWREDSUMU));
}

void RiscVEmitter::VWREDSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeIVV_M(vd, vs1, vs2, vm, Funct6::VWREDSUM));
}

void RiscVEmitter::VFREDOSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VFREDOSUM));
}

void RiscVEmitter::VFREDUSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VFREDUSUM));
}

void RiscVEmitter::VFREDMAX_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMAX));
}

void RiscVEmitter::VFREDMIN_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VMIN));
}

void RiscVEmitter::VFWREDOSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VFWREDOSUM));
}

void RiscVEmitter::VFWREDUSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	Write32(EncodeFVV_M(vd, vs1, vs2, vm, Funct6::VFWREDUSUM));
}

void RiscVEmitter::VMAND_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMAND));
}

void RiscVEmitter::VMNAND_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMNAND));
}

void RiscVEmitter::VMANDN_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMANDNOT));
}

void RiscVEmitter::VMXOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMXOR));
}

void RiscVEmitter::VMOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMOR));
}

void RiscVEmitter::VMNOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMNOR));
}

void RiscVEmitter::VMORN_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMORNOT));
}

void RiscVEmitter::VMXNOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	Write32(EncodeMVV_M(vd, vs1, vs2, VUseMask::NONE, Funct6::VMXNOR));
}

void RiscVEmitter::VCPOP_M(RiscVReg rd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsGPR(rd), "%s instruction rd must be GPR", __func__);
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	Write32(EncodeV(rd, Funct3::OPMVV, (RiscVReg)Funct5::VCPOP, vs2, vm, Funct6::VRWUNARY0));
}

void RiscVEmitter::VFIRST_M(RiscVReg rd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsGPR(rd), "%s instruction rd must be GPR", __func__);
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	Write32(EncodeV(rd, Funct3::OPMVV, (RiscVReg)Funct5::VFIRST, vs2, vm, Funct6::VRWUNARY0));
}

void RiscVEmitter::VMSBF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd overlap vs2", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VMSBF, vs2, vm, Funct6::VMUNARY0));
}

void RiscVEmitter::VMSIF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd overlap vs2", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VMSIF, vs2, vm, Funct6::VMUNARY0));
}

void RiscVEmitter::VMSOF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd overlap vs2", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VMSOF, vs2, vm, Funct6::VMUNARY0));
}

void RiscVEmitter::VIOTA_M(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd overlap vs2", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VIOTA, vs2, vm, Funct6::VMUNARY0));
}

void RiscVEmitter::VID_M(RiscVReg vd, VUseMask vm) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	// The spec doesn't say this, but it also says it's essentially viota.m with vs2=-1, so let's assume.
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VID, V0, vm, Funct6::VMUNARY0));
}

void RiscVEmitter::VMV_X_S(RiscVReg rd, RiscVReg vs2) {
	_assert_msg_(IsGPR(rd), "%s instruction rd must be GPR", __func__);
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	Write32(EncodeV(rd, Funct3::OPMVV, (RiscVReg)Funct5::VMV_S, vs2, VUseMask::NONE, Funct6::VRWUNARY0));
}

void RiscVEmitter::VMV_S_X(RiscVReg vd, RiscVReg rs1) {
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(IsGPR(rs1), "%s instruction rs1 must be GPR", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, rs1, V0, VUseMask::NONE, Funct6::VRWUNARY0));
}

void RiscVEmitter::VFMV_F_S(RiscVReg rd, RiscVReg vs2) {
	_assert_msg_(FloatBitsSupported() >= 32, "FVV instruction requires vector float support");
	_assert_msg_(IsFPR(rd), "%s instruction rd must be FPR", __func__);
	Write32(EncodeV(rd, Funct3::OPFVV, (RiscVReg)Funct5::VMV_S, vs2, VUseMask::NONE, Funct6::VRWUNARY0));
}

void RiscVEmitter::VFMV_S_F(RiscVReg vd, RiscVReg rs1) {
	_assert_msg_(FloatBitsSupported() >= 32, "FVV instruction requires vector float support");
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(IsFPR(rs1), "%s instruction rs1 must be FPR", __func__);
	Write32(EncodeV(vd, Funct3::OPFVV, rs1, V0, VUseMask::NONE, Funct6::VRWUNARY0));
}

void RiscVEmitter::VSLIDEUP_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSLIDEUP));
}

void RiscVEmitter::VSLIDEUP_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s slide amount must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VSLIDEUP));
}

void RiscVEmitter::VSLIDEDOWN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VSLIDEDOWN));
}

void RiscVEmitter::VSLIDEDOWN_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s slide amount must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VSLIDEDOWN));
}

void RiscVEmitter::VSLIDE1UP_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VSLIDEUP));
}

void RiscVEmitter::VFSLIDE1UP_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VSLIDEUP));
}

void RiscVEmitter::VSLIDE1DOWN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeMVX(vd, rs1, vs2, vm, Funct6::VSLIDEDOWN));
}

void RiscVEmitter::VFSLIDE1DOWN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeFVF(vd, rs1, vs2, vm, Funct6::VSLIDEDOWN));
}

void RiscVEmitter::VRGATHER_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(vd != vs1, "%s instruction vd cannot overlap vs1", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VRGATHER));
}

void RiscVEmitter::VRGATHEREI16_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(vd != vs1, "%s instruction vd cannot overlap vs1", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VRGATHEREI16));
}

void RiscVEmitter::VRGATHER_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VRGATHER));
}

void RiscVEmitter::VRGATHER_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	_assert_msg_((uimm5 & 0x1F) == uimm5, "%s index must be <= 0x1F", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VRGATHER));
}

void RiscVEmitter::VCOMPRESS_VM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1) {
	_assert_msg_(vd != vs1, "%s instruction vd cannot overlap vs1", __func__);
	_assert_msg_(vd != vs2, "%s instruction vd cannot overlap vs2", __func__);
	Write32(EncodeMVV(vd, vs1, vs2, VUseMask::NONE, Funct6::VCOMPRESS));
}

void RiscVEmitter::VMVR_V(int regs, RiscVReg vd, RiscVReg vs2) {
	_assert_msg_(regs == 1 || regs == 2 || regs == 4 || regs == 8, "%s can only access count=1/2/4/8 at a time, not %d", __func__, regs);
	_assert_msg_(regs == 1 || ((int)DecodeReg(vd) & (regs - 1)) == 0, "%s base reg must align to reg count", __func__);
	_assert_msg_((int)DecodeReg(vd) + regs <= 32, "%s cannot access beyond V31", __func__);
	Write32(EncodeIVI(vd, regs - 1, vs2, VUseMask::NONE, Funct6::VSMUL_VMVR));
}

void RiscVEmitter::VANDN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VANDN));
}

void RiscVEmitter::VANDN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VANDN));
}

void RiscVEmitter::VBREV_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VBREV, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VBREV8_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VBREV8, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VREV8_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VREV8, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VCLZ_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VCLZ, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VCTZ_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VCTZ, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VCPOP_V(RiscVReg vd, RiscVReg vs2, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	_assert_msg_(IsVPR(vd), "%s instruction vd must be VPR", __func__);
	_assert_msg_(vm != VUseMask::V0_T || vd != V0, "%s instruction vd overlap with mask", __func__);
	Write32(EncodeV(vd, Funct3::OPMVV, (RiscVReg)Funct5::VCPOP_V, vs2, vm, Funct6::VFXUNARY0));
}

void RiscVEmitter::VROL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VROL));
}

void RiscVEmitter::VROL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VROL));
}

void RiscVEmitter::VROR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VROR));
}

void RiscVEmitter::VROR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VROR));
}

void RiscVEmitter::VROR_VI(RiscVReg vd, RiscVReg vs2, u8 uimm6, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b') || SupportsVectorBitmanip('k'), "%s instruction requires Zvbb or Zvkb", __func__);
	_assert_msg_(uimm6 < 64, "%s immediate must be 0-63", __func__);
	// From an encoding perspective, easier to think of this as vror and vror32.
	Funct6 variant = uimm6 >= 32 ? Funct6::VROL : Funct6::VROR;
	Write32(EncodeIVI(vd, SignReduce32(uimm6, 5), vs2, vm, variant));
}

void RiscVEmitter::VWSLL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	Write32(EncodeIVV(vd, vs1, vs2, vm, Funct6::VWSLL));
}

void RiscVEmitter::VWSLL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	Write32(EncodeIVX(vd, rs1, vs2, vm, Funct6::VWSLL));
}

void RiscVEmitter::VWSLL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm) {
	_assert_msg_(SupportsVectorBitmanip('b'), "%s instruction requires Zvbb", __func__);
	_assert_msg_(uimm5 < 32, "%s immediate must be 0-31", __func__);
	Write32(EncodeIVI(vd, SignReduce32(uimm5, 5), vs2, vm, Funct6::VWSLL));
}

void RiscVEmitter::ADD_UW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		ADD(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('a'), "%s instruction unsupported without B", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd) && rd == rs1 && rs2 == R_ZERO) {
		C_ZEXT_W(rd);
		return;
	}

	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ADD, rs1, rs2, Funct7::ADDUW_ZEXT));
}

void RiscVEmitter::SH_ADD(int shift, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('a'), "%s instruction unsupported without B", __func__);
	if (shift == 1)
		Write32(EncodeGR(Opcode32::OP, rd, Funct3::SH1ADD, rs1, rs2, Funct7::SH_ADD));
	else if (shift == 2)
		Write32(EncodeGR(Opcode32::OP, rd, Funct3::SH2ADD, rs1, rs2, Funct7::SH_ADD));
	else if (shift == 3)
		Write32(EncodeGR(Opcode32::OP, rd, Funct3::SH3ADD, rs1, rs2, Funct7::SH_ADD));
	else
		_assert_msg_(shift >= 1 && shift <= 3, "%s shift amount must be 1-3", __func__);
}

void RiscVEmitter::SH_ADD_UW(int shift, RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		SH_ADD(shift, rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('a'), "%s instruction unsupported without B", __func__);
	if (shift == 1)
		Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SH1ADD, rs1, rs2, Funct7::SH_ADD));
	else if (shift == 2)
		Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SH2ADD, rs1, rs2, Funct7::SH_ADD));
	else if (shift == 3)
		Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SH3ADD, rs1, rs2, Funct7::SH_ADD));
	else
		_assert_msg_(shift >= 1 && shift <= 3, "%s shift amount must be 1-3", __func__);
}

void RiscVEmitter::SLLI_UW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SLLI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('a'), "%s instruction unsupported without B", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift %d out of range", shamt);
	Write32(EncodeGIShift(Opcode32::OP_IMM_32, rd, Funct3::SLL, rs1, shamt, Funct7::ADDUW_ZEXT));
}

void RiscVEmitter::ANDN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::AND, rs1, rs2, Funct7::NOT));
}

void RiscVEmitter::ORN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::OR, rs1, rs2, Funct7::NOT));
}

void RiscVEmitter::XNOR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::XOR, rs1, rs2, Funct7::NOT));
}

void RiscVEmitter::CLZ(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CLZ, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::CLZW(RiscVReg rd, RiscVReg rs) {
	if (BitsSupported() == 32) {
		CLZ(rd, rs);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM_32, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CLZ, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::CTZ(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CTZ, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::CTZW(RiscVReg rd, RiscVReg rs) {
	if (BitsSupported() == 32) {
		CTZ(rd, rs);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM_32, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CTZ, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::CPOP(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CPOP, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::CPOPW(RiscVReg rd, RiscVReg rs) {
	if (BitsSupported() == 32) {
		CPOP(rd, rs);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM_32, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::CPOP, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::MAX(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MAX, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::MAXU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MAXU, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::MIN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MIN, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::MINU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MINU, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::SEXT_B(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd) && rd == rs) {
		C_SEXT_B(rd);
		return;
	}

	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::SEXT_B, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::SEXT_H(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd) && rd == rs) {
		C_SEXT_H(rd);
		return;
	}

	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::COUNT_SEXT_ROL, rs, Funct5::SEXT_H, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::ZEXT_H(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);

	if (AutoCompress() && SupportsCompressed('b') && CanCompress(rd) && rd == rs) {
		C_ZEXT_H(rd);
		return;
	}

	if (BitsSupported() == 32)
		Write32(EncodeGR(Opcode32::OP, rd, Funct3::ZEXT, rs, R_ZERO, Funct7::ADDUW_ZEXT));
	else
		Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ZEXT, rs, R_ZERO, Funct7::ADDUW_ZEXT));
}

void RiscVEmitter::ROL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::COUNT_SEXT_ROL, rs1, rs2, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::ROLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		ROL(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::COUNT_SEXT_ROL, rs1, rs2, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::ROR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::ROR, rs1, rs2, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::RORW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		ROR(rd, rs1, rs2);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ROR, rs1, rs2, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::RORI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift %d out of range", shamt);
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::ROR, rs1, shamt, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::RORIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		RORI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift %d out of range", shamt);
	Write32(EncodeGIShift(Opcode32::OP_IMM_32, rd, Funct3::ROR, rs1, shamt, Funct7::COUNT_SEXT_ROT));
}

void RiscVEmitter::ORC_B(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP_IMM, rd, Funct3::BEXT, rs, Funct5::ORC_B, Funct7::BSET_ORC));
}

void RiscVEmitter::REV8(RiscVReg rd, RiscVReg rs) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('b'), "%s instruction unsupported without B", __func__);
	const u32 shamt = BitsSupported() - 8;
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::ROR, rs, shamt, Funct7::BINV_REV));
}

void RiscVEmitter::CLMUL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('c'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::CLMUL, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::CLMULH(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('c'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::CLMULH, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::CLMULR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('c'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::CLMULR, rs1, rs2, Funct7::MINMAX_CLMUL));
}

void RiscVEmitter::BCLR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::BSET, rs1, rs2, Funct7::BCLREXT));
}

void RiscVEmitter::BCLRI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::BSET, rs1, shamt, Funct7::BCLREXT));
}

void RiscVEmitter::BEXT(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::BEXT, rs1, rs2, Funct7::BCLREXT));
}

void RiscVEmitter::BEXTI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::BEXT, rs1, shamt, Funct7::BCLREXT));
}

void RiscVEmitter::BINV(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::BSET, rs1, rs2, Funct7::BINV_REV));
}

void RiscVEmitter::BINVI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::BSET, rs1, shamt, Funct7::BINV_REV));
}

void RiscVEmitter::BSET(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::BSET, rs1, rs2, Funct7::BSET_ORC));
}

void RiscVEmitter::BSETI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsBitmanip('s'), "%s instruction unsupported without B", __func__);
	Write32(EncodeGIShift(Opcode32::OP_IMM, rd, Funct3::BSET, rs1, shamt, Funct7::BSET_ORC));
}

void RiscVEmitter::CZERO_EQZ(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsIntConditional(), "%s instruction unsupported without Zicond", __func__);
	Write32(EncodeR(Opcode32::OP, rd, Funct3::CZERO_EQZ, rs1, rs2, Funct7::CZERO));
}

void RiscVEmitter::CZERO_NEZ(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s should avoid write to zero", __func__);
	_assert_msg_(SupportsIntConditional(), "%s instruction unsupported without Zicond", __func__);
	Write32(EncodeR(Opcode32::OP, rd, Funct3::CZERO_NEZ, rs1, rs2, Funct7::CZERO));
}

bool RiscVEmitter::AutoCompress() const {
	return SupportsCompressed() && autoCompress_;
}

void RiscVEmitter::C_ADDI4SPN(RiscVReg rd, u32 uimm10) {
	_assert_msg_(IsGPR(rd) && CanCompress(rd), "%s requires rd as GPR between X8 and X15", __func__);
	_assert_msg_((uimm10 & 0x03FC) == uimm10 && uimm10 != 0, "%s offset must fit in 10 bits and be a non-zero multiple of 4: %d", __func__, (int)uimm10);
	u8 imm2_3 = (ImmBit8(uimm10, 2) << 1) | ImmBit8(uimm10, 3);
	u8 imm9_8_7_6 = ImmBits8(uimm10, 6, 4);
	u8 imm5_4 = ImmBits8(uimm10, 4, 2);
	u8 imm_5_4_9_8_7_6_2_3 = (imm5_4 << 6) | (imm9_8_7_6 << 2) | imm2_3;
	Write16(EncodeCIW(Opcode16::C0, CompressReg(rd), imm_5_4_9_8_7_6_2_3, Funct3::C_ADDI4SPN));
}

void RiscVEmitter::C_FLD(RiscVReg rd, RiscVReg rs1, u8 uimm8) {
	_assert_msg_(BitsSupported() <= 64 && FloatBitsSupported() == 64, "%s is only valid with RV32DC/RV64DC", __func__);
	_assert_msg_(IsFPR(rd) && CanCompress(rd), "%s requires rd as FPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCL8(Opcode16::C0, CompressReg(rd), CompressReg(rs1), uimm8, Funct3::C_FLD));
}

void RiscVEmitter::C_LW(RiscVReg rd, RiscVReg rs1, u8 uimm7) {
	_assert_msg_(IsGPR(rd) && CanCompress(rd), "%s requires rd as GPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCL4(Opcode16::C0, CompressReg(rd), CompressReg(rs1), uimm7, Funct3::C_LW));
}

void RiscVEmitter::C_FLW(RiscVReg rd, RiscVReg rs1, u8 uimm7) {
	_assert_msg_(BitsSupported() == 32 && FloatBitsSupported() >= 32, "%s is only valid with RV32FC", __func__);
	_assert_msg_(IsFPR(rd) && CanCompress(rd), "%s requires rd as FPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCL4(Opcode16::C0, CompressReg(rd), CompressReg(rs1), uimm7, Funct3::C_FLW));
}

void RiscVEmitter::C_LD(RiscVReg rd, RiscVReg rs1, u8 uimm8) {
	_assert_msg_(BitsSupported() == 64 || BitsSupported() == 128, "%s is only valid with RV64/RV128", __func__);
	_assert_msg_(IsGPR(rd) && CanCompress(rd), "%s requires rd as GPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCL8(Opcode16::C0, CompressReg(rd), CompressReg(rs1), uimm8, Funct3::C_LD));
}

void RiscVEmitter::C_FSD(RiscVReg rs2, RiscVReg rs1, u8 uimm8) {
	_assert_msg_(BitsSupported() <= 64 && FloatBitsSupported() == 64, "%s is only valid with RV32DC/RV64DC", __func__);
	_assert_msg_(IsFPR(rs2) && CanCompress(rs2), "%s requires rs2 as FPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCL8(Opcode16::C0, CompressReg(rs2), CompressReg(rs1), uimm8, Funct3::C_FSD));
}

void RiscVEmitter::C_SW(RiscVReg rs2, RiscVReg rs1, u8 uimm7) {
	_assert_msg_(IsGPR(rs2) && CanCompress(rs2), "%s requires rs2 as GPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCS4(Opcode16::C0, CompressReg(rs2), CompressReg(rs1), uimm7, Funct3::C_SW));
}

void RiscVEmitter::C_FSW(RiscVReg rs2, RiscVReg rs1, u8 uimm7) {
	_assert_msg_(BitsSupported() == 32 && FloatBitsSupported() >= 32, "%s is only valid with RV32FC", __func__);
	_assert_msg_(IsFPR(rs2) && CanCompress(rs2), "%s requires rs2 as FPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCS4(Opcode16::C0, CompressReg(rs2), CompressReg(rs1), uimm7, Funct3::C_FSW));
}

void RiscVEmitter::C_SD(RiscVReg rs2, RiscVReg rs1, u8 uimm8) {
	_assert_msg_(BitsSupported() == 64 || BitsSupported() == 128, "%s is only valid with RV64/RV128", __func__);
	_assert_msg_(IsGPR(rs2) && CanCompress(rs2), "%s requires rs2 as GPR between X8 and X15", __func__);
	_assert_msg_(IsGPR(rs1) && CanCompress(rs1), "%s requires rs1 as GPR between X8 and X15", __func__);
	Write16(EncodeCS8(Opcode16::C0, CompressReg(rs2), CompressReg(rs1), uimm8, Funct3::C_SD));
}

void RiscVEmitter::C_NOP() {
	Write16(EncodeCI(Opcode16::C1, 0, R_ZERO, Funct3::C_ADDI));
}

void RiscVEmitter::C_ADDI(RiscVReg rd, s8 simm6) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(simm6 != 0 && SignReduce32(simm6, 6) == (s32)simm6, "%s immediate must be non-zero and s5.0: %d", __func__, simm6);
	Write16(EncodeCI(Opcode16::C1, ImmBits8(simm6, 0, 6), rd, Funct3::C_ADDI));
}

void RiscVEmitter::C_JAL(const void *dst) {
	_assert_msg_(BitsSupported() == 32, "%s is only valid with RV32C", __func__);
	_assert_msg_(CJInRange(GetCodePointer(), dst), "C_JAL destination is too far away (%p -> %p)", GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 1) == 0, "C_JAL destination should be aligned");
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeCJ(Opcode16::C1, (s32)distance, Funct3::C_JAL));
}

FixupBranch RiscVEmitter::C_JAL() {
	_assert_msg_(BitsSupported() == 32, "%s is only valid with RV32C", __func__);
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::CJ };
	Write16(EncodeCJ(Opcode16::C1, 0, Funct3::C_JAL));
	return fixup;
}

void RiscVEmitter::C_ADDIW(RiscVReg rd, s8 simm6) {
	if (BitsSupported() == 32) {
		C_ADDI(rd, simm6);
		return;
	}

	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(SignReduce32(simm6, 6) == (s32)simm6, "%s immediate must be s5.0: %d", __func__, simm6);
	Write16(EncodeCI(Opcode16::C1, ImmBits8(simm6, 0, 6), rd, Funct3::C_ADDIW));
}

void RiscVEmitter::C_LI(RiscVReg rd, s8 simm6) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(SignReduce32(simm6, 6) == (s32)simm6, "%s immediate must be s5.0: %d", __func__, simm6);
	Write16(EncodeCI(Opcode16::C1, ImmBits8(simm6, 0, 6), rd, Funct3::C_LI));
}

void RiscVEmitter::C_ADDI16SP(s32 simm10) {
	_assert_msg_(simm10 != 0 && SignReduce32(simm10, 10) == simm10, "%s immediate must be non-zero and s9.0: %d", __func__, simm10);
	_assert_msg_((simm10 & 0xF) == 0, "%s immediate must be multiple of 16: %d", __func__, simm10);
	u8 imm8_7_5 = (ImmBits8(simm10, 7, 2) << 1) | ImmBit8(simm10, 5);
	u8 imm4_6 = (ImmBit8(simm10, 4) << 1) | ImmBit8(simm10, 6);
	u8 imm9_4_6_8_7_5 = (ImmBit8(simm10, 9) << 5) | (imm4_6 << 3) | imm8_7_5;
	Write16(EncodeCI(Opcode16::C1, imm9_4_6_8_7_5, R_SP, Funct3::C_LUI));
}

void RiscVEmitter::C_LUI(RiscVReg rd, s32 simm18) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO && rd != R_SP, "%s must write to GPR other than X0/X2", __func__);
	_assert_msg_(simm18 != 0 && SignReduce32(simm18, 18) == simm18, "%s immediate must be non-zero and s17.0: %d", __func__, simm18);
	_assert_msg_((simm18 & 0x0FFF) == 0, "%s immediate must not have lower 12 bits set: %d", __func__, simm18);
	u8 imm17_12 = ImmBits8(simm18, 12, 6);
	Write16(EncodeCI(Opcode16::C1, imm17_12, rd, Funct3::C_LUI));
}

void RiscVEmitter::C_SRLI(RiscVReg rd, u8 uimm6) {
	_assert_msg_(IsGPR(rd), "%s must write to GPR", __func__);
	_assert_msg_(uimm6 != 0 && uimm6 <= (BitsSupported() == 64 ? 63 : 31), "%s immediate must be between 1 and %d: %d", __func__, BitsSupported() == 64 ? 63 : 31, uimm6);
	Write16(EncodeCB(Opcode16::C1, uimm6, CompressReg(rd), Funct2::C_SRLI, Funct3::C_ARITH));
}

void RiscVEmitter::C_SRAI(RiscVReg rd, u8 uimm6) {
	_assert_msg_(IsGPR(rd), "%s must write to GPR", __func__);
	_assert_msg_(uimm6 != 0 && uimm6 <= (BitsSupported() == 64 ? 63 : 31), "%s immediate must be between 1 and %d: %d", __func__, BitsSupported() == 64 ? 63 : 31, uimm6);
	Write16(EncodeCB(Opcode16::C1, uimm6, CompressReg(rd), Funct2::C_SRAI, Funct3::C_ARITH));
}

void RiscVEmitter::C_ANDI(RiscVReg rd, s8 simm6) {
	_assert_msg_(IsGPR(rd), "%s must write to GPR", __func__);
	// It seems like a mistake that this allows simm6 == 0 when c.li can be used...
	_assert_msg_(SignReduce32(simm6, 6) == (s32)simm6, "%s immediate must be s5.0: %d", __func__, simm6);
	Write16(EncodeCB(Opcode16::C1, ImmBits8(simm6, 0, 6), CompressReg(rd), Funct2::C_ANDI, Funct3::C_ARITH));
}

void RiscVEmitter::C_SUB(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_SUB, CompressReg(rd), Funct6::C_OP));
}

void RiscVEmitter::C_XOR(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_XOR, CompressReg(rd), Funct6::C_OP));
}

void RiscVEmitter::C_OR(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_OR, CompressReg(rd), Funct6::C_OP));
}
void RiscVEmitter::C_AND(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_AND, CompressReg(rd), Funct6::C_OP));
}

void RiscVEmitter::C_SUBW(RiscVReg rd, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		C_SUB(rd, rs2);
		return;
	}

	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_SUBW, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_ADDW(RiscVReg rd, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		C_ADD(rd, rs2);
		return;
	}

	_assert_msg_(IsGPR(rd) && IsGPR(rs2), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_ADDW, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_J(const void *dst) {
	_assert_msg_(CJInRange(GetCodePointer(), dst), "C_J destination is too far away (%p -> %p)", GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 1) == 0, "C_J destination should be aligned");
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write16(EncodeCJ(Opcode16::C1, (s32)distance, Funct3::C_J));
}

void RiscVEmitter::C_BEQZ(RiscVReg rs1, const void *dst) {
	_assert_msg_(IsGPR(rs1), "%s must use a GPR", __func__);
	_assert_msg_(CBInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write16(EncodeCB(Opcode16::C1, (s32)distance, CompressReg(rs1), Funct3::C_BEQZ));
}

void RiscVEmitter::C_BNEZ(RiscVReg rs1, const void *dst) {
	_assert_msg_(IsGPR(rs1), "%s must use a GPR", __func__);
	_assert_msg_(CBInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write16(EncodeCB(Opcode16::C1, (s32)distance, CompressReg(rs1), Funct3::C_BNEZ));
}

FixupBranch RiscVEmitter::C_J() {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::CJ };
	Write16(EncodeCJ(Opcode16::C1, 0, Funct3::C_J));
	return fixup;
}

FixupBranch RiscVEmitter::C_BEQZ(RiscVReg rs1) {
	_assert_msg_(IsGPR(rs1), "%s must use a GPR", __func__);
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::CB };
	Write16(EncodeCB(Opcode16::C1, 0, CompressReg(rs1), Funct3::C_BEQZ));
	return fixup;
}

FixupBranch RiscVEmitter::C_BNEZ(RiscVReg rs1) {
	_assert_msg_(IsGPR(rs1), "%s must use a GPR", __func__);
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::CB };
	Write16(EncodeCB(Opcode16::C1, 0, CompressReg(rs1), Funct3::C_BNEZ));
	return fixup;
}

void RiscVEmitter::C_SLLI(RiscVReg rd, u8 uimm6) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(uimm6 != 0 && uimm6 <= (BitsSupported() == 64 ? 63 : 31), "%s immediate must be between 1 and %d: %d", __func__, BitsSupported() == 64 ? 63 : 31, uimm6);
	Write16(EncodeCI(Opcode16::C2, uimm6, rd, Funct3::C_SLLI));
}

void RiscVEmitter::C_FLDSP(RiscVReg rd, u32 uimm9) {
	_assert_msg_(BitsSupported() <= 64 && FloatBitsSupported() == 64, "%s is only valid with RV32DC/RV64DC", __func__);
	_assert_msg_(IsFPR(rd), "%s must write to FPR", __func__);
	_assert_msg_((uimm9 & 0x01F8) == uimm9, "%s offset must fit in 9 bits and be a multiple of 8: %d", __func__, (int)uimm9);
	u8 imm8_7_6 = ImmBits8(uimm9, 6, 3);
	u8 imm5_4_3 = ImmBits8(uimm9, 3, 3);
	u8 imm5_4_3_8_7_6 = (imm5_4_3 << 3) | imm8_7_6;
	Write16(EncodeCI(Opcode16::C2, imm5_4_3_8_7_6, rd, Funct3::C_FLDSP));
}

void RiscVEmitter::C_LWSP(RiscVReg rd, u8 uimm8) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_((uimm8 & 0xFC) == uimm8, "%s offset must fit in 8 bits and be a multiple of 4: %d", __func__, (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3_2 = ImmBits8(uimm8, 2, 4);
	u8 imm5_4_3_2_7_6 = (imm5_4_3_2 << 2) | imm7_6;
	Write16(EncodeCI(Opcode16::C2, imm5_4_3_2_7_6, rd, Funct3::C_LWSP));
}

void RiscVEmitter::C_FLWSP(RiscVReg rd, u8 uimm8) {
	_assert_msg_(BitsSupported() == 32 && FloatBitsSupported() >= 32, "%s is only valid with RV32FC", __func__);
	_assert_msg_(IsFPR(rd), "%s must write to FPR", __func__);
	_assert_msg_((uimm8 & 0xFC) == uimm8, "%s offset must fit in 8 bits and be a multiple of 4: %d", __func__, (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3_2 = ImmBits8(uimm8, 2, 4);
	u8 imm5_4_3_2_7_6 = (imm5_4_3_2 << 2) | imm7_6;
	Write16(EncodeCI(Opcode16::C2, imm5_4_3_2_7_6, rd, Funct3::C_FLWSP));
}

void RiscVEmitter::C_LDSP(RiscVReg rd, u32 uimm9) {
	_assert_msg_(BitsSupported() == 64 || BitsSupported() == 128, "%s is only valid with RV64/RV128", __func__);
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_((uimm9 & 0x01F8) == uimm9, "%s offset must fit in 9 bits and be a multiple of 8: %d", __func__, (int)uimm9);
	u8 imm8_7_6 = ImmBits8(uimm9, 6, 3);
	u8 imm5_4_3 = ImmBits8(uimm9, 3, 3);
	u8 imm5_4_3_8_7_6 = (imm5_4_3 << 3) | imm8_7_6;
	Write16(EncodeCI(Opcode16::C2, imm5_4_3_8_7_6, rd, Funct3::C_LDSP));
}

void RiscVEmitter::C_JR(RiscVReg rs1) {
	_assert_msg_(IsGPR(rs1) && rs1 != R_ZERO, "%s must read from GPR other than X0", __func__);
	Write16(EncodeCR(Opcode16::C2, R_ZERO, rs1, Funct4::C_JR));
}

void RiscVEmitter::C_MV(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(IsGPR(rs2) && rs2 != R_ZERO, "%s must read from GPR other than X0", __func__);
	Write16(EncodeCR(Opcode16::C2, rs2, rd, Funct4::C_MV));
}

void RiscVEmitter::C_EBREAK() {
	Write16(EncodeCR(Opcode16::C2, R_ZERO, R_ZERO, Funct4::C_JALR));
}

void RiscVEmitter::C_JALR(RiscVReg rs1) {
	_assert_msg_(IsGPR(rs1) && rs1 != R_ZERO, "%s must read from GPR other than X0", __func__);
	Write16(EncodeCR(Opcode16::C2, R_ZERO, rs1, Funct4::C_JALR));
}

void RiscVEmitter::C_ADD(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(IsGPR(rd) && rd != R_ZERO, "%s must write to GPR other than X0", __func__);
	_assert_msg_(IsGPR(rs2) && rs2 != R_ZERO, "%s must read from a GPR other than X0", __func__);
	Write16(EncodeCR(Opcode16::C2, rs2, rd, Funct4::C_ADD));
}

void RiscVEmitter::C_FSDSP(RiscVReg rs2, u32 uimm9) {
	_assert_msg_(BitsSupported() <= 64 && FloatBitsSupported() == 64, "%s is only valid with RV32DC/RV64DC", __func__);
	_assert_msg_(IsFPR(rs2), "%s must read from FPR", __func__);
	_assert_msg_((uimm9 & 0x01F8) == uimm9, "%s offset must fit in 9 bits and be a multiple of 8: %d", __func__, (int)uimm9);
	u8 imm8_7_6 = ImmBits8(uimm9, 6, 3);
	u8 imm5_4_3 = ImmBits8(uimm9, 3, 3);
	u8 imm5_4_3_8_7_6 = (imm5_4_3 << 3) | imm8_7_6;
	Write16(EncodeCSS(Opcode16::C2, rs2, imm5_4_3_8_7_6, Funct3::C_FSDSP));
}

void RiscVEmitter::C_SWSP(RiscVReg rs2, u8 uimm8) {
	_assert_msg_(IsGPR(rs2), "%s must read from GPR", __func__);
	_assert_msg_((uimm8 & 0xFC) == uimm8, "%s offset must fit in 8 bits and be a multiple of 4: %d", __func__, (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3_2 = ImmBits8(uimm8, 2, 4);
	u8 imm5_4_3_2_7_6 = (imm5_4_3_2 << 2) | imm7_6;
	Write16(EncodeCSS(Opcode16::C2, rs2, imm5_4_3_2_7_6, Funct3::C_SWSP));
}

void RiscVEmitter::C_FSWSP(RiscVReg rs2, u8 uimm8) {
	_assert_msg_(BitsSupported() == 32 && FloatBitsSupported() >= 32, "%s is only valid with RV32FC", __func__);
	_assert_msg_(IsFPR(rs2), "%s must read from FPR", __func__);
	_assert_msg_((uimm8 & 0xFC) == uimm8, "%s offset must fit in 8 bits and be a multiple of 4: %d", __func__, (int)uimm8);
	u8 imm7_6 = ImmBits8(uimm8, 6, 2);
	u8 imm5_4_3_2 = ImmBits8(uimm8, 2, 4);
	u8 imm5_4_3_2_7_6 = (imm5_4_3_2 << 2) | imm7_6;
	Write16(EncodeCSS(Opcode16::C2, rs2, imm5_4_3_2_7_6, Funct3::C_FSWSP));
}

void RiscVEmitter::C_SDSP(RiscVReg rs2, u32 uimm9) {
	_assert_msg_(BitsSupported() == 64 || BitsSupported() == 128, "%s is only valid with RV64/RV128", __func__);
	_assert_msg_(IsGPR(rs2), "%s must read from GPR", __func__);
	_assert_msg_((uimm9 & 0x01F8) == uimm9, "%s offset must fit in 9 bits and be a multiple of 8: %d", __func__, (int)uimm9);
	u8 imm8_7_6 = ImmBits8(uimm9, 6, 3);
	u8 imm5_4_3 = ImmBits8(uimm9, 3, 3);
	u8 imm5_4_3_8_7_6 = (imm5_4_3 << 3) | imm8_7_6;
	Write16(EncodeCSS(Opcode16::C2, rs2, imm5_4_3_8_7_6, Funct3::C_SDSP));
}

void RiscVEmitter::C_LBU(RiscVReg rd, RiscVReg rs1, u8 uimm2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rd) && IsGPR(rs1), "%s must use GPRs", __func__);
	_assert_msg_((uimm2 & 3) == uimm2, "%s offset must be 0-3", __func__);
	Write16(EncodeCLB(Opcode16::C0, CompressReg(rd), uimm2, CompressReg(rs1), Funct6::C_LBU));
}

void RiscVEmitter::C_LHU(RiscVReg rd, RiscVReg rs1, u8 uimm2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rd) && IsGPR(rs1), "%s must use GPRs", __func__);
	_assert_msg_((uimm2 & 2) == uimm2, "%s offset must be 0 or 2", __func__);
	Write16(EncodeCLH(Opcode16::C0, CompressReg(rd), uimm2 >> 1, false, CompressReg(rs1), Funct6::C_LH));
}

void RiscVEmitter::C_LH(RiscVReg rd, RiscVReg rs1, u8 uimm2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rd) && IsGPR(rs1), "%s must use GPRs", __func__);
	_assert_msg_((uimm2 & 2) == uimm2, "%s offset must be 0 or 2", __func__);
	Write16(EncodeCLH(Opcode16::C0, CompressReg(rd), uimm2 >> 1, true, CompressReg(rs1), Funct6::C_LH));
}

void RiscVEmitter::C_SB(RiscVReg rs2, RiscVReg rs1, u8 uimm2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rs2) && IsGPR(rs1), "%s must use GPRs", __func__);
	_assert_msg_((uimm2 & 3) == uimm2, "%s offset must be 0-3", __func__);
	Write16(EncodeCSB(Opcode16::C0, CompressReg(rs2), uimm2, CompressReg(rs1), Funct6::C_SB));
}

void RiscVEmitter::C_SH(RiscVReg rs2, RiscVReg rs1, u8 uimm2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rs2) && IsGPR(rs1), "%s must use GPRs", __func__);
	_assert_msg_((uimm2 & 2) == uimm2, "%s offset must be 0 or 2", __func__);
	Write16(EncodeCSH(Opcode16::C0, CompressReg(rs2), uimm2 >> 1, false, CompressReg(rs1), Funct6::C_SH));
}

void RiscVEmitter::C_ZEXT_B(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_ZEXT_B, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_SEXT_B(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(SupportsBitmanip('b'), "Zbb bitmanip instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_SEXT_B, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_ZEXT_H(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(SupportsBitmanip('b'), "Zbb bitmanip instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_ZEXT_H, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_SEXT_H(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(SupportsBitmanip('b'), "Zbb bitmanip instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_SEXT_H, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_ZEXT_W(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(SupportsBitmanip('a'), "Zba bitmanip instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_ZEXT_W, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_NOT(RiscVReg rd) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCU(Opcode16::C1, Funct5::C_NOT, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVEmitter::C_MUL(RiscVReg rd, RiscVReg rs2) {
	_assert_msg_(SupportsCompressed('b'), "Zcb compressed instructions unsupported");
	_assert_msg_(SupportsMulDiv(true), "%s instruction unsupported without M/Zmmul", __func__);
	_assert_msg_(IsGPR(rd), "%s must use GPRs", __func__);
	Write16(EncodeCA(Opcode16::C1, CompressReg(rs2), Funct2::C_MUL, CompressReg(rd), Funct6::C_OP_32));
}

void RiscVCodeBlock::PoisonMemory(int offset) {
	// So we can adjust region to writable space.  Might be zero.
	ptrdiff_t writable = writable_ - code_;

	u32 *ptr = (u32 *)(region + offset + writable);
	u32 *maxptr = (u32 *)(region + region_size - offset + writable);
	// This will only write an even multiple of u32, but not much else to do.
	// RiscV: 0x00100073 = EBREAK, 0x9002 = C.EBREAK
	while (ptr + 1 <= maxptr)
		*ptr++ = 0x00100073;
	if (SupportsCompressed() && ptr < maxptr && (intptr_t)maxptr - (intptr_t)ptr >= 2)
		*(u16 *)ptr = 0x9002;
}

};
