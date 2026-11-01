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

#include "ppsspp_config.h"
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include "Common/Log.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/ARM64/Arm64VfpuDot.h"
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Core.h"

namespace MIPSComp {

using namespace Arm64Gen;
using namespace Arm64IRJitConstants;

static const bool enableDebug = false;
static const bool enableDisasm = false;

static void ShowPC(void *membase, void *jitbase) {
	static int count = 0;
	if (currentMIPS) {
		u32 downcount = currentMIPS->downcount;
		ERROR_LOG(Log::JIT, "[%08x] ShowPC  Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
	} else {
		ERROR_LOG(Log::JIT, "Universe corrupt?");
	}
	//if (count > 2000)
	//	exit(0);
	count++;
}

void Arm64JitBackend::GenerateFixedCode(MIPSState *mipsState) {
	regs_.SetMemoryBase(mipsState->space->base);
	if (((intptr_t)mipsState->space->base & 0x00000000FFFFFFFFUL) != 0) {
		jo.enablePointerify = false;
	}
	// The Media Engine runs for the slices MeCore::RunFor gives it, and leaves when one runs out.
	const bool mediaEngine = mipsState->IsMediaEngine();
	// This will be used as a writable scratch area, always 32-bit accessible.
	const u8 *start = AlignCodePage();
	if (DebugProfilerEnabled()) {
		ProtectMemoryPages(start, GetMemoryProtectPageSize(), MEM_PROT_READ | MEM_PROT_WRITE);
		hooks_.profilerPC = (uint32_t *)GetWritableCodePtr();
		Write32(0);
		hooks_.profilerStatus = (IRProfilerStatus *)GetWritableCodePtr();
		Write32(0);
	}

	const u8 *disasmStart = AlignCodePage();
	BeginWrite(GetMemoryProtectPageSize());

	if (jo.useStaticAlloc) {
		saveStaticRegisters_ = AlignCode16();
		STR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		regs_.EmitSaveStaticRegisters();
		RET();

		loadStaticRegisters_ = AlignCode16();
		regs_.EmitLoadStaticRegisters();
		LDR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		RET();
	} else {
		saveStaticRegisters_ = nullptr;
		loadStaticRegisters_ = nullptr;
	}

	restoreRoundingMode_ = AlignCode16();
	{
		MRS(SCRATCH2_64, FIELD_FPCR);
		// We are not in flush-to-zero mode outside the JIT, so let's turn it off.
		uint32_t mask = ~(4 << 22);
		// Assume we're always in round-to-nearest mode beforehand.
		mask &= ~(3 << 22);
		ANDI2R(SCRATCH2, SCRATCH2, mask);
		_MSR(FIELD_FPCR, SCRATCH2_64);
		RET();
	}

	applyRoundingMode_ = AlignCode16();
	{
		LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, fcr31));
		ANDI2R(SCRATCH2, SCRATCH1, 3);
		FixupBranch skip1 = TBZ(SCRATCH1, 24);
		ADDI2R(SCRATCH2, SCRATCH2, 4);
		SetJumpTarget(skip1);

		// We can skip if the rounding mode is nearest (0) and flush is not set.
		// (as restoreRoundingMode cleared it out anyway)
		FixupBranch skip = CBZ(SCRATCH2);

		// MIPS Rounding Mode:       ARM Rounding Mode
		//   0: Round nearest        0
		//   1: Round to zero        3
		//   2: Round up (ceil)      1
		//   3: Round down (floor)   2
		ANDI2R(SCRATCH1, SCRATCH2, 3);
		CMPI2R(SCRATCH1, 1);

		FixupBranch skipadd = B(CC_NEQ);
		ADDI2R(SCRATCH2, SCRATCH2, 2);
		SetJumpTarget(skipadd);
		FixupBranch skipsub = B(CC_LE);
		SUBI2R(SCRATCH2, SCRATCH2, 1);
		SetJumpTarget(skipsub);

		// Actually change the system FPCR register
		MRS(SCRATCH1_64, FIELD_FPCR);
		// Clear both flush-to-zero and rounding before re-setting them.
		ANDI2R(SCRATCH1, SCRATCH1, ~((4 | 3) << 22));
		ORR(SCRATCH1, SCRATCH1, SCRATCH2, ArithOption(SCRATCH2, ST_LSL, 22));
		_MSR(FIELD_FPCR, SCRATCH1_64);

		SetJumpTarget(skip);
		RET();
	}

