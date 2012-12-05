#include "base/logging.h"
#include "ArmEmitterTest.h"

#include "Common/ArmABI.h"
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


u32 TestLeaf(u32 a, u32 b, u32 c)
{
	ILOG("TestLeaf: %08x %08x %08x\n", a, b, c);
	return 0xFF;
}

void TestLeaf2(u32 a)
{
	ILOG("TestLeaf2 %08x\n");
}


void TestCode::Generate()
{
	testCodePtr = this->GetCodePtr();
	// Sonic1 commented that R11 is the frame pointer in debug mode, whatever "debug mode" means.
	PUSH(2, R11, _LR);
	ARMABI_MOVI2R(R0, 0x13371338);
	ARMABI_MOVI2R(R1, 0x1337);
	ARMABI_CallFunction((void*)&TestLeaf2);

	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x1, 0x100, 0x1337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x2, 0x100, 0x31337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x3, 0x100, 0x1337);
	POP(2, R11, _PC); // Yup, this is how you return.

	testCodePtr2 = this->GetCodePtr();
	PUSH(2, R11, _LR);
	ARMABI_PushAllCalleeSavedRegsAndAdjustStack();
	ARMABI_CallFunction((void*)&TestLeaf2);
	ARMABI_PopAllCalleeSavedRegsAndAdjustStack();
	POP(2, R11, _PC);
}


void CallPtr(const void *ptr)
{
	((void(*)())ptr)();
}



void ArmEmitterTest()
{
	TestCode gen;
	gen.ReserveCodeSpace(0x4000);
	gen.Generate();

	CallPtr(gen.testCodePtr);
	ILOG("ARM emitter test 1 passed!");
}
