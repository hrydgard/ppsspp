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

#include "base/logging.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MemoryUtil.h"
#include "MipsEmitter.h"
#include "CPUDetect.h"

namespace MIPSGen {
void MIPSXEmitter::SetCodePtr(u8 *ptr) {
	code_ = ptr;
	lastCacheFlushEnd_ = ptr;
}

void MIPSXEmitter::ReserveCodeSpace(u32 bytes) {
	for (u32 i = 0; i < bytes / 4; ++i) {
		BREAK(0);
	}
}

const u8 *MIPSXEmitter::AlignCode16() {
	ReserveCodeSpace((-(intptr_t)code_) & 15);
	return code_;
}

const u8 *MIPSXEmitter::AlignCodePage() {
	// TODO: Assuming code pages ought to be 4K?
	ReserveCodeSpace((-(intptr_t)code_) & 4095);
	return code_;
}

const u8 *MIPSXEmitter::GetCodePtr() const {
	return code_;
}

u8 *MIPSXEmitter::GetWritableCodePtr() {
	return code_;
}

void MIPSXEmitter::FlushIcache() {
	FlushIcacheSection(lastCacheFlushEnd_, code_);
	lastCacheFlushEnd_ = code_;
}

void MIPSXEmitter::FlushIcacheSection(u8 *start, u8 *end) {
#if defined(MIPS)
	// TODO: Completely untested.
#ifdef __clang__
	__clear_cache(start, end);
#else
	__builtin___clear_cache(start, end);
#endif
#endif
}

void MIPSXEmitter::BREAK(u32 code) {
	// 000000 iiiiiiiiiiiiiiiiiiii 001101
	_dbg_assert_msg_(JIT, code <= 0xfffff, "Bad emitter arguments");
	Write32Fields(26, 0x00, 6, code & 0xfffff, 0, 0x0d);
}

FixupBranch MIPSXEmitter::J() {
	// 000010 iiiiiiiiiiiiiiiiiiiiiiiii (fix up)
	FixupBranch b = MakeFixupBranch(BRANCH_26);
	Write32Fields(26, 0x02);
	return b;
}

void MIPSXEmitter::J(const void *func) {
	SetJumpTarget(J(), func);
}

FixupBranch MIPSXEmitter::JAL() {
	// 000011 iiiiiiiiiiiiiiiiiiiiiiiii (fix up)
	FixupBranch b = MakeFixupBranch(BRANCH_26);
	Write32Fields(26, 0x03);
	return b;
}

void MIPSXEmitter::JAL(const void *func) {
	SetJumpTarget(JAL(), func);
}

FixupBranch MIPSXEmitter::BEQ(MIPSReg rs, MIPSReg rt) {
	// 000100 sssss ttttt iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(JIT, rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x04, 21, rs, 16, rt);
	return b;
}

void MIPSXEmitter::BEQ(MIPSReg rs, MIPSReg rt, const void *func) {
	SetJumpTarget(BEQ(rs, rt), func);
}

FixupBranch MIPSXEmitter::BNE(MIPSReg rs, MIPSReg rt) {
	// 000101 sssss ttttt iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(JIT, rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x05, 21, rs, 16, rt);
	return b;
}

void MIPSXEmitter::BNE(MIPSReg rs, MIPSReg rt, const void *func) {
	SetJumpTarget(BNE(rs, rt), func);
}

FixupBranch MIPSXEmitter::BLEZ(MIPSReg rs) {
	// 000110 sssss xxxxx iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(JIT, rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x06, 21, rs);
	return b;
}

void MIPSXEmitter::BLEZ(MIPSReg rs, const void *func) {
	SetJumpTarget(BLEZ(rs), func);
}

FixupBranch MIPSXEmitter::BGTZ(MIPSReg rs) {
	// 000111 sssss xxxxx iiiiiiiiiiiiiii (fix up)
	_dbg_assert_msg_(JIT, rs < F_BASE, "Bad emitter arguments");
	FixupBranch b = MakeFixupBranch(BRANCH_16);
	Write32Fields(26, 0x07, 21, rs);
	return b;
}

void MIPSXEmitter::BGTZ(MIPSReg rs, const void *func) {
	SetJumpTarget(BGTZ(rs), func);
}

void MIPSXEmitter::SetJumpTarget(const FixupBranch &branch) {
	SetJumpTarget(branch, code_);
}

void MIPSXEmitter::SetJumpTarget(const FixupBranch &branch, const void *dst) {
	const intptr_t srcp = (intptr_t)branch.ptr;
	const intptr_t dstp = (intptr_t)dst;
	u32 *fixup = (u32 *)branch.ptr;

	_dbg_assert_msg_(JIT, (dstp & 3) == 0, "Destination should be aligned");

	if (branch.type == BRANCH_16) {
		// The distance is encoded as words from the delay slot.
		ptrdiff_t distance = (dstp - srcp - 4) >> 2;
		_dbg_assert_msg_(JIT, distance >= -0x8000 && distance < 0x8000, "Destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0xffff0000) | (distance & 0x0000ffff);
	} else {
		// Absolute, easy.
		_dbg_assert_msg_(JIT, (srcp & 0xf0000000) != (dstp & 0xf0000000), "Destination is too far away (%p -> %p)", branch.ptr, dst);
		*fixup = (*fixup & 0xfc000000) | ((dstp >> 2) & 0x03ffffff);
	}
}

FixupBranch MIPSXEmitter::MakeFixupBranch(FixupBranchType type) {
	FixupBranch b;
	b.ptr = code_;
	b.type = type;
	return b;
}

void MIPSXEmitter::LW(MIPSReg value, MIPSReg base, s16 offset) {
	// 100011 xxxxx ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(JIT, value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x23, 21, base, 16, value, 0, (u16)offset);
}

void MIPSXEmitter::SW(MIPSReg value, MIPSReg base, s16 offset) {
	// 101011 sssss ttttt iiiiiiiiiiiiiiii - rs = base, rt = value
	_dbg_assert_msg_(JIT, value < F_BASE && base < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x2b, 21, base, 16, value, 0, (u16)offset);
}

void MIPSXEmitter::SLL(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000000
	_dbg_assert_msg_(JIT, rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x00);
}

void MIPSXEmitter::SRL(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000010
	_dbg_assert_msg_(JIT, rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x02);
}

void MIPSXEmitter::SRA(MIPSReg rd, MIPSReg rt, u8 sa) {
	// 000000 xxxxx ttttt ddddd aaaaa 000011
	_dbg_assert_msg_(JIT, rd < F_BASE && rt < F_BASE && sa <= 0x1f, "Bad emitter arguments");
	Write32Fields(26, 0x00, 16, rt, 11, rd, 6, sa & 0x1f, 0, 0x03);
}

void MIPSXEmitter::ADDIU(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001001 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x09, 21, rs, 16, rt, 0, (u16)imm);
}
void MIPSXEmitter::SLTIU(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001011 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0b, 21, rs, 16, rt, 0, (u16)imm);
}
void MIPSXEmitter::ANDI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001100 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0c, 21, rs, 16, rt, 0, (u16)imm);
}
void MIPSXEmitter::ORI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001101 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0d, 21, rs, 16, rt, 0, (u16)imm);
}
void MIPSXEmitter::XORI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001110 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0e, 21, rs, 16, rt, 0, (u16)imm);
}
void MIPSXEmitter::LUI(MIPSReg rt, MIPSReg rs, s16 imm) {
	// 001111 sssss ttttt iiiiiiiiiiiiiiii
	_dbg_assert_msg_(JIT, rs < F_BASE && rt < F_BASE, "Bad emitter arguments");
	Write32Fields(26, 0x0f, 21, rs, 16, rt, 0, (u16)imm);
}

}