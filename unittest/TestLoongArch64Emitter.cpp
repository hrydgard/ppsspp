// Copyright (c) 2025- PPSSPP Project.

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
#include "Common/LoongArch64Emitter.h"
#include "UnitTest.h"

bool TestLoongArch64Emitter(){
    using namespace LoongArch64Gen;

    cpu_info.LOONGARCH_COMPLEX = true;
    cpu_info.LOONGARCH_LAM = true;
    cpu_info.LOONGARCH_UAL = true;
    cpu_info.LOONGARCH_FPU = true;
    cpu_info.LOONGARCH_LSX = true;
    cpu_info.LOONGARCH_LASX = true;
    cpu_info.LOONGARCH_CRC32 = true;
    cpu_info.LOONGARCH_COMPLEX = true;
    cpu_info.LOONGARCH_CRYPTO = true;
    cpu_info.LOONGARCH_LVZ = true;
    cpu_info.LOONGARCH_LBT_X86 = true;
    cpu_info.LOONGARCH_LBT_ARM = true;
    cpu_info.LOONGARCH_LBT_MIPS = true;
    cpu_info.LOONGARCH_PTW = true;

    u32 code[1024];
    LoongArch64Emitter emitter((u8 *)code, (u8 *)code);

    emitter.ADD_W(R12, R13, R14); // t0, t1, t2
    emitter.ADD_D(R12, R13, R14); // t0, t1, t2
    emitter.SUB_W(R12, R13, R14); // t0, t1, t2
    emitter.SUB_D(R12, R13, R14); // t0, t1, t2

    emitter.ADDI_W(R12, R13, 1024); // t0, t1, 1024
    emitter.ADDI_D(R12, R13, 1024); // t0, t1, 1024
    emitter.ADDU16I_D(R12, R13, 16384); // t0, t1, 1024

    emitter.ALSL_W(R12, R13, R14, 4); // t0, t1, t2, 4
    emitter.ALSL_D(R12, R13, R14, 4); // t0, t1, t2, 4
    emitter.ALSL_WU(R12, R13, R14, 4); // t0, t1, t2, 4

    static constexpr uint32_t expected[] = {
        0x001039ac, // add.w
        0x0010b9ac, // add.d
        0x001139ac, // sub.w
        0x0011b9ac, // sub.d

        0x029001ac, // addi.w
        0x02d001ac, // addi.d
        0x110001ac, // addiu16i.d

        0x0005b9ac, // alsl.w
        0x002db9ac, // alsl.d
        0x0007b9ac, // alsl.wu
    };

    ptrdiff_t len = (u32 *)emitter.GetWritableCodePtr() - code;
	EXPECT_EQ_INT(len, ARRAY_SIZE(expected));

	for (ptrdiff_t i = 0; i < len; ++i) {
		EXPECT_EQ_HEX(code[i], expected[i]);
	}

    return true;
}