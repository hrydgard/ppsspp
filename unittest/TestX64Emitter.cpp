#include "ppsspp_config.h"

#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)

#include "Common/CPUDetect.h"
#include "Common/x64Emitter.h"
#include "Core/MIPS/x86/RegCacheFPU.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "ext/disarm.h"

#include "UnitTest.h"

static const u8 *prevStart = NULL;

static bool CheckLast(const Gen::XEmitter &emit, const char *comp) {
	auto vec = DisassembleX86(prevStart, (int)(emit.GetCodePointer() - prevStart));
	EXPECT_EQ_STR(vec[0], std::string(comp));
	return true;
}

static void PrintLast(const Gen::XEmitter &emit) {
	for (const u8 *p = prevStart; p < emit.GetCodePointer(); p++) {
		printf("%02x ", *p);
	}
	printf("\n");
}

bool TestX64Emitter() {
	using namespace Gen;

	u32 code[512];
	XEmitter emitter((u8 *)code);

	bool prevAVX = cpu_info.bAVX;
	cpu_info.bAVX = true;

	prevStart = emitter.GetCodePointer();
	emitter.VADDSD(XMM0, XMM1, R(XMM7));
	RET(CheckLast(emitter, "vaddsd xmm0, xmm1, xmm7"));

	prevStart = emitter.GetCodePointer();
	emitter.VMULSD(XMM0, XMM1, R(XMM7));
	RET(CheckLast(emitter, "vmulsd xmm0, xmm1, xmm7"));

	cpu_info.bAVX = prevAVX;

	// Just for checking.
	PrintLast(emitter);
	return true;
}

#else

bool TestX64Emitter() {
	return true;
}

#endif
