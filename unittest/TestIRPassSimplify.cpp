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
#include <cstring>
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRPassSimplify.h"

struct IRVerification {
	const char *name;
	const std::vector<IRInst> input;
	const std::vector<IRInst> expected;
	const std::vector<IRPassFunc> passes;
};

static void LogInstructions(const std::vector<IRInst> &insts) {
	for (size_t i = 0; i < insts.size(); ++i) {
		char buf[256];
		DisassembleIR(buf, sizeof(buf), insts[i]);
		printf("  %s\n", buf);
	}
}

static bool VerifyPass(const IRVerification &v) {
	IRWriter in, out;
	IROptions opts{};
	opts.unalignedLoadStore = true;

	for (const auto &inst : v.input)
		in.Write(inst);
	if (IRApplyPasses(v.passes.data(), v.passes.size(), in, out, opts)) {
		printf("%s FAILED: Unable to apply passes (or wanted to log)\n", v.name);
		return false;
	}

	const std::vector<IRInst> actual = out.GetInstructions();
	if (actual.size() != v.expected.size()) {
		printf("%s FAILED: produced %d instructions, expected %d\n", v.name, (int)actual.size(), (int)v.expected.size());
		printf("Actual:\n");
		LogInstructions(actual);
		printf("Expected:\n");
		LogInstructions(v.expected);
		return false;
	}

	for (size_t i = 0; i < actual.size(); ++i) {
		if (memcmp(&v.expected[i], &actual[i], sizeof(IRInst)) != 0) {
			char actualBuf[256];
			DisassembleIR(actualBuf, sizeof(actualBuf), actual[i]);
			char expectedBuf[256];
			DisassembleIR(expectedBuf, sizeof(expectedBuf), v.expected[i]);

			if (strcmp(expectedBuf, actualBuf) == 0) {
				// This means a field (like src2) was left set but isn't relevant.  Ignore.
				continue;
			}

			printf("%s FAILED: #%d expected '%s' but was '%s'", v.name, (int)i, expectedBuf, actualBuf);
			return false;
		}
	}

	return true;
}

static const IRVerification tests[] = {
	{
		"SimplePurgeTemps",
		{
			{ IROp::Add, { IRTEMP_0 }, MIPS_REG_A0, MIPS_REG_A1 },
			{ IROp::Mov, { MIPS_REG_V0 }, IRTEMP_0 },
			{ IROp::Add, { IRTEMP_0 }, MIPS_REG_A2, MIPS_REG_A3 },
			{ IROp::Mov, { MIPS_REG_V1 }, IRTEMP_0 },
		},
		{
			{ IROp::Add, { MIPS_REG_V0 }, MIPS_REG_A0, MIPS_REG_A1 },
			{ IROp::Add, { MIPS_REG_V1 }, MIPS_REG_A2, MIPS_REG_A3 },
		},
		{ &PurgeTemps },
	},
	{
		"Load32LeftPurgeTemps",
		{
			{ IROp::Mov, { IRTEMP_LR_ADDR }, MIPS_REG_A0 },
			{ IROp::AndConst, { IRTEMP_LR_ADDR }, IRTEMP_LR_ADDR, 0, 0xFFFFFFFC },
			{ IROp::Load32, { MIPS_REG_V0 }, IRTEMP_LR_ADDR, 0, 0 },
		},
		{
			{ IROp::AndConst, { IRTEMP_LR_ADDR }, MIPS_REG_A0, 0, 0xFFFFFFFC },
			{ IROp::Load32, { MIPS_REG_V0 }, IRTEMP_LR_ADDR, 0, 0 },
		},
		{ &PurgeTemps },
	},
	{
		"SwapClobberTemp",
		{
			{ IROp::Sub, { MIPS_REG_A0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Mov, { MIPS_REG_S0 }, MIPS_REG_A0 },
			{ IROp::Slt, { MIPS_REG_A0 }, MIPS_REG_V0, MIPS_REG_V1 },
		},
		{
			{ IROp::Sub, { MIPS_REG_S0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Slt, { MIPS_REG_A0 }, MIPS_REG_V0, MIPS_REG_V1 },
		},
		{ &PurgeTemps },
	},
	{
		"DoubleClobberTemp",
		{
			{ IROp::Add, { MIPS_REG_A0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Mov, { MIPS_REG_S0 }, MIPS_REG_A0 },
			{ IROp::Mov, { MIPS_REG_S0 }, MIPS_REG_A1 },
		},
		{
			{ IROp::Add, { MIPS_REG_S0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Mov, { MIPS_REG_A0 }, MIPS_REG_S0 },
			{ IROp::Mov, { MIPS_REG_S0 }, MIPS_REG_A1 },
		},
		{ &PurgeTemps },
	},
	{
		"SimplePropagateConstants",
		{
			{ IROp::SetConst, { MIPS_REG_A0 }, 0, 0, 0x12340000 },
			{ IROp::OrConst, { MIPS_REG_A0 }, MIPS_REG_A0, 0, 0x00005678 },
			{ IROp::AddConst, { MIPS_REG_A1 }, MIPS_REG_A0, 0, 0 },
			{ IROp::AddConst, { MIPS_REG_A2 }, MIPS_REG_A0, 0, 0x00001111 },
		},
		{
			{ IROp::SetConst, { MIPS_REG_A0 }, 0, 0, 0x12345678 },
			{ IROp::SetConst, { MIPS_REG_A1 }, 0, 0, 0x12345678 },
			{ IROp::SetConst, { MIPS_REG_A2 }, 0, 0, 0x12346789 },
		},
		{ &PropagateConstants },
	},
	{
		// Needed for PurgeTemps optimizations to work.
		"OrToMov",
		{
			{ IROp::Sub, { MIPS_REG_A0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Or, { MIPS_REG_S0 }, MIPS_REG_A0, MIPS_REG_ZERO },
			{ IROp::Add, { MIPS_REG_S1 }, MIPS_REG_A0, MIPS_REG_ZERO },
			{ IROp::OrConst, { MIPS_REG_S2 }, MIPS_REG_A0, 0, 0 },
			{ IROp::AddConst, { MIPS_REG_S3 }, MIPS_REG_A0, 0, 0 },
		},
		{
			{ IROp::Sub, { MIPS_REG_A0 }, MIPS_REG_A1, MIPS_REG_A2 },
			{ IROp::Mov, { MIPS_REG_S0 }, MIPS_REG_A0 },
			{ IROp::Mov, { MIPS_REG_S1 }, MIPS_REG_A0 },
			{ IROp::Mov, { MIPS_REG_S2 }, MIPS_REG_A0 },
			{ IROp::Mov, { MIPS_REG_S3 }, MIPS_REG_A0 },
		},
		{ &PropagateConstants },
	},
};

bool TestIRPassSimplify() {
	InitIR();

	for (const auto &test : tests) {
		if (!VerifyPass(test))
			return false;
	}

	return true;
}
