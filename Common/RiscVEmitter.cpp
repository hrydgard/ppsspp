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

static inline bool SupportsMulDiv() {
	// TODO
	return true;
}

enum class Opcode32 {
	// Note: invalid, just used for FixupBranch.
	ZERO = 0b0000000,
	LOAD = 0b0000011,
	MISC_MEM = 0b0001111,
	OP_IMM = 0b0010011,
	AUIPC = 0b0010111,
	OP_IMM_32 = 0b0011011,
	STORE = 0b0100011,
	OP = 0b0110011,
	LUI = 0b0110111,
	OP_32 = 0b0111011,
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
};

enum class Funct2 {
	// TODO: 0b00,
};

enum class Funct7 {
	ZERO = 0b0000000,

	SUB = 0b0100000,
	SRA = 0b0100000,

	MULDIV = 0b0000001,
};

enum class Funct12 {
	ECALL = 0b000000000000,
	EBREAK = 0b000000000001,
};

static inline RiscVReg DecodeReg(RiscVReg reg) { return (RiscVReg)(reg & 0x1F); }
static inline bool IsGPR(RiscVReg reg) { return reg < 0x20; }
static inline bool IsFPR(RiscVReg reg) { return (reg & 0x20) != 0 && (int)reg < 0x40; }

static inline u32 EncodeR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct7 funct7) {
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)funct7 << 25);
}

static inline u32 EncodeGR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct7 funct7) {
	_assert_msg_(IsGPR(rd), "R instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "R instruction rs1 must be GPR");
	_assert_msg_(IsGPR(rs2), "R instruction rs2 must be GPR");
	return EncodeR(opcode, rd, funct3, rs1, rs2, funct7);
}

static inline u32 EncodeR4(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, RiscVReg rs3) {
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)funct2 << 25) | ((u32)DecodeReg(rs3) << 27);
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, s32 simm12) {
	_assert_msg_(((simm12 << 20) >> 20) == simm12, "I immediate must be signed s11.0");
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)simm12 << 20);
}

static inline u32 EncodeGI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, s32 simm12) {
	_assert_msg_(IsGPR(rd), "I instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "I instruction rs1 must be GPR");
	return EncodeI(opcode, rd, funct3, rs1, simm12);
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct12 funct12) {
	return EncodeI(opcode, rd, funct3, rs1, (s32)funct12);
}

static inline u32 EncodeGI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct12 funct12) {
	_assert_msg_(IsGPR(rd), "I instruction rd must be GPR");
	_assert_msg_(IsGPR(rs1), "I instruction rs1 must be GPR");
	return EncodeI(opcode, rd, funct3, rs1, funct12);
}

static inline u32 EncodeS(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm12) {
	_assert_msg_(((simm12 << 20) >> 20) == simm12, "S immediate must be signed s11.0");
	u32 imm4_0 = simm12 & 0x1F;
	u32 imm11_5 = (simm12 >> 5) & 0x7F;
	return (u32)opcode | ((u32)imm4_0 << 7) | ((u32)funct3 << 12) | ((u32)DecodeReg(rs1) << 15) | ((u32)DecodeReg(rs2) << 20) | ((u32)imm11_5 << 25);
}

static inline u32 EncodeGS(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm12) {
	_assert_msg_(IsGPR(rs1), "S instruction rs1 must be GPR");
	_assert_msg_(IsGPR(rs2), "S instruction rs2 must be GPR");
	return EncodeS(opcode, funct3, rs1, rs2, simm12);
}

