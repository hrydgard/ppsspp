#include "Common/x64Emitter.h"
#include "Core/MIPS/x86/RegCacheFPU.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "ext/disarm.h"

#include "UnitTest.h"

static const u8 *prevStart = NULL;

bool CheckLast(Gen::XEmitter &emit, const char *comp) {
	auto vec = DisassembleX86(prevStart, emit.GetCodePtr() - prevStart);
	EXPECT_EQ_STR(vec[0], std::string(comp));
	return true;
}

void PrintLast(Gen::XEmitter &emit) {
	for (const u8 *p = prevStart; p < emit.GetCodePtr(); p++) {
		printf("%02x ", *p);
	}
	printf("\n");
}

bool TestX64Emitter() {
	using namespace Gen;

	u32 code[512];
	XEmitter emitter((u8 *)code);

	prevStart = emitter.GetCodePtr();
	emitter.VADDSD(XMM0, XMM1, R(XMM7));
	RET(CheckLast(emitter, "vaddsd xmm0, xmm1, xmm7"));

	prevStart = emitter.GetCodePtr();
	emitter.VMULSD(XMM0, XMM1, R(XMM7));
	RET(CheckLast(emitter, "vmulsd xmm0, xmm1, xmm7"));

	// Just for checking.
	PrintLast(emitter);
	return true;
}
