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

#include <cstdio>
#include "Common/CPUDetect.h"
#include "Common/RiscVEmitter.h"
#include "UnitTest.h"
#include "ext/riscv-disas.h"

bool TestRiscVEmitter() {
	using namespace RiscVGen;

	cpu_info.RiscV_A = true;
	cpu_info.RiscV_C = true;
	cpu_info.RiscV_D = true;
	cpu_info.RiscV_F = true;
	cpu_info.RiscV_M = true;

	u32 code[1024];
	RiscVEmitter emitter((u8 *)code, (u8 *)code);

	emitter.SetAutoCompress(false);
	emitter.LUI(X1, 0xFFFFF000);
	emitter.AUIPC(X2, 0x70051000);
	emitter.JAL(X3, code);
	emitter.JALR(X1, X2, -12);
	FixupBranch b1 = emitter.JAL(X4);
	emitter.SetJumpTarget(b1);
	emitter.BEQ(X5, X6, code);
	emitter.LB(X7, X8, 42);
	emitter.SB(X9, X10, 1337);
	emitter.ADDI(X11, X12, 42);
	emitter.SLLI(X13, X14, 3);
	emitter.ADD(X15, X16, X17);
	emitter.FENCE(Fence::RW, Fence::RW);

	static constexpr uint32_t expected[] = {
		0xfffff0b7,
		0x70051117,
		0xff9ff1ef,
		0xff4100e7,
		0x0040026f,
		0xfe6286e3,
		0x02a40383,
		0x52950ca3,
		0x02a60593,
		0x00371693,
		0x011807b3,
		0x0330000f,
	};

	ptrdiff_t len = (u32 *)emitter.GetWritableCodePtr() - code;
	EXPECT_EQ_INT(len, ARRAY_SIZE(expected));

	for (ptrdiff_t i = 0; i < len; ++i) {
		EXPECT_EQ_HEX(code[i], expected[i]);
	}

	return true;
}
