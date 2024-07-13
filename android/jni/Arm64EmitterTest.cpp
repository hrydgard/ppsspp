#include "Common/Arm64Emitter.h"
#include "Common/BitSet.h"
#include "Common/CPUDetect.h"
#include "Common/Log.h"

static bool functionWasCalled;

using namespace Arm64Gen;

class TestCode : public Arm64Gen::ARM64CodeBlock {
public:
	TestCode();
	void Generate();
	const u8 *testCodePtr;
	const u8 *testCodePtr2;
	ARM64FloatEmitter fp;
};

TestCode::TestCode() : fp(this)
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

	const u8 *start = AlignCode16();

	uint32_t regs_to_save = Arm64Gen::ALL_CALLEE_SAVED;
	uint32_t regs_to_save_fp = Arm64Gen::ALL_CALLEE_SAVED_FP;
	fp.ABI_PushRegisters(regs_to_save, regs_to_save_fp);

	PUSH(X3);
	POP(X3);

	PUSH2(X3, X4);
	POP2(X3, X4);

	fp.SCVTF(S0, W3, 12);
	fp.SCVTF(S3, W12);
	MOVI2R(X0, 1337);

	fp.ABI_PopRegisters(regs_to_save, regs_to_save_fp);

	RET();

	FlushIcache();
}

static u32 CallPtr(const void *ptr) {
	return ((u32(*)())ptr)();
}

void Arm64EmitterTest() {
	return;

	for (int i = 0; i < 6; i++) {
		INFO_LOG(Log::System, "---------------------------");
	}
	INFO_LOG(Log::System, "---------------------------");
	INFO_LOG(Log::System, "Running ARM64 emitter test!");
	INFO_LOG(Log::System, "---------------------------");

	TestCode gen;
	gen.ReserveCodeSpace(0x1000);
	const u8 *codeStart = gen.GetCodePtr();
	gen.Generate();

	u32 retval = CallPtr(gen.testCodePtr);
	INFO_LOG(Log::System, "Returned %d", retval);
	// INFO_LOG(Log::System, "ARM emitter test 1 passed if %f == 3.0! retval = %08x", abc[32 + 31], retval);
	/*
	INFO_LOG(Log::System, "x: %08x %08x %08x %08x", x[0], x[1], x[2], x[3]);
	INFO_LOG(Log::System, "y: %08x %08x %08x %08x", y[0], y[1], y[2], y[3]);
	INFO_LOG(Log::System, "z: %08x %08x %08x %08x", z[0], z[1], z[2], z[3]);
	INFO_LOG(Log::System, "c: %f %f %f %f", c[0], c[1], c[2], c[3]);*/
	for (int i = 0; i < 6; i++) {
		INFO_LOG(Log::System, "--------------------------");
	}
	// DisassembleArm(codeStart, gen.GetCodePtr()-codeStart);
}