	updateRoundingMode_ = AlignCode16();
	{
		LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, fcr31));

		// Set SCRATCH2 to FZ:RM (FZ is bit 24, and RM are lowest 2 bits.)
		ANDI2R(SCRATCH2, SCRATCH1, 3);
		FixupBranch skip = TBZ(SCRATCH1, 24);
		ADDI2R(SCRATCH2, SCRATCH2, 4);
		SetJumpTarget(skip);

		// Update currentRoundingFunc_ with the right convertS0ToSCRATCH1_ func.
		MOVP2R(SCRATCH1_64, convertS0ToSCRATCH1_);
		LSL(SCRATCH2, SCRATCH2, 3);
		LDR(SCRATCH2_64, SCRATCH1_64, SCRATCH2);
		MOVP2R(SCRATCH1_64, &currentRoundingFunc_);
		STR(INDEX_UNSIGNED, SCRATCH2_64, SCRATCH1_64, 0);
		RET();
	}

	hooks_.enterDispatcher = (IRNativeFuncNoArg)AlignCode16();

	uint32_t regs_to_save = Arm64Gen::ALL_CALLEE_SAVED;
	uint32_t regs_to_save_fp = Arm64Gen::ALL_CALLEE_SAVED_FP;
	fp_.ABI_PushRegisters(regs_to_save, regs_to_save_fp);

	// Fixed registers, these are always kept when in Jit context.
	MOVP2R(MEMBASEREG, mipsState->space->base);
	MOVP2R(CTXREG, mipsState);
	MOVP2R(JITBASEREG, GetBasePtr());

	LoadStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	MovFromPC(SCRATCH1);
	WriteDebugPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	if (mediaEngine)
		ApplyRoundingMode(true);
	outerLoop_ = GetCodePtr();
	if (!mediaEngine) {
		SaveStaticRegisters();  // Advance can change the downcount, so must save/restore
		RestoreRoundingMode(true);
		WriteDebugProfilerStatus(IRProfilerStatus::TIMER_ADVANCE);
		QuickCallFunctionR(SCRATCH1_64, &CoreTiming::Advance, CTXREG);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		ApplyRoundingMode(true);
		LoadStaticRegisters();
	}

		dispatcherCheckCoreState_ = GetCodePtr();

		MOVP2R(SCRATCH1_64, &coreState);
		LDR(INDEX_UNSIGNED, SCRATCH1, SCRATCH1_64, 0);
		FixupBranch badCoreState = CBNZ(SCRATCH1);

		// Check downcount.
		FixupBranch outOfTime;
		if (mediaEngine)
			outOfTime = TBNZ(DOWNCOUNTREG, 31);
		else
			TBNZ(DOWNCOUNTREG, 31, outerLoop_);
		FixupBranch skipToRealDispatch = B();

		dispatcherPCInSCRATCH1_ = GetCodePtr();
		MovToPC(SCRATCH1);

		hooks_.dispatcher = GetCodePtr();

			FixupBranch bail = TBNZ(DOWNCOUNTREG, 31);
			SetJumpTarget(skipToRealDispatch);

			dispatcherNoCheck_ = GetCodePtr();

			// Debug
			if (enableDebug) {
				MOV(W0, DOWNCOUNTREG);
				MOV(X1, MEMBASEREG);
				MOV(X2, JITBASEREG);
				QuickCallFunction(SCRATCH1_64, &ShowPC);
			}

			MovFromPC(SCRATCH1);
			WriteDebugPC(SCRATCH1);
			ANDI2R(SCRATCH1, SCRATCH1, Memory::BLOCK_SHADOW_ENTRY_MASK);
			MOVP2R(SCRATCH2_64, mipsState->space->blockShadow);
			hooks_.dispatchFetch = GetCodePtr();
			LDR(SCRATCH1, SCRATCH2_64, SCRATCH1_64);
			// The entry is the block's offset into the code space, or 0 for none.
			FixupBranch skipJump = CBZ(SCRATCH1);
				ADD(SCRATCH1_64, JITBASEREG, SCRATCH1_64);
				BR(SCRATCH1_64);
			SetJumpTarget(skipJump);

			// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
			// A replacement hook's block shadow entry jumps here too (GetBlockShadowHookValue()). JitAt
			// replaces it with the new block's entry, and destroying that block puts the hook value back.
			hooks_.blockShadowHook = GetCodePtr();
			RestoreRoundingMode(true);
			WriteDebugProfilerStatus(IRProfilerStatus::COMPILING);
			QuickCallFunctionR(SCRATCH1_64, &MIPSComp::JitAt, CTXREG);
			WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
			ApplyRoundingMode(true);

			// Let's just dispatch again, we'll enter the block since we know it's there.
			B(dispatcherNoCheck_);

		SetJumpTarget(bail);

		MOVP2R(SCRATCH1_64, &coreState);
		LDR(INDEX_UNSIGNED, SCRATCH1, SCRATCH1_64, 0);
		CBZ(SCRATCH1, outerLoop_);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);
	if (mediaEngine)
		SetJumpTarget(outOfTime);

	WriteDebugProfilerStatus(IRProfilerStatus::NOT_RUNNING);
	SaveStaticRegisters();
	RestoreRoundingMode(true);

	fp_.ABI_PopRegisters(regs_to_save, regs_to_save_fp);

	RET();

	hooks_.crashHandler = GetCodePtr();
	MOVP2R(SCRATCH1_64, &coreState);
	MOVI2R(SCRATCH2, CORE_RUNTIME_ERROR);
	STR(INDEX_UNSIGNED, SCRATCH2, SCRATCH1_64, 0);
	B(quitLoop);

	// Generate some integer conversion funcs.
	// MIPS order!
	static const RoundingMode roundModes[8] = { ROUND_N, ROUND_Z, ROUND_P, ROUND_M, ROUND_N, ROUND_Z, ROUND_P, ROUND_M };
	for (size_t i = 0; i < ARRAY_SIZE(roundModes); ++i) {
		convertS0ToSCRATCH1_[i] = AlignCode16();

		// Invert 0x80000000 -> 0x7FFFFFFF for the NAN result.
		fp_.MVNI(32, EncodeRegToDouble(SCRATCHF2), 0x80, 24);
		fp_.FCMP(S0, S0);  // Detect NaN
		fp_.FCVTS(S0, S0, roundModes[i]);
		fp_.FCSEL(S0, S0, SCRATCHF2, CC_VC);

		RET();
	}

	// Guest values live in caller-saved registers too, so these save them all around the call.
	static_assert((int)IROp::StoreKernel32 - (int)IROp::LoadKernel8 == ARRAY_SIZE(kernelLoadStore_) - 1, "kernel ops in a row");
	for (int i = 0; i < ARRAY_SIZE(kernelLoadStore_); ++i) {
		const IROp op = (IROp)((int)IROp::LoadKernel8 + i);
		const bool isStore = op >= IROp::StoreKernel8;
		kernelLoadStore_[i] = AlignCode16();
		static const int SAVE_SIZE = 16 + 16 * 8 + 32 * 16;
		SUB(SP, SP, SAVE_SIZE);
		STP(INDEX_SIGNED, X29, X30, SP, 0);
		for (int r = 0; r < 16; r += 2)
			STP(INDEX_SIGNED, (ARM64Reg)(X0 + r), (ARM64Reg)(X0 + r + 1), SP, 16 + r * 8);
		for (int r = 0; r < 32; r += 2)
			fp_.STP(128, INDEX_SIGNED, (ARM64Reg)(Q0 + r), (ARM64Reg)(Q0 + r + 1), SP, 16 + 16 * 8 + r * 16);
		MOVP2R(X0, mipsState);
		MOV(W1, SCRATCH1);
		if (isStore) {
			MOV(W2, SCRATCH2);
			MOVI2R(W3, (u32)op);
			QuickCallFunction(SCRATCH2_64, &IRRunKernelStore);
		} else {
			MOVI2R(W2, (u32)op);
			QuickCallFunction(SCRATCH2_64, &IRRunKernelLoad);
			MOV(SCRATCH1, W0);
		}
		for (int r = 0; r < 32; r += 2)
			fp_.LDP(128, INDEX_SIGNED, (ARM64Reg)(Q0 + r), (ARM64Reg)(Q0 + r + 1), SP, 16 + 16 * 8 + r * 16);
		for (int r = 0; r < 16; r += 2)
			LDP(INDEX_SIGNED, (ARM64Reg)(X0 + r), (ARM64Reg)(X0 + r + 1), SP, 16 + r * 8);
		LDP(INDEX_SIGNED, X29, X30, SP, 0);
		ADD(SP, SP, SAVE_SIZE);
		RET();
	}

	vfpuDotSpecial_ = GenerateArm64VfpuDotSpecial(this, &fp_, CTXREG);
	vfpuDot_ = GenerateArm64VfpuDotCall(this, &fp_, CTXREG, vfpuDotSpecial_);
	for (int terms = 2; terms <= 4; terms++)
		vfpuMultiDot_[terms] = GenerateArm64VfpuMultiDot(this, &fp_, CTXREG, terms);

	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
		std::vector<std::string> lines = DisassembleArm64(disasmStart, (int)(GetCodePtr() - disasmStart));
		for (auto s : lines) {
			INFO_LOG(Log::JIT, "%s", s.c_str());
		}
	}

	// Let's spare the pre-generated code from unprotect-reprotect.
	AlignCodePage();
	jitStartOffset_ = (int)(GetCodePtr() - start);
	// Don't forget to zap the instruction cache! This must stay at the end of this function.
	FlushIcache();
	EndWrite();

	// Update our current cached rounding mode func, too.
	UpdateFCR31(mipsState);
}

