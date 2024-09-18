// Copyright (c) 2023- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "ppsspp_config.h"
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <string>
#include <vector>
#include "Common/x64Emitter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

#if PPSSPP_PLATFORM(WINDOWS) && (defined(_MSC_VER) || defined(__clang__) || defined(__INTEL_COMPILER))
#define X64JIT_XMM_CALL __vectorcall
#define X64JIT_USE_XMM_CALL 1
#elif PPSSPP_ARCH(AMD64)
// SystemV ABI supports XMM registers.
#define X64JIT_XMM_CALL
#define X64JIT_USE_XMM_CALL 1
#else
// GCC on x86 doesn't support vectorcall.
#define X64JIT_XMM_CALL
#define X64JIT_USE_XMM_CALL 0
#endif

namespace MIPSComp {

class X64JitBackend : public Gen::XCodeBlock, public IRNativeBackend {
public:
	X64JitBackend(JitOptions &jo, IRBlockCache &blocks);
	~X64JitBackend();

	bool DescribeCodePtr(const u8 *ptr, std::string &name) const override;

	void GenerateFixedCode(MIPSState *mipsState) override;
	bool CompileBlock(IRBlockCache *irBlockCache, int block_num, bool preload) override;
	void ClearAllBlocks() override;
	void InvalidateBlock(IRBlockCache *irBlockCache, int block_num) override;

protected:
	const CodeBlockCommon &CodeBlock() const override {
		return *this;
	}

private:
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void MovFromPC(Gen::X64Reg r);
	void MovToPC(Gen::X64Reg r);
	void WriteDebugPC(uint32_t pc);
	void WriteDebugPC(Gen::X64Reg r);
	void WriteDebugProfilerStatus(IRProfilerStatus status);

	void SaveStaticRegisters();
	void LoadStaticRegisters();

	// Note: destroys SCRATCH1.
	void FlushAll();

	void WriteConstExit(uint32_t pc);
	void OverwriteExit(int srcOffset, int len, int block_num) override;

	void CompIR_Arith(IRInst inst) override;
	void CompIR_Assign(IRInst inst) override;
	void CompIR_Basic(IRInst inst) override;
	void CompIR_Bits(IRInst inst) override;
	void CompIR_Breakpoint(IRInst inst) override;
	void CompIR_Compare(IRInst inst) override;
	void CompIR_CondAssign(IRInst inst) override;
	void CompIR_CondStore(IRInst inst) override;
	void CompIR_Div(IRInst inst) override;
	void CompIR_Exit(IRInst inst) override;
	void CompIR_ExitIf(IRInst inst) override;
	void CompIR_FArith(IRInst inst) override;
	void CompIR_FAssign(IRInst inst) override;
	void CompIR_FCompare(IRInst inst) override;
	void CompIR_FCondAssign(IRInst inst) override;
	void CompIR_FCvt(IRInst inst) override;
	void CompIR_FLoad(IRInst inst) override;
	void CompIR_FRound(IRInst inst) override;
	void CompIR_FSat(IRInst inst) override;
	void CompIR_FSpecial(IRInst inst) override;
	void CompIR_FStore(IRInst inst) override;
	void CompIR_Generic(IRInst inst) override;
	void CompIR_HiLo(IRInst inst) override;
	void CompIR_Interpret(IRInst inst) override;
	void CompIR_Load(IRInst inst) override;
	void CompIR_LoadShift(IRInst inst) override;
	void CompIR_Logic(IRInst inst) override;
	void CompIR_Mult(IRInst inst) override;
	void CompIR_RoundingMode(IRInst inst) override;
	void CompIR_Shift(IRInst inst) override;
	void CompIR_Store(IRInst inst) override;
	void CompIR_StoreShift(IRInst inst) override;
	void CompIR_System(IRInst inst) override;
	void CompIR_Transfer(IRInst inst) override;
	void CompIR_VecArith(IRInst inst) override;
	void CompIR_VecAssign(IRInst inst) override;
	void CompIR_VecClamp(IRInst inst) override;
	void CompIR_VecHoriz(IRInst inst) override;
	void CompIR_VecLoad(IRInst inst) override;
	void CompIR_VecPack(IRInst inst) override;
	void CompIR_VecStore(IRInst inst) override;
	void CompIR_ValidateAddress(IRInst inst) override;

	void EmitConst4x32(const void **c, uint32_t v);
	void EmitFPUConstants();
	void EmitVecConstants();

	Gen::OpArg PrepareSrc1Address(IRInst inst);
	void CopyVec4ToFPRLane0(Gen::X64Reg dest, Gen::X64Reg src, int lane);

	JitOptions &jo;
	X64IRRegCache regs_;

	const u8 *outerLoop_ = nullptr;
	const u8 *outerLoopPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherCheckCoreState_ = nullptr;
	const u8 *dispatcherPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherNoCheck_ = nullptr;
	const u8 *restoreRoundingMode_ = nullptr;
	const u8 *applyRoundingMode_ = nullptr;

	const u8 *saveStaticRegisters_ = nullptr;
	const u8 *loadStaticRegisters_ = nullptr;

	typedef struct { float f[4]; } Float4Constant;
	struct Constants {
		const void *noSignMask;
		const void *signBitAll;
		const void *positiveZeroes;
		const void *positiveInfinity;
		const void *positiveOnes;
		const void *negativeOnes;
		const void *qNAN;
		const void *maxIntBelowAsFloat;
		const float *mulTableVi2f;
		const float *mulTableVf2i;
		const Float4Constant *vec4InitValues;
	};
	Constants constants;

	int jitStartOffset_ = 0;
	int compilingBlockNum_ = -1;
	int logBlocks_ = 0;
	// Only useful in breakpoints, where it's set immediately prior.
	uint32_t lastConstPC_ = 0;
};

class X64IRJit : public IRNativeJit {
public:
	X64IRJit(MIPSState *mipsState)
		: IRNativeJit(mipsState), x64Backend_(jo, blocks_) {
		Init(x64Backend_);
	}

private:
	X64JitBackend x64Backend_;
};

} // namespace MIPSComp

#endif
