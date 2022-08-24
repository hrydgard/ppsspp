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

enum class Opcode32 {
	// Note: invalid, just used for FixupBranch.
	ZERO = 0b0000000,
	SYSTEM = 0b1110011,
};

enum class Funct3 {
	// Note: invalid, just used for FixupBranch.
	ZERO = 0b000,
	PRIV = 0b000,
};

enum class Funct2 {
	// TODO: 0b00,
};

enum class Funct7 {
	// TODO: 0b0000000,
};

enum class Funct12 {
	ECALL = 0b000000000000,
	EBREAK = 0b000000000001,
};

static inline u32 EncodeR(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct7 funct7) {
	return (u32)opcode | ((u32)rd << 7) | ((u32)funct3 << 12) | ((u32)rs1 << 15) | ((u32)rs2 << 20) | ((u32)funct7 << 25);
}

static inline u32 EncodeR4(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, Funct2 funct2, RiscVReg rs3) {
	return (u32)opcode | ((u32)rd << 7) | ((u32)funct3 << 12) | ((u32)rs1 << 15) | ((u32)rs2 << 20) | ((u32)funct2 << 25) | ((u32)rs3 << 27);
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, s32 simm12) {
	_assert_msg_(((simm12 << 20) >> 20) == simm12, "I immediate must be signed s11.0");
	return (u32)opcode | ((u32)rd << 7) | ((u32)funct3 << 12) | ((u32)rs1 << 15) | ((u32)simm12 << 20);
}

static inline u32 EncodeI(Opcode32 opcode, RiscVReg rd, Funct3 funct3, RiscVReg rs1, Funct12 funct12) {
	return EncodeI(opcode, rd, funct3, rs1, (s32)funct12);
}

static inline u32 EncodeS(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm12) {
	_assert_msg_(((simm12 << 20) >> 20) == simm12, "S immediate must be signed s11.0");
	u32 imm4_0 = simm12 & 0x1F;
	u32 imm11_5 = (simm12 >> 5) & 0x7F;
	return (u32)opcode | ((u32)imm4_0 << 7) | ((u32)funct3 << 12) | ((u32)rs1 << 15) | ((u32)rs2 << 20) | ((u32)imm11_5 << 25);
}

static inline u32 EncodeB(Opcode32 opcode, Funct3 funct3, RiscVReg rs1, RiscVReg rs2, s32 simm13) {
	_assert_msg_(((simm13 << 19) >> 19) == simm13, "B immediate must be signed s12.0");
	_assert_msg_((simm13 & 1) == 0, "B immediate must be even");
	u32 imm11 = (simm13 >> 11) & 1;
	u32 imm12 = (simm13 >> 12) & 1;
	// This weird encoding scheme is to keep most bits the same as S, but keep sign at 31.
	u32 imm4_1_11 = (simm13 & 0x1E) | imm11;
	u32 imm12_10_5 = (imm12 << 6) | ((simm13 >> 5) & 0x3F);
	return (u32)opcode | ((u32)imm4_1_11 << 7) | ((u32)funct3 << 12) | ((u32)rs1 << 15) | ((u32)rs2 << 20) | ((u32)imm12_10_5 << 25);
}

static inline u32 EncodeU(Opcode32 opcode, RiscVReg rd, s32 simm32) {
	_assert_msg_((simm32 & 0x0FFF) == 0, "U immediate must not have lower 12 bits set");
	return (u32)opcode | ((u32)rd << 7) | (u32)simm32;
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
	return (u32)opcode | ((u32)rd << 7) | ((u32)imm20_10_1_11_19_12 << 12);
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

void RiscVEmitter::EBREAK() {
	Write32(EncodeI(Opcode32::SYSTEM, R_ZERO, Funct3::PRIV, R_ZERO, Funct12::EBREAK));
}

};
