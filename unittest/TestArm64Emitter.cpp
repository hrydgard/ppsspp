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

	int lz = CountLeadingZeros(0x0000400000000000, 64);
	EXPECT_EQ_INT(lz, 17);

	u32 code[512];
	ARM64XEmitter emitter((u8 *)code);
	ARM64FloatEmitter fp(&emitter);
	//fp.FMOV(32, false, S1, X3);
	//RET(CheckLast(emitter, "aa023be1 fmov s1, w3"));
	//fp.FMOV(32, false, X1, S3);
	//RET(CheckLast(emitter, "aa023be1 fmov x1, s3"));
	emitter.MOV(X1, X2, ArithOption(X1, ST_LSL, 14));
	RET(CheckLast(emitter, "aa023be1 mov x1, x2, lsl #14"));
	emitter.LSLV(X1, X2, X30);
	RET(CheckLast(emitter, "9ade2041 lslv x1, x2, x30"));
	emitter.CMP(W2, W30);
	RET(CheckLast(emitter, "6b1e005f cmp w2, w30"));
	emitter.ADD(X1, X2, X30);
	RET(CheckLast(emitter, "8b1e0041 add x1, x2, x30"));
	emitter.SUB(W1, W2, W30);
	RET(CheckLast(emitter, "4b1e0041 sub w1, w2, w30"));
	emitter.SUBS(W1, W2, W30);
	RET(CheckLast(emitter, "6b1e0041 subs w1, w2, w30"));
	emitter.EOR(X1, X2, X30);
	RET(CheckLast(emitter, "ca1e0041 eor x1, x2, x30"));
	emitter.AND(W1, W2, W30);
	RET(CheckLast(emitter, "0a1e0041 and w1, w2, w30"));
	emitter.ANDS(W1, W2, W30);
	RET(CheckLast(emitter, "6a1e0041 ands w1, w2, w30"));
	emitter.ORR(X1, X2, X30);
	RET(CheckLast(emitter, "aa1e0041 orr x1, x2, x30"));

	fp.FMUL(S0, S12, S22);
	RET(CheckLast(emitter, "1e360980 fmul s0, s12, s22"));
	emitter.ADD(X23, X30, 123, false);
	RET(CheckLast(emitter, "9101efd7 add x23, x30, #123"));
	emitter.MOV(X23, X30);
	RET(CheckLast(emitter, "aa1e03f7 mov x23, x30"));
	fp.FSQRT(S20, S25);
	RET(CheckLast(emitter, "1e21c334 fsqrt s20, s25"));
	emitter.ORI2R(X1, X3, 0x3F, INVALID_REG);
	RET(CheckLast(emitter, "b2401461 orr x1, x3, #0x3f"));
	emitter.EORI2R(X1, X3, 0x3F0000003F0, INVALID_REG);
	RET(CheckLast(emitter, "d21c1461 eor x1, x3, #0x3f0000003f0"));

	printf("yay!\n");
	//emitter.ANDI2R(W1, W3, 0xFF00FF00FF00FF00ULL, INVALID_REG);
	//RET(CheckLast(emitter, "00000000 and x1, x3, 0xFF00FF00FF00FF00"));

	// fp.FMUL(Q0, Q1, Q2);
	// RET(CheckLast(emitter, "4b3e4041 sub w1, w2, w30"));
	return true;
}