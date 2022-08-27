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
#include "Common/BitScan.h"
#include "Common/RiscVEmitter.h"

namespace RiscVGen {

static inline bool SupportsCompressed() {
	// TODO
	return true;
}

static inline uint8_t BitsSupported() {
	// TODO
	return 64;
}

static inline uint8_t FloatBitsSupported() {
	// TODO: 0 if not.
	return 64;
}

static inline bool SupportsMulDiv() {
	// TODO
	return true;
}

static inline bool SupportsAtomic() {
	// TODO
	return true;
}

static inline bool SupportsZicsr() {
	// TODO
	return true;
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
	BRANCH = 0b1100011,
	JALR = 0b1100111,
	JAL = 0b1101111,
	SYSTEM = 0b1110011,
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
};

enum class Funct2 {
	S = 0b00,
	D = 0b01,
	Q = 0b11,
};

enum class Funct7 {
	ZERO = 0b0000000,

	SUB = 0b0100000,
	SRA = 0b0100000,

	MULDIV = 0b0000001,
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
};

enum class Funct12 {
	ECALL = 0b000000000000,
	EBREAK = 0b000000000001,
};

static inline RiscVReg DecodeReg(RiscVReg reg) { return (RiscVReg)(reg & 0x1F); }
static inline bool IsGPR(RiscVReg reg) { return reg < 0x20; }
static inline bool IsFPR(RiscVReg reg) { return (reg & 0x20) != 0 && (int)reg < 0x40; }

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
	_assert_msg_(IsFPR(rd), "R4 instruction rd must be FPR");
	_assert_msg_(IsFPR(rs1), "R4 instruction rs1 must be FPR");
	_assert_msg_(IsFPR(rs2), "R4 instruction rs2 must be FPR");
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

static inline Funct3 BitsToFunct3(int bits, bool useFloat = false) {
	int bitsSupported = useFloat ? FloatBitsSupported() : BitsSupported();
	_assert_msg_(bitsSupported >= bits, "Cannot use funct3 width %d, only have %d", bits, bitsSupported);
	switch (bits) {
	case 32:
		return Funct3::LS_W;
	case 64:
		return Funct3::LS_D;
	default:
		_assert_msg_(false, "Invalid funct3 width %d", bits);
		return Funct3::LS_W;
	}
}

static inline Funct2 BitsToFunct2(int bits) {
	_assert_msg_(FloatBitsSupported() >= bits, "Cannot use funct2 width %d, only have %d", bits, FloatBitsSupported());
	switch (bits) {
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
	case FConv::Q:
		return 128;
	}
	return 0;
}

static inline int FConvToIntegerBits(FConv c) {
	switch (c) {
	case FConv::S:
	case FConv::D:
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
	if (bytes & 2)
		Write16(0);
}

const u8 *RiscVEmitter::AlignCode16() {
	int c = int((u64)code_ & 15);
	if (c)
		ReserveCodeSpace(16 - c);
	return code_;
}

const u8 *RiscVEmitter::AlignCodePage() {
	int page_size = GetMemoryProtectPageSize();
	int c = int((u64)code_ & (page_size - 1));
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
	__builtin___clear_cache(start, end);
#endif
}

FixupBranch::~FixupBranch() {
	_assert_msg_(ptr == nullptr, "FixupBranch never set (left infinite loop)");
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

	// If compressed, this may be an unaligned 32-bit value.
	u32 fixup;
	memcpy(&fixup, writableSrc, sizeof(u32));

	_assert_msg_((dstp & 1) == 0, "Destination should be aligned");
	_assert_msg_((dstp & 3) == 0 || SupportsCompressed(), "Destination should be aligned (no compressed)");

	ptrdiff_t distance = dstp - srcp;
	_assert_msg_((distance & 1) == 0, "Distance should be aligned");
	_assert_msg_((distance & 3) == 0 || SupportsCompressed(), "Distance should be aligned (no compressed)");

	switch (branch.type) {
	case FixupBranchType::B:
		_assert_msg_(BInRange(branch.ptr, dst), "B destination is too far away (%p -> %p)", branch.ptr, dst);
		fixup = (fixup & 0x01FFF07F) | EncodeB(Opcode32::ZERO, Funct3::ZERO, R_ZERO, R_ZERO, (s32)distance);
		break;

	case FixupBranchType::J:
		_assert_msg_(JInRange(branch.ptr, dst), "J destination is too far away (%p -> %p)", branch.ptr, dst);
		fixup = (fixup & 0x00000FFF) | EncodeJ(Opcode32::ZERO, R_ZERO, (s32)distance);
		break;
	}

	memcpy(writableSrc, &fixup, sizeof(u32));
	branch.ptr = nullptr;
}

bool RiscVEmitter::BInRange(const void *func) const {
	return BInRange(code_, func);
}

bool RiscVEmitter::JInRange(const void *func) const {
	return JInRange(code_, func);
}

bool RiscVEmitter::BInRange(const void *src, const void *dst) const {
	const intptr_t srcp = (intptr_t)src;
	const intptr_t dstp = (intptr_t)dst;
	ptrdiff_t distance = dstp - srcp;

	// Get rid of bits and sign extend to validate range.
	s32 encodable = SignReduce32((s32)distance, 13);
	return distance == encodable;
}

bool RiscVEmitter::JInRange(const void *src, const void *dst) const {
	const intptr_t srcp = (intptr_t)src;
	const intptr_t dstp = (intptr_t)dst;
	ptrdiff_t distance = dstp - srcp;

	// Get rid of bits and sign extend to validate range.
	s32 encodable = SignReduce32((s32)distance, 21);
	return distance == encodable;
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
			int32_t lower = (int32_t)SignReduce64(svalue, 12);
			int32_t upper = ((v - lower) >> 12) << 12;
			_assert_msg_(force || upper + lower == v, "Upper + ADDI immediate math mistake?");

			// Should be fused on some processors.
			(this->*upperOp)(rd, upper);
			if (lower != 0)
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
		SLLI(rd, rd, BitsSupported() - 32);
		SRLI(rd, rd, BitsSupported() - 32);
		return;
	}

	// If we have a temporary, let's use it to shorten.
	if (temp != R_ZERO) {
		int32_t lower = (int32_t)svalue;
		int32_t upper = (svalue - lower) >> 32;
		_assert_msg_(((int64_t)upper << 32) + lower == svalue, "LI + SLLI + LI + ADDI immediate math mistake?");

		// This could be a bit more optimal, in case a different shamt could simplify an LI.
		LI(rd, (int64_t)upper);
		SLLI(rd, rd, 32);
		LI(temp, (int64_t)lower);
		ADD(rd, rd, temp);
		return;
	}

	// Okay, let's just start with the upper 32 bits and add the rest via ORI.
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
		ORI(rd, rd, chunk);

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
	Write32(EncodeGU(Opcode32::LUI, rd, simm32));
}

void RiscVEmitter::AUIPC(RiscVReg rd, s32 simm32) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGU(Opcode32::AUIPC, rd, simm32));
}

