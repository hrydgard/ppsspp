#include "Common/ARM64Emitter.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/Util/DisArm64.h"

#include "UnitTest.h"

static bool CheckLast(Arm64Gen::ARM64XEmitter &emit, const char *comp) {
	u32 instr;
	memcpy(&instr, emit.GetCodePtr() - 4, 4);
	char disasm[512];
	Arm64Dis(0, instr, disasm, sizeof(disasm), true);
	EXPECT_EQ_STR(std::string(disasm), std::string(comp));
	return true;
}

static void DisassembleARMBetween(const u8 *start, const u8 *end) {
	while (start < end) {
		char disasm[512];
		uint32_t instr;
		memcpy(&instr, start, 4);
		Arm64Dis(0, instr, disasm, sizeof(disasm), true);
		printf("%s\n", disasm);
		start += 4;
	}
}

bool TestArm64Emitter() {
	using namespace Arm64Gen;

	u32 code[512];
	ARM64XEmitter emitter((u8 *)code);
	emitter.ADD(X1, X2, X30);
	RET(CheckLast(emitter, "8b3e6041 add x1, x2, x30"));
	emitter.SUB(W1, W2, W30);
	RET(CheckLast(emitter, "4b3e4041 sub w1, w2, w30"));
	return true;
}