void Arm64JitBackend::GetFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const {
	labels.push_back({ outerLoop_, "outerLoop", false });
	labels.push_back({ outerLoopPCInSCRATCH1_, "outerLoopPCInSCRATCH1", false });
	labels.push_back({ dispatcherCheckCoreState_, "dispatcherCheckCoreState", false });
	labels.push_back({ dispatcherPCInSCRATCH1_, "dispatcherPCInSCRATCH1", false });
	labels.push_back({ dispatcherNoCheck_, "dispatcherNoCheck", false });
	labels.push_back({ restoreRoundingMode_, "restoreRoundingMode", true });
	labels.push_back({ applyRoundingMode_, "applyRoundingMode", true });
	labels.push_back({ vfpuDot_, "vfpuDot", true });
	labels.push_back({ updateRoundingMode_, "updateRoundingMode", true });
	labels.push_back({ saveStaticRegisters_, "saveStaticRegisters", true });
	labels.push_back({ loadStaticRegisters_, "loadStaticRegisters", true });
	labels.push_back({ vfpuDotSpecial_, "vfpuDotSpecial", true });
	labels.push_back({ vfpuMultiDot_[2], "vfpuMultiDot2", true });
	labels.push_back({ vfpuMultiDot_[3], "vfpuMultiDot3", true });
	labels.push_back({ vfpuMultiDot_[4], "vfpuMultiDot4", true });
	for (int i = 0; i < 8; ++i)
		labels.push_back({ convertS0ToSCRATCH1_[i], "convertS0ToSCRATCH1", true });
}

} // namespace MIPSComp

#endif
