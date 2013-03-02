#include "base/logging.h"
#include "ArmEmitterTest.h"

#include "Common/ArmEmitter.h"

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

void TestCode::Generate()
{
	testCodePtr = this->GetCodePtr();
	// Sonic1 commented that R11 is the frame pointer in debug mode, whatever "debug mode" means.
	PUSH(2, R11, _LR);
	MOVI2R(R11, (u32)&abc[0]);
	MOVI2R(R1, 0x3f800000);
	STR(R11, R1, 4 * (32 + 31));
	VLDR(S0, R11, 0);
	VLDR(S1, R11, 4);
	VADD(S12, S0, S1);
	VSTR(S0, R11, 4 * (32 + 31));
	VSTR(S12, R11, 4 * (32 + 31));
	//VSTR(S2, R0, 8);
	POP(2, R11, _PC); // Yup, this is how you return.

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
	ILOG("Running ARM emitter test!");
	TestCode gen;
	gen.ReserveCodeSpace(0x1000);
	const u8 *codeStart = gen.GetCodePtr();
	gen.Generate();
	DisassembleArm(codeStart, gen.GetCodePtr()-codeStart);

	u32 retval = CallPtr(gen.testCodePtr);
	ILOG("ARM emitter test 1 passed if %f == 3.0! retval = %08x", abc[32 + 31], retval);
}
