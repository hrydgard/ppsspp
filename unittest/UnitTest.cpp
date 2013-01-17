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


#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "Common/ArmEmitter.h"
#include "ext/disarm.h"

#define EXPECT_EQ_STR(a, b) if ((a) != (b)) { printf(__FUNCTION__ ": Test Fail\n%s\nvs\n%s\n", a.c_str(), b.c_str()); return false; }

bool TestArmEmitter() {
	using namespace ArmGen;

	u32 code[512];
	ARMXEmitter emitter((u8 *)code);
	emitter.LDR(R3, R7);

	char disasm[512];
	ArmDis(0, code[0] & 0xFFFFFFFF, disasm);
	std::string dis(disasm);
	EXPECT_EQ_STR(dis, std::string("e4973000 LDR r3, [r7], #0"));
	
	printf("TestArmEmitter: Success\n");

	return true;
}

int main(int argc, const char *argv[])
{
	TestArmEmitter();
	return 0;
}