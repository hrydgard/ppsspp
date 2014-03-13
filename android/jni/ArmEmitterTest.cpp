#include "base/logging.h"
#include "ArmEmitterTest.h"

#include "Common/ArmEmitter.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/ARM/ArmJit.h"

static bool functionWasCalled;

using namespace ArmGen;

class TestCode : public ArmGen::ARMXCodeBlock {
public:
	TestCode();
	void Generate();
	const u8 *testCodePtr;
	const u8 *testCodePtr2;
};

TestCode::TestCode()
{
	AllocCodeSpace(0x10000);
}

static float abc[256] = {1.0f, 2.0f, 0.0f};

static float a[4] = {1.0f, 2.0f, 3.0f, 4.5f};
static float b[4] = {1.0f, 1.0f, 1.0f, 0.5f};
static float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};

static u32 x[4] = {0x04030201, 0x08070605, 0x0, 0x0};
static u32 y[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
static u32 z[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

void TestCode::Generate()
{
	testCodePtr = this->GetCodePtr();
	// Sonic1 commented that R11 is the frame pointer in debug mode, whatever "debug mode" means.
	PUSH(2, R11, R_LR);

	// Load the three pointers
	/*
	MOVP2R(R0, a);
	MOVP2R(R1, b);
	MOVP2R(R2, c);

	// Load from two, do the operation, write to the third.
	VLD1(F_32, D0, R0, 2);  // Load 2 doubles
	VLD1(F_32, D2, R1, 2);  // Load another 2 doubles
	// VADD(F_32, Q2, Q0, Q1);  // Add them, seeing them as floating point quads
	VMUL_scalar(F_32, Q2, Q0, DScalar(D3, 1));   // Multiply a quad by a scalar (ultra efficient for matrix mul! limitation: Scalar has to come out of D0-D15)
	ADD(R1, R1, 12);
	VLD1_all_lanes(F_32, Q2, R1, true);
	ADD(R0, R0, 12);
	VLD1_lane(F_32, D4, R0, 1, true);
	// VMUL(F_32, Q2, Q0, Q1);
	VST1(F_32, D4, R2, 2);
	*/

	// Let's try some integer stuff
	MOVP2R(R0, x);
	MOVP2R(R1, y);
	MOVP2R(R2, z);
	MOVP2R(R3, c);
	VLD1(I_32, D0, R0, 1);  // Load 1 double
	VMOVL(I_8 | I_UNSIGNED, Q1, D0);
	VMOVL(I_16 | I_UNSIGNED, Q2, D2);
	VCVT(F_32 | I_SIGNED, Q3, Q2);
	VST1(I_32, D2, R1, 2);
	VST1(I_32, D4, R2, 2);
	VST1(I_32, D6, R3, 2);
	PLD(R1, 32);
	VDUP(F_32, Q3, Q8, 1);
	u32 word = *(u32 *)(GetCodePtr() - 4);
	ILOG("Instruction Word: %08x", word);


	// This works!

	// c will later be logged.

	/*
	MOVI2R(R11, (u32)&abc[0]);
	MOVI2R(R1, 0x3f800000);
	STR(R11, R1, 4 * (32 + 31));
	VLDR(S0, R11, 0);
	VLDR(S1, R11, 4);
	VADD(S12, S0, S1);
	VSTR(S0, R11, 4 * (32 + 31));
	VSTR(S12, R11, 4 * (32 + 31));
	*/
	//VSTR(S2, R0, 8);
	POP(2, R11, R_PC); // Yup, this is how you return.

	FlushLitPool();
	FlushIcache();

	//VLDR(S1, R0, 4);
	//VADD(S2, S0, S1);
	//VSTR(S2, R0, 8);
	//QuickCallFunction(R3, (void*)&TestLeaf);

	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x1, 0x100, 0x1337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x2, 0x100, 0x31337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x3, 0x100, 0x1337);
}


u32 CallPtr(const void *ptr)
{
	return ((u32(*)())ptr)();
}

extern void DisassembleArm(const u8 *data, int size);

#ifndef _WIN32
#define OutputDebugStringA puts
#endif

bool TestRegCache() {
	using namespace ArmGen;
	u32 code[512];
	ARMXEmitter emitter((u8 *)code);
	MIPSState mips;
	MIPSComp::ArmJitOptions jo;
	MIPSComp::JitState js;

	OutputDebugStringA("======START======\n");

	char temp[256];
	u8 regs[4];
	GetVectorRegs(regs, V_Quad, 0x20);
	sprintf(temp, "Vector notation experiment: %i %i %i %i %s", regs[0], regs[1], regs[2], regs[3], GetVectorNotation(0x20, V_Quad));
	OutputDebugStringA(temp);

	ArmRegCacheFPU fpr(&mips, &js, &jo);
	MIPSAnalyst::AnalysisResults stats;
	memset(&stats, 0, sizeof(stats));
	fpr.SetEmitter(&emitter);
	fpr.Start(stats);
	ARMReg rtriple = fpr.QMapReg(0x20, V_Triple, 0);
	ARMReg rquad = fpr.QMapReg(0x20, V_Quad, MAP_DIRTY);
	ARMReg rquad2 = fpr.QMapReg(0x21, V_Quad, MAP_DIRTY);
	emitter.VADD(F_32, Q0, rquad, rquad2);
	ARMReg rpair3 = fpr.QMapReg(0x26, V_Single, MAP_DIRTY);
	ARMReg rpair4 = fpr.QMapReg(0x24, V_Single, MAP_DIRTY);
	emitter.VMUL(F_32, rpair3, rpair3, rpair4);
	fpr.FlushAll();

	// OutputDebugString the whole thing.
	const u8 *ptr = (const u8 *)code;
	for (; ptr < emitter.GetCodePtr(); ptr += 4) {
		char temp[128];
		sprintf(temp, "%08x\n", *(const u32 *)ptr);
		OutputDebugStringA(temp);
	}
	OutputDebugStringA("======END======\n");

	return true;
}


void ArmEmitterTest()
{
	// Disabled for now.
	//return;

	// If I commit with it enabled by accident, let's not blow up.

#ifdef ARM
	if (!cpu_info.bNEON)
		return;

	for (int i = 0; i < 6; i++) {
		ILOG("--------------------------");
	}
	ILOG("--------------------------");
	ILOG("Running ARM emitter test!");
	ILOG("--------------------------");

	TestCode gen;
	gen.ReserveCodeSpace(0x1000);
	const u8 *codeStart = gen.GetCodePtr();
	gen.Generate();

	u32 retval = CallPtr(gen.testCodePtr);
	// ILOG("ARM emitter test 1 passed if %f == 3.0! retval = %08x", abc[32 + 31], retval);
	ILOG("x: %08x %08x %08x %08x", x[0], x[1], x[2], x[3]);
	ILOG("y: %08x %08x %08x %08x", y[0], y[1], y[2], y[3]);
	ILOG("z: %08x %08x %08x %08x", z[0], z[1], z[2], z[3]);
	ILOG("c: %f %f %f %f", c[0], c[1], c[2], c[3]);
	for (int i = 0; i < 6; i++) {
		ILOG("--------------------------");
	}
#else
	// Set ARM features that the ARM emitter might check. We test the ARM emitter in x86 sometimes.
	cpu_info.bNEON = true;
	cpu_info.bVFPv3 = true;
	cpu_info.bVFPv4 = true;
#endif

	TestRegCache();

	// DisassembleArm(codeStart, gen.GetCodePtr()-codeStart);
}
