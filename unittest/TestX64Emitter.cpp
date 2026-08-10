#include "ppsspp_config.h"

#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)

#include "Common/CPUDetect.h"
#include "Common/x64Analyzer.h"
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

// Emits nothing itself - runs the x64 crash-handler instruction analyzer (Common/x64Analyzer.cpp)
// on the instruction most recently emitted (from prevStart to the emitter's current position),
// and checks that it decoded it the way we expect.
static bool CheckAnalyze(const Gen::XEmitter &emit, bool expectWrite, InstructionClass expectClass, int expectOperandSize, bool expectZeroExtend = false, bool expectSignExtend = false) {
	LSInstructionInfo info{};
	bool success = X86AnalyzeMOV(prevStart, info);
	EXPECT_TRUE(success);
	EXPECT_EQ_INT(info.instructionSize, (int)(emit.GetCodePointer() - prevStart));
	EXPECT_EQ_INT(info.isMemoryWrite, expectWrite);
	EXPECT_EQ_INT((int)info.instructionClass, (int)expectClass);
	EXPECT_EQ_INT(info.operandSizeInBytes, expectOperandSize);
	EXPECT_EQ_INT(info.zeroExtend, expectZeroExtend);
	EXPECT_EQ_INT(info.signExtend, expectSignExtend);
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

	// Exercise Common/x64Analyzer.cpp (used by the JIT crash handler to figure out what a faulting
	// load/store instruction was doing) against instructions written by the emitter, for the most
	// common memory access instructions.
	prevStart = emitter.GetCodePointer();
	emitter.MOV(32, R(EAX), MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::GPR, 4));

	prevStart = emitter.GetCodePointer();
	emitter.MOV(32, MDisp(RCX, 4), R(EAX));
	RET(CheckAnalyze(emitter, true, InstructionClass::GPR, 4));

	prevStart = emitter.GetCodePointer();
	emitter.MOVZX(32, 8, EAX, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::GPR, 1, true, false));

	prevStart = emitter.GetCodePointer();
	emitter.MOVZX(32, 16, EAX, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::GPR, 2, true, false));

	prevStart = emitter.GetCodePointer();
	emitter.MOVSX(32, 8, EAX, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::GPR, 1, false, true));

	prevStart = emitter.GetCodePointer();
	emitter.MOVSX(32, 16, EAX, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::GPR, 2, false, true));

	prevStart = emitter.GetCodePointer();
	emitter.MOVSS(XMM0, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::FP, 4));

	prevStart = emitter.GetCodePointer();
	emitter.MOVSS(MDisp(RCX, 4), XMM0);
	RET(CheckAnalyze(emitter, true, InstructionClass::FP, 4));

	prevStart = emitter.GetCodePointer();
	emitter.MOVUPS(XMM0, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::FP_SIMD, 16));

	prevStart = emitter.GetCodePointer();
	emitter.MOVUPS(MDisp(RCX, 4), XMM0);
	RET(CheckAnalyze(emitter, true, InstructionClass::FP_SIMD, 16));

	prevStart = emitter.GetCodePointer();
	emitter.MOVAPS(XMM0, MDisp(RCX, 4));
	RET(CheckAnalyze(emitter, false, InstructionClass::FP_SIMD, 16));

	prevStart = emitter.GetCodePointer();
	emitter.MOVAPS(MDisp(RCX, 4), XMM0);
	RET(CheckAnalyze(emitter, true, InstructionClass::FP_SIMD, 16));

	// Just for checking.
	PrintLast(emitter);
	return true;
}

#else

bool TestX64Emitter() {
	return true;
}

#endif