void RiscVEmitter::JAL(RiscVReg rd, const void *dst) {
	_assert_msg_(JInRange(GetCodePointer(), dst), "JAL destination is too far away (%p -> %p)", GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 1) == 0, "JAL destination should be aligned");
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "JAL destination should be aligned (no compressed)");
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGJ(Opcode32::JAL, rd, (s32)distance));
}

void RiscVEmitter::JALR(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGI(Opcode32::JALR, rd, Funct3::ZERO, rs1, simm12));
}

FixupBranch RiscVEmitter::JAL(RiscVReg rd) {
	FixupBranch fixup{ GetCodePointer(), FixupBranchType::J };
	Write32(EncodeGJ(Opcode32::JAL, rd, 0));
	return fixup;
}

void RiscVEmitter::BEQ(RiscVReg rs1, RiscVReg rs2, const void *dst) {
	_assert_msg_(BInRange(GetCodePointer(), dst), "%s destination is too far away (%p -> %p)", __func__, GetCodePointer(), dst);
	_assert_msg_(((intptr_t)dst & 3) == 0 || SupportsCompressed(), "%s destination should be aligned (no compressed)", __func__);
	ptrdiff_t distance = (intptr_t)dst - (intptr_t)GetCodePointer();
	Write32(EncodeGB(Opcode32::BRANCH, Funct3::BEQ, rs1, rs2, (s32)distance));
}

void RiscVEmitter::BNE(RiscVReg rs1, RiscVReg rs2, const void *dst) {
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
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_H, rs1, simm12));
}

void RiscVEmitter::LW(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_W, rs1, simm12));
}

void RiscVEmitter::LBU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_BU, rs1, simm12));
}

void RiscVEmitter::LHU(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_HU, rs1, simm12));
}

void RiscVEmitter::SB(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_B, rs1, rs2, simm12));
}

void RiscVEmitter::SH(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_H, rs1, rs2, simm12));
}

