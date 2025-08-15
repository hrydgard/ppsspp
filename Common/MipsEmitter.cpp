// Copyright (c) 2014- PPSSPP Project.

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
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/MipsEmitter.h"
#include "Common/CPUDetect.h"

namespace MIPSGen {
void MIPSEmitter::SetCodePointer(const u8 *ptr, u8 *writePtr) {
	code_ = writePtr;
	lastCacheFlushEnd_ = writePtr;
}

const u8 *MIPSEmitter::GetCodePointer() const {
	return code_;
}

void MIPSEmitter::ReserveCodeSpace(u32 bytes) {
	for (u32 i = 0; i < bytes / 4; ++i) {
		BREAK(0);
	}
}

const u8 *MIPSEmitter::AlignCode16() {
	ReserveCodeSpace((-(intptr_t)code_) & 15);
	return code_;
}

const u8 *MIPSEmitter::AlignCodePage() {
	// TODO: Assuming code pages ought to be 4K?
	ReserveCodeSpace((-(intptr_t)code_) & 4095);
	return code_;
}

const u8 *MIPSEmitter::GetCodePtr() const {
	return code_;
}

u8 *MIPSEmitter::GetWritableCodePtr() {
	return code_;
}

void MIPSEmitter::FlushIcache() {
	FlushIcacheSection(lastCacheFlushEnd_, code_);
	lastCacheFlushEnd_ = code_;
}

void MIPSEmitter::FlushIcacheSection(u8 *start, u8 *end) {
#if PPSSPP_ARCH(MIPS)
#ifdef __clang__
	__clear_cache(start, end);
#else
	__builtin___clear_cache(start, end);
#endif
#endif
}

void MIPSEmitter::BREAK(u32 code) {
	// 000000 iiiiiiiiiiiiiiiiiiii 001101
	_dbg_assert_msg_(code <= 0xfffff, "Bad emitter arguments");
	Write32Fields(26, 0x00, 6, code & 0xfffff, 0, 0x0d);
}

FixupBranch MIPSEmitter::J(std::function<void ()> delaySlot) {
	// 000010 iiiiiiiiiiiiiiiiiiiiiiiiii (fix up)
	FixupBranch b = MakeFixupBranch(BRANCH_26);
	Write32Fields(26, 0x02);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::J(const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(J(delaySlot), func);
}

FixupBranch MIPSEmitter::JAL(std::function<void ()> delaySlot) {
	// 000011 iiiiiiiiiiiiiiiiiiiiiiiiii (fix up)
	FixupBranch b = MakeFixupBranch(BRANCH_26);
	Write32Fields(26, 0x03);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::JAL(const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(JAL(delaySlot), func);
}

void MIPSEmitter::JR(MIPSReg rs, std::function<void ()> delaySlot) {
	// 000000 sssss xxxxxxxxxx hint- 001000 (hint must be 0.)
	_dbg_assert_msg_(rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 0, 0x08);
	ApplyDelaySlot(delaySlot);
}

void MIPSEmitter::JALR(MIPSReg rd, MIPSReg rs, std::function<void ()> delaySlot) {
	// 000000 sssss xxxxx ddddd hint- 001001 (hint must be 0.)
	_dbg_assert_msg_(rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 11, rd, 0, 0x09);
	ApplyDelaySlot(delaySlot);
}

FixupBranch MIPSEmitter::BLTZ(MIPSReg rs, std::function<void ()> delaySlot) {
	// 000001 sssss xxxxx iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x01, 21, rs);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::BLTZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(BLTZ(rs, delaySlot), func);
}

FixupBranch MIPSEmitter::BEQ(MIPSReg rs, MIPSReg rt, std::function<void ()> delaySlot) {
	// 000100 sssss ttttt iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x04, 21, rs, 16, rt);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::BEQ(MIPSReg rs, MIPSReg rt, const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(BEQ(rs, rt, delaySlot), func);
}

FixupBranch MIPSEmitter::BNE(MIPSReg rs, MIPSReg rt, std::function<void ()> delaySlot) {
	// 000101 sssss ttttt iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x05, 21, rs, 16, rt);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::BNE(MIPSReg rs, MIPSReg rt, const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(BNE(rs, rt, delaySlot), func);
}

FixupBranch MIPSEmitter::BLEZ(MIPSReg rs, std::function<void ()> delaySlot) {
	// 000110 sssss xxxxx iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x06, 21, rs);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::BLEZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(BLEZ(rs, delaySlot), func);
}

FixupBranch MIPSEmitter::BGTZ(MIPSReg rs, std::function<void ()> delaySlot) {
	// 000111 sssss xxxxx iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x07, 21, rs);
	ApplyDelaySlot(delaySlot);
	return b;
}

void MIPSEmitter::BGTZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot) {
	SetJumpTarget(BGTZ(rs, delaySlot), func);
}

void MIPSEmitter::SetJumpTarget(const FixupBranch &branch) {
	SetJumpTarget(branch, code_);
}

bool MIPSEmitter::BInRange(const void *func) {
	return BInRange(code_, func);
}

bool MIPSEmitter::JInRange(const void *func) {
	return JInRange(code_, func);
}

void MIPSEmitter::SetJumpTarget(const FixupBranch &branch, const void *dst) {
	const intptr_t srcp = (intptr_t)branch.ptr;
	const intptr_t dstp = (intptr_t)dst;
	u32 *fixup = (u32 *)branch.ptr;

	_dbg_assert_msg_((dstp & 3) == 0, "Destination should be aligned");

	if (branch.type == BRANCH_16) {
		// The distance is encoded as words from the delay slot.
		ptrdiff_t distance = (dstp - srcp - 4) >> 2;
		_dbg_assert_msg_(BInRange(branch.ptr, dst), "Destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0xffff0000) | (distance & 0x0000ffff);
	} else {
		// Absolute, easy.
		_dbg_assert_msg_(JInRange(branch.ptr, dst), "Destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0xfc000000) | ((dstp >> 2) & 0x03ffffff);
	}
}

bool MIPSEmitter::BInRange(const void *src, const void *dst) {
	const intptr_t srcp = (intptr_t)src;
	const intptr_t dstp = (intptr_t)dst;

	// The distance is encoded as words from the delay slot.
	ptrdiff_t distance = (dstp - srcp - 4) >> 2;
	return distance >= -0x8000 && distance < 0x8000;
}

bool MIPSEmitter::JInRange(const void *src, const void *dst) {
	const intptr_t srcp = (intptr_t)src;
	const intptr_t dstp = (intptr_t)dst;

	return (srcp - (srcp & 0x0fffffff)) == (dstp - (dstp & 0x0fffffff));
}

void MIPSEmitter::ApplyDelaySlot(std::function<void ()> delaySlot) {
	if (delaySlot) {
		delaySlot();
	} else {
		// We just insert a NOP if there's no delay slot provided.  Safer.
		NOP();
	}
}

void MIPSEmitter::QuickCallFunction(MIPSReg scratchreg, const void *func) {
	_dbg_assert_msg_(scratchreg < F_BASE, "Bad emitter arguments");
	if (JInRange(func)) {
		JAL(func);
	} else {
		// This may never happen.
		MOVP2R(scratchreg, func);
		JALR(scratchreg);
	}
}

FixupBranch MIPSEmitter::MakeFixupBranch(FixupBranchType type) const {
	FixupBranch b;
	b.ptr = code_;
	b.type = type;
	return b;
}

void MIPSEmitter::LB(MIPSReg value, MIPSReg base, s16 offset) {
	// 100000 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x20, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::LH(MIPSReg value, MIPSReg base, s16 offset) {
	// 100001 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x21, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::LW(MIPSReg value, MIPSReg base, s16 offset) {
	// 100011 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x23, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::SB(MIPSReg value, MIPSReg base, s16 offset) {
	// 101000 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x28, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::SH(MIPSReg value, MIPSReg base, s16 offset) {
	// 101001 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x29, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::SW(MIPSReg value, MIPSReg base, s16 offset) {
	// 101011 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x2b, 21, base, 16, value, 0, (u16)offset);
}

void MIPSEmitter::SLL(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000000
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x00);
}

void MIPSEmitter::SRL(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000010
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x02);
}

void MIPSEmitter::SRA(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000011
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x03);
}

void MIPSEmitter::SLLV(MIPSReg rd, MIPSReg rt, MIPSReg rs) {
	// 000000 sssss ttttt ddddd xxxxx 000100
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x04);
}

