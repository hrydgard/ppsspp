#include "ArmEmitterTest.h"

#include "Common/ArmEmitter.h"
#include "Common/CPUDetect.h"

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
	u32 word = *(u32 *)(GetCodePtr() - 4);
	INFO_LOG(Log::System, "Instruction Word: %08x", word);


	// This works!

	// c will later be logged.

	/*
	MOVP2R(R11, &abc[0]);
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


void ArmEmitterTest()
{
	// Disabled for now.
	return;

	// If I commit with it enabled by accident, let's not blow up.
	if (!cpu_info.bNEON)
		return;

	for (int i = 0; i < 6; i++) {
		INFO_LOG(Log::System, "--------------------------");
	}
	INFO_LOG(Log::System, "--------------------------");
	INFO_LOG(Log::System, "Running ARM emitter test!");
	INFO_LOG(Log::System, "--------------------------");

	TestCode gen;
	gen.ReserveCodeSpace(0x1000);
	const u8 *codeStart = gen.GetCodePtr();
	gen.Generate();

	u32 retval = CallPtr(gen.testCodePtr);
	// INFO_LOG(Log::System, "ARM emitter test 1 passed if %f == 3.0! retval = %08x", abc[32 + 31], retval);
	INFO_LOG(Log::System, "x: %08x %08x %08x %08x", x[0], x[1], x[2], x[3]);
	INFO_LOG(Log::System, "y: %08x %08x %08x %08x", y[0], y[1], y[2], y[3]);
	INFO_LOG(Log::System, "z: %08x %08x %08x %08x", z[0], z[1], z[2], z[3]);
	INFO_LOG(Log::System, "c: %f %f %f %f", c[0], c[1], c[2], c[3]);
	for (int i = 0; i < 6; i++) {
		INFO_LOG(Log::System, "--------------------------");
	}
	// DisassembleArm(codeStart, gen.GetCodePtr()-codeStart);
}
