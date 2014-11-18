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
	Write32Fields(26, 0x00000000, 6, code & 0xFFFFF, 0, 0x0000000d);
}

}