void MIPSEmitter::SRLV(MIPSReg rd, MIPSReg rt, MIPSReg rs) {
	// 000000 sssss ttttt ddddd xxxxx 000110
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x06);
}

void MIPSEmitter::SRAV(MIPSReg rd, MIPSReg rt, MIPSReg rs) {
	// 000000 sssss ttttt ddddd xxxxx 000111
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x07);
}

void MIPSEmitter::SLT(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd xxxxx 101010
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x2a);
}

void MIPSEmitter::SLTU(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd xxxxx 101011
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x2b);
}

void MIPSEmitter::SLTI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001010 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0a, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::SLTIU(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001011 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0b, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::ADDU(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd 00000100001
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x21);
}

void MIPSEmitter::SUBU(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd 00000100011
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x23);
}

void MIPSEmitter::ADDIU(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001001 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x09, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::AND(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd 00000100100
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x24);
}

void MIPSEmitter::OR(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd 00000100101
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x25);
}

void MIPSEmitter::XOR(MIPSReg rd, MIPSReg rs, MIPSReg rt) {
	// 000000 sssss ttttt ddddd 00000100110
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && rs < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x00, 21, rs, 16, rt, 11, rd, 0, 0x26);
}

void MIPSEmitter::ANDI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001100 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0c, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::ORI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001101 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0d, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::XORI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001110 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0e, 21, rs, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::LUI(MIPSReg rt, s16 imm) {
	// 001111 00000 ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0f, 16, rt, 0, (u16)imm);
}