static inline u32 EncodeB(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm13) {
	_assert_msg_(((simm13 << 19) >> 19) == simm13, "B immediate must be signed s12.0");
	_assert_msg_((simm13 & 1) == 0, "B immediate must be even");
	u32 imm11 = (simm13 >> 11) & 1;
	u32 imm12 = (simm13 >> 12) & 1;
	// This weird encoding scheme is to keep most bits the same as S, but keep sign at 31.
	u32 imm4_1_11 = (simm13 & 0x1E) | imm11;
	u32 imm12_10_5 = (imm12 << 6) | ((simm13 >> 5) & 0x3F);
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
	_assert_msg_(((simm21 << 11) >> 11) == simm21, "J immediate must be signed s20.0");
	_assert_msg_((simm21 & 1) == 0, "J immediate must be even");
	u32 imm11 = (simm21 >> 11) & 1;
	u32 imm20 = (simm21 >> 20) & 1;
	u32 imm10_1 = (simm21 >> 1) & 0x03FF;
	u32 imm19_12 = (simm21 >> 12) & 0x00FF;
	// This encoding scheme tries to keep the bits from B in the same places, plus sign.
	u32 imm20_10_1_11_19_12 = (imm20 << 19) | (imm10_1 << 9) | (imm11 << 8) | imm19_12;
	return (u32)opcode | ((u32)DecodeReg(rd) << 7) | ((u32)imm20_10_1_11_19_12 << 12);
}

static inline u32 EncodeGJ(Opcode32 opcode, RiscVReg rd, s32 simm21) {
	_assert_msg_(IsGPR(rd), "J instruction rd must be GPR");
	return EncodeJ(opcode, rd, simm21);
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

void RiscVEmitter::SetJumpTarget(const FixupBranch &branch) {
	SetJumpTarget(branch, code_);
}

void RiscVEmitter::SetJumpTarget(const FixupBranch &branch, const void *dst) {
	const intptr_t srcp = (intptr_t)branch.ptr;
	const intptr_t dstp = (intptr_t)dst;
	const ptrdiff_t writable_delta = writable_ - code_;
	u32 *fixup = (u32 *)(branch.ptr + writable_delta);

	_assert_msg_((dstp & 1) == 0, "Destination should be aligned");
	_assert_msg_((dstp & 3) == 0 || SupportsCompressed(), "Destination should be aligned (no compressed)");

	ptrdiff_t distance = dstp - srcp;
	_assert_msg_((distance & 1) == 0, "Distance should be aligned");
	_assert_msg_((distance & 3) == 0 || SupportsCompressed(), "Distance should be aligned (no compressed)");

	switch (branch.type) {
	case FixupBranchType::B:
		_assert_msg_(BInRange(branch.ptr, dst), "B destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0x01FFF07F) | EncodeB(Opcode32::ZERO, Funct3::ZERO, R_ZERO, R_ZERO, (s32)distance);
		break;

	case FixupBranchType::J:
		_assert_msg_(JInRange(branch.ptr, dst), "J destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0x00000FFF) | EncodeJ(Opcode32::ZERO, R_ZERO, (s32)distance);
		break;
	}
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

	return distance <= 0x00000FFE && -distance <= 0x00000FFE;
}

bool RiscVEmitter::JInRange(const void *src, const void *dst) const {
	const intptr_t srcp = (intptr_t)src;
	const intptr_t dstp = (intptr_t)dst;
	ptrdiff_t distance = dstp - srcp;

	return distance <= 0x000FFFFE && -distance <= 0x000FFFFE;
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
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::ADD, rs1, simm12));
}

void RiscVEmitter::SLLIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SLL, rs1, shamt));
}

void RiscVEmitter::SRLIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt));
}

void RiscVEmitter::SRAIW(RiscVReg rd, RiscVReg rs1, u32 shamt) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	// Not sure if shamt=0 is legal or not, let's play it safe.
	_assert_msg_(shamt > 0 && shamt < 32, "Shift out of range");
	Write32(EncodeGI(Opcode32::OP_IMM_32, rd, Funct3::SRL, rs1, shamt | (1 << 10)));
}

void RiscVEmitter::ADDW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ADD, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SUBW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::ADD, rs1, rs2, Funct7::SUB));
}

void RiscVEmitter::SLLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SLL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
	_assert_msg_(rd != R_ZERO, "%s write to zero is a HINT", __func__);
	Write32(EncodeGR(Opcode32::OP_32, rd, Funct3::SRL, rs1, rs2, Funct7::ZERO));
}

void RiscVEmitter::SRAW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2) {
	_assert_msg_(BitsSupported() >= 64, "%s is only valid with R64I", __func__);
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

};
