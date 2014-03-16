#include "Common/ArmEmitter.h"
#include "ext/disarm.h"

#include "UnitTest.h"

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
	emitter.VNEG(S1, S2);
	RET(CheckLast(emitter, "eef10a41 VNEG s1, s2"));
	emitter.LDR(R3, R7);
	RET(CheckLast(emitter, "e5973000 LDR r3, [r7, #0]"));
	emitter.VLDR(S3, R8, 48);
	RET(CheckLast(emitter, "edd81a0c VLDR s3, [r8, #48]"));
	emitter.VSTR(S5, R12, -36);
	RET(CheckLast(emitter, "ed4c2a09 VSTR s5, [r12, #-36]"));
	emitter.VADD(S1, S2, S3);
	RET(CheckLast(emitter, "ee710a21 VADD s1, s2, s3"));
	emitter.VADD(D1, D2, D3);
	RET(CheckLast(emitter, "ee321b03 VADD d1, d2, d3"));
	emitter.VSUB(S1, S2, S3);
	RET(CheckLast(emitter, "ee710a61 VSUB s1, s2, s3"));
	emitter.VMUL(S7, S8, S9);
	RET(CheckLast(emitter, "ee643a24 VMUL s7, s8, s9"));
	emitter.VMUL(S0, S5, S10);
	RET(CheckLast(emitter, "ee220a85 VMUL s0, s5, s10"));
	emitter.VNMUL(S7, S8, S9);
	RET(CheckLast(emitter, "ee643a64 VNMUL s7, s8, s9"));
	emitter.VMLA(S7, S8, S9);
	RET(CheckLast(emitter, "ee443a24 VMLA s7, s8, s9"));
	emitter.VNMLA(S7, S8, S9);
	RET(CheckLast(emitter, "ee543a64 VNMLA s7, s8, s9"));
	emitter.VNMLS(S7, S8, S9);
	RET(CheckLast(emitter, "ee543a24 VNMLS s7, s8, s9"));
	emitter.VABS(S1, S2);
	RET(CheckLast(emitter, "eef00ac1 VABS s1, s2"));
	emitter.VMOV(S1, S2);
	RET(CheckLast(emitter, "eef00a41 VMOV s1, s2"));
	emitter.VMOV(D1, D2);
	RET(CheckLast(emitter, "eeb01b42 VMOV d1, d2"));
	emitter.VCMP(S1, S2);
	RET(CheckLast(emitter, "eef40a41 VCMP s1, s2"));
	emitter.VCMPE(S1, S2);
	RET(CheckLast(emitter, "eef40ac1 VCMPE s1, s2"));
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
	emitter.VMOV_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VMOV_imm(I_8, R0, VIMMxxxxxxxx, 0xF3);
	emitter.VMOV_immf(Q0, 1.0f);
	emitter.VMOV_immf(Q0, -1.0f);
	emitter.VBIC_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VMVN_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VPADD(F_32, D0, D0, D0);
	emitter.VMOV(Q14, Q2);

	emitter.VMOV(S3, S6);
	RET(CheckLast(emitter, "eef01a43 VMOV s3, s6"));

	emitter.VMOV_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VMOV_imm(I_8, R0, VIMMxxxxxxxx, 0xF3);
	emitter.VMOV_immf(Q0, 1.0f);
	RET(CheckLast(emitter, "f2870f50 VMOV q0, 1.0"));
	emitter.VMOV_immf(Q0, -1.0f);
	emitter.VBIC_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VMVN_imm(I_32, R0, VIMM___x___x, 0xF3);
	emitter.VPADD(F_32, D0, D0, D0);
	emitter.VMOV(Q14, Q2);

	emitter.VMOV(S9, R3);
	RET(CheckLast(emitter, "ee043a90 VMOV s9, r3"));
	emitter.VMOV(R9, S3);
	RET(CheckLast(emitter, "ee119a90 VMOV r9, s3"));

	emitter.VMOV(S3, S6);
	RET(CheckLast(emitter, "eef01a43 VMOV s3, s6"));
	emitter.VMOV(S25, S21);
	RET(CheckLast(emitter, "eef0ca6a VMOV s25, s21"));
	emitter.VLD1(I_32, D19, R3, 2, ALIGN_NONE, R_PC);
	RET(CheckLast(emitter, "f4633a8f VLD1.32 {d19-d20}, [r3]"));
	emitter.VST1(I_32, D23, R9, 1, ALIGN_NONE, R_PC);
	RET(CheckLast(emitter, "f449778f VST1.32 {d23}, [r9]"));
	emitter.VLD1_lane(F_32, D8, R3, 0, ALIGN_NONE, R_PC);
	RET(CheckLast(emitter, "f4a3880f VLD1.32 {d8[0]}, [r3]"));
	emitter.VLD1_lane(I_8, D8, R3, 2, ALIGN_NONE, R_PC);
	RET(CheckLast(emitter, "f4a3804f VLD1.i8 {d8[2]}, [r3]"));

	emitter.VADD(I_8, D3, D4, D19);
	RET(CheckLast(emitter, "f2043823 VADD.i8 d3, d4, d19"));
	emitter.VADD(I_32, D3, D4, D19);
	RET(CheckLast(emitter, "f2243823 VADD.i32 d3, d4, d19"));
	emitter.VADD(F_32, D3, D4, D19);
	RET(CheckLast(emitter, "f2043d23 VADD.f32 d3, d4, d19"));
	emitter.VSUB(I_16, Q5, Q6, Q15);
	RET(CheckLast(emitter, "f31ca86e VSUB.i16 q5, q6, q15"));
	emitter.VMUL(F_32, Q1, Q2, Q3);
	RET(CheckLast(emitter, "f3042d56 VMUL.f32 q1, q2, q3"));
	emitter.VMUL(F_32, Q13, Q15, Q14);
	RET(CheckLast(emitter, "f34eadfc VMUL.f32 q13, q15, q14"));
	emitter.VADD(F_32, Q1, Q2, Q3);
	RET(CheckLast(emitter, "f2042d46 VADD.f32 q1, q2, q3"));
	emitter.VMLA(F_32, Q1, Q2, Q3);
	RET(CheckLast(emitter, "f2042d56 VMLA.f32 q1, q2, q3"));
	emitter.VMLS(F_32, Q1, Q2, Q3);
	RET(CheckLast(emitter, "f2242d56 VMLS.f32 q1, q2, q3"));
	emitter.VMLS(I_16, Q1, Q2, Q3);
	RET(CheckLast(emitter, "f3142946 VMLS.i16 q1, q2, q3"));

	emitter.VORR(Q1, Q2, Q3);
	RET(CheckLast(emitter, "f2242156 VORR q1, q2, q3"));
	emitter.VAND(Q1, Q2, Q3);
	RET(CheckLast(emitter, "f2042156 VAND q1, q2, q3"));
	emitter.VDUP(F_32, Q14, D30, 1);
	RET(CheckLast(emitter, "f3fccc6e VDUP.32 q14, d30[1]"));
	
	// TODO: This is broken.
	// emitter.VDUP(F_32, D14, D30, 1);
	// RET(CheckLast(emitter, "f3bcec2e VDUP.32 d14, d30[1]"));

	//emitter.VNEG(S1, S2);
	//RET(CheckLast(emitter, "eef10a60 VNEG.f32 s1, s1"));
	emitter.VNEG(F_32, Q1, Q2);
	RET(CheckLast(emitter, "f3b927c4 VNEG.f32 q1, q2"));
	emitter.VABS(F_32, Q1, Q2);
	RET(CheckLast(emitter, "f3b92744 VABS.f32 q1, q2"));
	emitter.VMOV(D26, D30);
	RET(CheckLast(emitter, "eef0ab6e VMOV d26, d30"));

	emitter.VMUL_scalar(F_32, Q1, Q2, DScalar(D7, 0));
	RET(CheckLast(emitter, "f3a42947 VMUL.f32 q1, q2, d7[0]"));

	emitter.VMUL_scalar(F_32, D1, D2, QScalar(Q7, 0));
	RET(CheckLast(emitter, "f2a2194e VMUL.f32 d1, d2, d14[0]"));

	emitter.VMLA_scalar(F_32, Q1, Q2, DScalar(D7, 0));
	RET(CheckLast(emitter, "f3a42147 VMLA.f32 q1, q2, d7[0]"));

	emitter.VMIN(F_32, D3, D4, D19);
	RET(CheckLast(emitter, "f2243f23 VMIN.f32 d3, d4, d19"));
	emitter.VMAX(F_32, Q3, Q4, Q9);
	RET(CheckLast(emitter, "f2086f62 VMAX.f32 q3, q4, q9"));
	return true;
}