void MIPSEmitter::INS(MIPSReg rt, MIPSReg rs, s8 pos, s8 size) {
	// 011111 sssss ttttt xxxxx yyyyy 000100
	_dbg_assert_msg_(rt < F_BASE && rs < F_BASE && pos <= 0x1f && (size+pos+1) <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x1f, 21, rt, 16, rs, 11, (size+pos+1) & 0x1f, 6, pos & 0x1f, 0, 0x04);
}

void MIPSEmitter::EXT(MIPSReg rt, MIPSReg rs, s8 pos, s8 size) {
	// 111111 sssss ttttt xxxxx yyyyy 000000
	_dbg_assert_msg_(rt < F_BASE && rs < F_BASE && pos <= 0x1f && size >= 1, "Bad emitter arguments");
	Write32Fields(26, 0x3f, 21, rt, 16, rs, 11, (size-1) & 0x1f, 6, pos & 0x1f, 0, 0x00);
}

void MIPSEmitter::DSLL(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 111000 DSLL
	// 000000 xxxxx ttttt ddddd aaaaa 111100 DSLL32
	_dbg_assert_msg_(rd < F_BASE && rt < F_BASE && sa <= 0x3f, "Bad emitter arguments");
	// TODO: Assert MIPS64.
	if (sa >= 32) {
		Write32Fields(26, 0x00, 16, rt, 11, rd, 6, (sa - 32) & 0x1f, 0, 0x3c);
	} else {
		Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x38);
	}
}

void MIPSEmitter::MOVI2R(MIPSReg reg, u64 imm) {
	_dbg_assert_msg_(reg < F_BASE, "Bad emitter arguments");
	// TODO: Assert MIPS64.

	// Probably better to use a literal pool and load.
	LUI(reg, imm >> 48);
	ORI(reg, reg, (imm >> 32) & 0x0000ffff);
	DSLL(reg, reg, 16);
	ORI(reg, reg, (imm >> 16) & 0x0000ffff);
	DSLL(reg, reg, 16);
	ORI(reg, reg, (imm >> 0) & 0x0000ffff);
}

void MIPSEmitter::MOVI2R(MIPSReg reg, u32 imm) {
	_dbg_assert_msg_(reg < F_BASE, "Bad emitter arguments");

	if ((imm & 0xffff0000) != 0) {
#if 0
		// TODO: CPUDetect MIPS64.  Ideally allow emitter to emit MIPS32 on x64.
		ORI(reg, R_ZERO, imm >> 16);
		DSLL(reg, reg, 16);
		ORI(reg, reg, imm & 0x0000ffff);
#else
		LUI(reg, imm >> 16);
		ORI(reg, reg, imm & 0x0000ffff);
#endif
	} else {
		ORI(reg, R_ZERO, imm & 0x0000ffff);
	}
}

void MIPSCodeBlock::PoisonMemory(int offset) {
	u32 *ptr = (u32 *)(region + offset);
	u32 *maxptr = (u32 *)(region + region_size - offset);
	// If our memory isn't a multiple of u32 then this won't write the last remaining bytes with anything
	// Less than optimal, but there would be nothing we could do but throw a runtime warning anyway.
	// AArch64: 0x0000000d = break 0
	while (ptr < maxptr)
		*ptr++ = 0x0000000d;
}

}