void RiscVEmitter::SW(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_W, rs1, rs2, simm12));
}

void RiscVEmitter::ADDI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	// Allow NOP form of ADDI.
	_assert_msg_(rd != R_ZERO || (rs1 == R_ZERO && simm12 == 0), "%s write to zero is a HINT", __func__);
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
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::XOR, rs1, simm12));
}

void RiscVEmitter::ORI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::OR, rs1, simm12));
}

void RiscVEmitter::ANDI(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::AND, rs1, simm12));
}

void RiscVEmitter::SLLI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::SLL, rs1, shamt));
}

void RiscVEmitter::SRLI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::SRL, rs1, shamt));
}

void RiscVEmitter::SRAI(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < BitsSupported(), "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM, rd, Funct3::SRL, rs1, shamt | (1 << 10)));
}

void RiscVEmitter::ADD(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::ADD, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SUB(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
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
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::OR, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::AND(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
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
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_WU, rs1, simm12));
}

void RiscVEmitter::LD(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	Write32(EncodeGI(Opcode32::LOAD, rd, Funct3::LS_D, rs1, simm12));
}

void RiscVEmitter::SD(RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	Write32(EncodeGS(Opcode32::STORE, Funct3::LS_D, rs1, rs2, simm12));
}

void RiscVEmitter::ADDIW(RiscVReg rd, RiscVReg rs1, s32 simm12) {
	if (BitsSupported() == 32) {
		ADDI(rd, rs1, simm12);
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
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SLL, rs1, shamt));
}

void RiscVEmitter::SRLIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SRLI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt));
}

void RiscVEmitter::SRAIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	if (BitsSupported() == 32) {
		SRAI(rd, rs1, shamt);
		return;
	}

	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt | (1 << 10)));
}

void RiscVEmitter::ADDW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	if (BitsSupported() == 32) {
		ADD(rd, rs1, rs2);
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
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MUL, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULH(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULH, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULHSU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULHSU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULHU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::MULHU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIV(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::DIV, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::DIVU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::DIVU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REM(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::REM, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::REMU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(SupportsMulDiv(), "%s is only valid with R32M", __func__);
	// Not explicitly a HINT, but seems sensible to restrict just in case.
	_assert_msg_(rd != R_ZERO, "%s write to zero", __func__);
	Write32(EncodeGR(Opcode32::OP, rd, Funct3::REMU, rs1, rs2, Funct7::MULDIV));
}

void RiscVEmitter::MULW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64 && SupportsMulDiv(), "%s is only valid with R64M", __func__);
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
	Write32(EncodeI(Opcode32::LOAD_FP, rd, BitsToFunct3(bits, true), rs1, simm12));
}

void RiscVEmitter::FS(int bits, RiscVReg rs2, RiscVReg rs1, s32 simm12) {
	_assert_msg_(IsGPR(rs1) && IsFPR(rs2), "FS with incorrect register types");
	Write32(EncodeS(Opcode32::STORE_FP, BitsToFunct3(bits, true), rs1, rs2, simm12));
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
		Funct2 fromFmt = BitsToFunct2(FConvToFloatBits(from));
		Funct2 toFmt = BitsToFunct2(FConvToFloatBits(to));
		if (FConvToFloatBits(to) > FConvToFloatBits(from)) {
			_assert_msg_(rm == Round::DYNAMIC || rm == Round::NEAREST_EVEN, "Invalid rounding mode for widening FCVT");
			rm = Round::NEAREST_EVEN;
		}
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
	case FMv::X: bits = 0; break;
	}

	_assert_msg_(BitsSupported() >= bits && FloatBitsSupported() >= bits, "FMV cannot be used for %d bits, only %d/%d supported", bits, BitsSupported(), FloatBitsSupported());
	_assert_msg_((to == FMv::X && from != FMv::X) || (to != FMv::X && from == FMv::X), "%s can only transfer between FPR/GPR", __func__);
	_assert_msg_(to == FMv::X ? IsGPR(rd) : IsFPR(rd), "%s rd of wrong type", __func__);
	_assert_msg_(from == FMv::X ? IsGPR(rs1) : IsFPR(rs1), "%s rs1 of wrong type", __func__);

	Funct5 funct5 = to == FMv::X ? Funct5::FMV_TOX : Funct5::FMV_FROMX;
	Write32(EncodeR(Opcode32::OP_FP, rd, Funct3::FMV, rs1, F0, BitsToFunct2(bits), funct5));
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

};
