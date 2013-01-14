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
	AND(R1, R0, Operand2(0xFC, 4));
	BIC(R0, R0, Operand2(0xFC, 4));
	CMP(R1, Operand2(0x10, 4));
	SetCC(CC_EQ);
	MOV(R2, Operand2(0x99, 0));
	SetCC(CC_NEQ);
	MOV(R2, Operand2(0xFF, 0));
	SetCC();
	QuickCallFunction(R3, (void*)&TestLeaf);

	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x1, 0x100, 0x1337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x2, 0x100, 0x31337);
	//ARMABI_CallFunctionCCC((void*)&TestLeaf, 0x3, 0x100, 0x1337);
	POP(2, R11, _PC); // Yup, this is how you return.

	testCodePtr2 = this->GetCodePtr();
	PUSH(2, R11, _LR);
	QuickCallFunction(R3, (void*)&TestLeaf2);
	POP(2, R11, _PC);
}


void CallPtr(const void *ptr)
{
	((void(*)())ptr)();
}



void ArmEmitterTest()
{
	ILOG("Running ARM emitter test!");
	TestCode gen;
	gen.ReserveCodeSpace(0x4000);
	gen.Generate();

	CallPtr(gen.testCodePtr);
	ILOG("ARM emitter test 1 passed!");
}
