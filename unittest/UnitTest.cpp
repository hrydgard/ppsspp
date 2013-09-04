// Copyright (c) 2012- PPSSPP Project.

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

// UnitTests
//
// This is a program to directly test various functions, without going
// through a PSP. Especially useful for things like opcode emitters,
// hashes, and various data conversion utility function.
//
// TODO: Make a test of nice unittest asserts and count successes etc.
// Or just integrate with an existing testing framework.


#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

#include "base/NativeApp.h"
#include "Common/ArmEmitter.h"
#include "ext/disarm.h"
#include "math/math_util.h"

#define EXPECT_TRUE(a) if (!(a)) { printf(__FUNCTION__ ":%i: Test Fail\n", __LINE__); return false; }
#define EXPECT_FALSE(a) if ((a)) { printf(__FUNCTION__ ":%i: Test Fail\n", __LINE__); return false; }
#define EXPECT_EQ_FLOAT(a, b) if ((a) != (b)) { printf(__FUNCTION__ ":" __LINE__ ": Test Fail\n%f\nvs\n%f\n", a, b); return false; }
#define EXPECT_EQ_STR(a, b) if (a != b) { printf(__FUNCTION__ ": Test Fail\n%s\nvs\n%s\n", a.c_str(), b.c_str()); return false; }

#define RET(a) if (!(a)) { return false; }

std::string System_GetProperty(SystemProperty prop) { return ""; }

bool CheckLast(ArmGen::ARMXEmitter &emit, const char *comp) {
	u32 instr;
	memcpy(&instr, emit.GetCodePtr() - 4, 4);
	char disasm[512];
	ArmDis(0, instr, disasm);
	EXPECT_EQ_STR(std::string(disasm), std::string(comp));
	return true;
}


bool TestArmEmitter() {
	using namespace ArmGen;

	u32 code[512];
	ARMXEmitter emitter((u8 *)code);
	emitter.LDR(R3, R7);
	RET(CheckLast(emitter, "e5973000 LDR r3, [r7, #0]"));
	emitter.VLDR(S3, R8, 48);
	RET(CheckLast(emitter, "edd81a0c VLDR s3, [r8, #48]"));
	emitter.VSTR(S5, R12, -36);
	RET(CheckLast(emitter, "ed4c2a09 VSTR s5, [r12, #-36]"));
	emitter.VADD(S1, S2, S3);
	RET(CheckLast(emitter, "ee710a21 VADD s1, s2, s3"));
	emitter.VMUL(S7, S8, S9);
	RET(CheckLast(emitter, "ee643a24 VMUL s7, s8, s9"));
	emitter.VMLA(S7, S8, S9);
	RET(CheckLast(emitter, "ee443a24 VMLA s7, s8, s9"));
	emitter.VNMLA(S7, S8, S9);
	RET(CheckLast(emitter, "ee543a64 VNMLA s7, s8, s9"));
	emitter.VABS(S1, S2);
	RET(CheckLast(emitter, "eef00ac1 VABS s1, s2"));
	emitter.VSQRT(S1, S2);
	RET(CheckLast(emitter, "eef10ac1 VSQRT s1, s2"));
	emitter.VDIV(S1, S2, S3);
	RET(CheckLast(emitter, "eec10a21 VDIV s1, s2, s3"));
	emitter.VMRS(R1);
	RET(CheckLast(emitter, "eef11a10 VMRS r1"));
	emitter.VMSR(R7);
	RET(CheckLast(emitter, "eee17a10 VMSR r7"));
	emitter.VMRS_APSR();
	RET(CheckLast(emitter, "eef1fa10 VMRS APSR"));
	emitter.VCVT(S0, S1, TO_INT | IS_SIGNED);
	RET(CheckLast(emitter, "eebd0a60 VCVT ..."));


	// WTF?
	//emitter.VSUB(S4, S5, S6);
	//RET(CheckLast(emitter, "ee322ac3 VSUB s4, s5, s6"));


	emitter.VMOV(S3, S6);
	RET(CheckLast(emitter, "eef01a43 VMOV s3, s6"));
	return true;
}

bool TestMathUtil() {
	EXPECT_FALSE(my_isinf(1.0));
	volatile float zero = 0.0f;
	EXPECT_TRUE(my_isinf(1.0f/zero));
	EXPECT_FALSE(my_isnan(1.0f/zero));
	return true;
}

int main(int argc, const char *argv[])
{
	TestArmEmitter();
	TestMathUtil();
	return 0;
}