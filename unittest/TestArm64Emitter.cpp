#include "Common/Arm64Emitter.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/Util/DisArm64.h"

#include <string.h>

#include "UnitTest.h"

static bool CheckLast(const Arm64Gen::ARM64XEmitter &emit, const char *comp) {
	u32 instr;
	memcpy(&instr, emit.GetCodePointer() - 4, 4);
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

#undef ADD

bool TestArm64Emitter() {
	using namespace Arm64Gen;

	int lz = CountLeadingZeros(0x0000400000000000, 64);
	EXPECT_EQ_INT(lz, 17);


	u32 code[512];
	ARM64XEmitter emitter((u8 *)code, (u8 *)code);
	ARM64FloatEmitter fp(&emitter);

	emitter.MOVfromSP(X3);
	RET(CheckLast(emitter, "910003e3 mov x3, sp"));
	emitter.MOVtoSP(X11);
	RET(CheckLast(emitter, "9100017f mov sp, x11"));

	fp.EOR(Q0, Q1, Q2);
	RET(CheckLast(emitter, "6e221c20 eor q0, q1, q2"));
	fp.EOR(D0, D1, D2);
	RET(CheckLast(emitter, "2e221c20 eor d0, d1, d2"));
	fp.SHRN(32, D0, Q3, 8);
	RET(CheckLast(emitter, "0f388460 shrn.32.64 d0, q3, #8"));
	fp.SHRN(8, D0, Q3, 4);
	RET(CheckLast(emitter, "0f0c8460 shrn.8.16 d0, q3, #4"));

	fp.XTN(32, D0, Q3);
	RET(CheckLast(emitter, "0ea12860 xtn.32.64 d0, q3"));
	fp.XTN(8, D4, Q1);
	RET(CheckLast(emitter, "0e212824 xtn.8.16 d4, q1"));

	fp.UMIN(32, D0, D3, D4);
	RET(CheckLast(emitter, "2ea46c60 umin.32 d0, d3, d4"));
	fp.UMAX(16, Q0, Q3, Q4);
	RET(CheckLast(emitter, "6e646460 umax.16 q0, q3, q4"));
	fp.SMIN(8, D0, D3, D4);
	RET(CheckLast(emitter, "0e246c60 smin.8 d0, d3, d4"));
	fp.SMAX(16, D0, D3, D4);
	RET(CheckLast(emitter, "0e646460 smax.16 d0, d3, d4"));

	fp.SHL(32, D0, D3, 18);
	RET(CheckLast(emitter, "0f325460 shl.32 d0, d3, #18"));
	fp.USHR(16, Q0, Q3, 7);
	RET(CheckLast(emitter, "6f190460 ushr.16 q0, q3, #7"));
	fp.SSHR(64, Q0, Q3, 38);
	RET(CheckLast(emitter, "4f5a0460 sshr.64 q0, q3, #38"));

	emitter.LDRH(INDEX_UNSIGNED, W3, X7, 18);
	RET(CheckLast(emitter, "794024e3 ldrh w3, [x7, #18]"));
	emitter.LDRSH(INDEX_UNSIGNED, W3, X7, 18);
	RET(CheckLast(emitter, "79c024e3 ldrsh w3, [x7, #18]"));
	fp.SCVTF(32, Q3, Q7, 7);
	RET(CheckLast(emitter, "4f39e4e3 scvtf q3.s, q7.s, #7"));
	fp.UCVTF(32, D3, D7, 15);
	RET(CheckLast(emitter, "2f31e4e3 ucvtf d3.s, d7.s, #15"));

	fp.LDP(128, INDEX_SIGNED, Q3, Q7, X3, 32);
	RET(CheckLast(emitter, "ad411c63 ldp q3, q7, [x3, #32]"));
	fp.STP(128, INDEX_SIGNED, Q3, Q7, X3, 32);
	RET(CheckLast(emitter, "ad011c63 stp q3, q7, [x3, #32]"));

	emitter.STP(INDEX_SIGNED, W21, W1, X27, 48);
	RET(CheckLast(emitter, "29060775 stp w21, w1, [x27, #48]"));
	emitter.STNP(W21, W1, X27, 48);
	RET(CheckLast(emitter, "28060775 stnp w21, w1, [x27, #48]"));

	emitter.MRS(X25, FIELD_NZCV);
	RET(CheckLast(emitter, "d53b4219 mrs x25, nzcv"));
	emitter._MSR(FIELD_NZCV, X25);
	RET(CheckLast(emitter, "d51b4219 msr nzcv, x25"));

	fp.DUP(32, Q1, Q30, 3);
	RET(CheckLast(emitter, "4e1c07c1 dup q1, q30.s[3]"));
	fp.UXTL(8, Q1, D8);
	RET(CheckLast(emitter, "2f08a501 uxtl.16.8 q1, d8"));
	fp.LDR(8, INDEX_PRE, Q1, X1, 96);
	RET(CheckLast(emitter, "3c460c21 ldr b1, [x1, #96]!"));
	fp.LDR(8, INDEX_POST, Q1, X1, 96);
	RET(CheckLast(emitter, "3c460421 ldr b1, [x1], #96"));
	fp.LDR(8, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "3d418021 ldr b1, [x1, #96]"));
	fp.LDR(16, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "7d40c021 ldr h1, [x1, #96]"));
	fp.LDR(32, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "bd406021 ldr s1, [x1, #96]"));
	fp.LDR(64, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "fd403021 ldr d1, [x1, #96]"));
	fp.LDR(128, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "3dc01821 ldr q1, [x1, #96]"));
	fp.STR(8, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "3d018021 str b1, [x1, #96]"));
	fp.STR(16, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "7d00c021 str h1, [x1, #96]"));
	fp.STR(32, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "bd006021 str s1, [x1, #96]"));
	fp.STR(64, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "fd003021 str d1, [x1, #96]"));
	fp.STR(128, INDEX_UNSIGNED, Q1, X1, 96);
	RET(CheckLast(emitter, "3d801821 str q1, [x1, #96]"));
	fp.FMLA(32, D1, D2, D3);
	RET(CheckLast(emitter, "0e23cc41 fmla.32 d1, d2, d3"));
	fp.FMLS(64, Q1, Q2, Q3);
	RET(CheckLast(emitter, "4ee3cc41 fmls.64 q1, q2, q3"));
	fp.FADD(32, Q1, Q13, Q21);
	RET(CheckLast(emitter, "4e35d5a1 fadd.32 q1, q13, q21"));
	fp.FMUL(32, D1, D13, D21);
	RET(CheckLast(emitter, "2e35dda1 fmul.32 d1, d13, d21"));
	fp.INS(32, Q3, 1, Q12, 3);
	RET(CheckLast(emitter, "6e0c6583 ins q3.s[1], q12.s[3]"));
	fp.INS(8, D4, 5, D11, 2);
	RET(CheckLast(emitter, "6e0b1564 ins d4.b[5], d11.b[2]"));
	emitter.NEG(X1, X2);
	RET(CheckLast(emitter, "cb0203e1 neg x1, x2"));

	emitter.ADD(X1, X2, X3, ArithOption(X1, ST_LSL, 3));
	RET(CheckLast(emitter, "8b030c41 add x1, x2, x3, lsl #3"));
	//emitter.EXTR(W1, W3, 0, 7);
	//RET(CheckLast(emitter, "53033061 extr w1, w3, w7"));
	//fp.FCVTL(32, Q6, D25);
	//RET(CheckLast(emitter, "4fa29820 fcvtl q6, d25"));
	fp.FMUL(32, Q0, Q1, Q2, 3);
	RET(CheckLast(emitter, "4fa29820 fmul q0, q1, q2.4s[3]"));  // A real disasm says fmla	v0.2s, v1.2s, v2.s[1]  but I think our way is more readable
	fp.FMLA(32, D0, D1, D2, 1);
	RET(CheckLast(emitter, "0fa21020 fmla d0, d1, d2.2s[1]"));
	fp.FCSEL(S0, S1, S2, CC_CS);
	RET(CheckLast(emitter, "1e222c20 fcsel s0, s1, s2, cs"));
	float value = 1.0;
	uint8_t imm8;
	FPImm8FromFloat(value, &imm8);
	fp.FMOV(S7, imm8);
	RET(CheckLast(emitter, "1e2e1007 fmov s7, #1.000000"));
	FPImm8FromFloat(-value, &imm8);
	fp.FMOV(S7, imm8);
	RET(CheckLast(emitter, "1e3e1007 fmov s7, #-1.000000"));
	fp.FMADD(S1, S2, S3, S4);
	RET(CheckLast(emitter, "1f031041 fmadd s1, s2, s3, s4"));
	fp.FNMSUB(D1, D2, D3, D4);
	RET(CheckLast(emitter, "1f639041 fnmsub d1, d2, d3, d4"));
	fp.FMAX(S1, S2, S3);
	RET(CheckLast(emitter, "1e234841 fmax s1, s2, s3"));
	fp.FNMUL(D1, D2, D3);
	RET(CheckLast(emitter, "1e638841 fnmul d1, d2, d3"));
	fp.SCVTF(S13, W7);
	RET(CheckLast(emitter, "1e2200ed scvtf s13, w7"));
	emitter.UBFM(W1, W3, 0, 7);
	RET(CheckLast(emitter, "53001c61 ubfm w1, w3, #0, #7"));
	fp.FMOV(W1, S3);
	RET(CheckLast(emitter, "1e260061 fmov w1, s3"));
	fp.FMOV(S1, W3);
	RET(CheckLast(emitter, "1e270061 fmov s1, w3"));
	/*
	fp.FMOV(X1, D3);
	RET(CheckLast(emitter, "1e260061 fmov x1, d3"));
	fp.FMOV(D1, X3);
	RET(CheckLast(emitter, "1e270061 fmov d1, x3"));
	*/
	fp.SCVTF(S13, S12);
	RET(CheckLast(emitter, "5e21d98d scvtf s13, s12"));
	fp.FCVTS(S13, S12, ROUND_N);
	RET(CheckLast(emitter, "5e21a98d fcvtns s13, s12"));
	fp.FCVTS(D13, D12, ROUND_P);
	RET(CheckLast(emitter, "5ee1a98d fcvtps d13, d12"));
	fp.FCVTS(W13, S12, ROUND_N);
	RET(CheckLast(emitter, "1e20018d fcvtns w13, s12"));
	fp.FCVTS(S22, S12, ROUND_Z);
	RET(CheckLast(emitter, "5ea1b996 fcvtzs s22, s12"));
	fp.FCVTS(X3, D2, ROUND_M);
	RET(CheckLast(emitter, "9e700043 fcvtms x3, d2"));
	fp.UCVTF(S12, X3, 8);
	RET(CheckLast(emitter, "9e03e06c ucvtf s12, x3, #8"));
	fp.SCVTF(S12, W13, 12);
	RET(CheckLast(emitter, "1e02d1ac scvtf s12, w13, #12"));
	fp.FCVTS(W12, S3, ROUND_Z);
	RET(CheckLast(emitter, "1e38006c fcvtzs w12, s3"));
	emitter.LSLV(W1, W2, W3);
	RET(CheckLast(emitter, "1ac32041 lslv w1, w2, w3"));
	emitter.LSRV(W1, W2, W3);
	RET(CheckLast(emitter, "1ac32441 lsrv w1, w2, w3"));
	emitter.UDIV(W1, W2, W3);
	RET(CheckLast(emitter, "1ac30841 udiv w1, w2, w3"));
	emitter.RBIT(W3, W2);
	RET(CheckLast(emitter, "5ac00043 rbit w3, w2"));
	emitter.CMP(W3, 3, false);
	RET(CheckLast(emitter, "71000c7f cmp w3, #3"));
	emitter.MADD(W3, W7, W1, W8);
	RET(CheckLast(emitter, "1b0120e3 madd w3, w7, w1, w8"));
	emitter.SMADDL(X3, W6, W1, X8);
	RET(CheckLast(emitter, "9b2120c3 smaddl x3, w6, w1, x8"));
	emitter.BRK(3);
	RET(CheckLast(emitter, "d4200060 brk #3"));
	emitter.CSEL(X3, X5, X7, CC_GT);
	RET(CheckLast(emitter, "9a87c0a3 csel x3, x5, x7, gt"));
	emitter.LDR(W23, X3, X12);
	RET(CheckLast(emitter, "b86c6877 ldr w23, [x3 + x12]"));
	emitter.LDP(INDEX_SIGNED, X23, X5, X3, 32);
	RET(CheckLast(emitter, "a9421477 ldp x23, x5, [x3, #32]"));
	emitter.LDP(INDEX_SIGNED, W23, W5, X3, 36);
	RET(CheckLast(emitter, "29449477 ldp w23, w5, [x3, #36]"));
	emitter.STP(INDEX_PRE, W23, W5, SP, -16);
	RET(CheckLast(emitter, "29be17f7 stp w23, w5, [x31, #-16]!"));	
	emitter.STP(INDEX_POST, W23, W5, X3, 36);
	RET(CheckLast(emitter, "28849477 stp w23, w5, [x3], #36"));
	emitter.LDP(INDEX_POST, W23, W5, X3, 36);
	RET(CheckLast(emitter, "28c49477 ldp w23, w5, [x3], #36"));
	emitter.STR(INDEX_PRE, W23, SP, -16);
	RET(CheckLast(emitter, "b81f0ff7 str w23, [x31, #-16]!"));
	emitter.STR(INDEX_UNSIGNED, W23, X3, 36);
	RET(CheckLast(emitter, "b9002477 str w23, [x3, #36]"));
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
	fp.FNEG(S20, S25);
	RET(CheckLast(emitter, "1e214334 fneg s20, s25"));
	fp.FABS(S20, S25);
	RET(CheckLast(emitter, "1e20c334 fabs s20, s25"));
	fp.FMOV(S20, S25);
	RET(CheckLast(emitter, "1e204334 fmov s20, s25"));
	fp.MOV(Q20, Q25);
	RET(CheckLast(emitter, "4eb91f34 mov q20, q25"));
	fp.FCMP(S7);
	RET(CheckLast(emitter, "1e2020e8 fcmp s7, #0.0"));
	fp.FCMP(D7, D3);
	RET(CheckLast(emitter, "1e6320e0 fcmp d7, d3"));
	emitter.ORRI2R(X1, X3, 0x3F, INVALID_REG);
	RET(CheckLast(emitter, "b2401461 orr x1, x3, #0x3f"));
	emitter.EORI2R(X1, X3, 0x3F0000003F0, INVALID_REG);
	RET(CheckLast(emitter, "d21c1461 eor x1, x3, #0x3f0000003f0"));

	printf("ARM64 emitter test completed!\n");
	//emitter.ANDI2R(W1, W3, 0xFF00FF00FF00FF00ULL, INVALID_REG);
	//RET(CheckLast(emitter, "00000000 and x1, x3, 0xFF00FF00FF00FF00"));

	// fp.FMUL(Q0, Q1, Q2);
	// RET(CheckLast(emitter, "4b3e4041 sub w1, w2, w30"));
	return true;
}
