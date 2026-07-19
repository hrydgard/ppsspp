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
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include <string>
#include <vector>
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

namespace MIPSComp {

class Arm64JitBackend : public Arm64Gen::ARM64CodeBlock, public IRNativeBackend {
public:
	Arm64JitBackend(JitOptions &jo, IRBlockCache &blocks);
	~Arm64JitBackend();

	bool DescribeCodePtr(const u8 *ptr, std::string &name) const override;

	void GenerateFixedCode(MIPSState *mipsState) override;
	bool CompileBlock(IRBlockCache *irBlockCache, int block_num) override;
	void ClearAllBlocks() override;
	void InvalidateBlock(IRBlockCache *irBlockCache, int block_num) override;

	void UpdateFCR31(MIPSState *mipsState) override;

protected:
	const CodeBlockCommon &CodeBlock() const override {
		return *this;
	}

private:
	friend class Arm64MEJitBackend;

	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode(bool force = false);
	void MovFromPC(Arm64Gen::ARM64Reg r);
	void MovToPC(Arm64Gen::ARM64Reg r);
	// Destroys SCRATCH2.
	void WriteDebugPC(uint32_t pc);
	void WriteDebugPC(Arm64Gen::ARM64Reg r);
	// Destroys SCRATCH2.
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

	struct LoadStoreArg {
		Arm64Gen::ARM64Reg base = Arm64Gen::INVALID_REG;
		Arm64Gen::ARM64Reg regOffset = Arm64Gen::INVALID_REG;
		int immOffset = 0;
		bool useUnscaled = false;
		bool useRegisterOffset = false;
		bool signExtendRegOffset = false;
	};
	LoadStoreArg PrepareSrc1Address(IRInst inst);

	JitOptions &jo;
	Arm64IRRegCache regs_;
	Arm64Gen::ARM64FloatEmitter fp_;

	const u8 *outerLoop_ = nullptr;
	const u8 *outerLoopPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherCheckCoreState_ = nullptr;
	const u8 *dispatcherPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherNoCheck_ = nullptr;
	const u8 *restoreRoundingMode_ = nullptr;
	const u8 *applyRoundingMode_ = nullptr;
	const u8 *updateRoundingMode_ = nullptr;

	const u8 *saveStaticRegisters_ = nullptr;
	const u8 *loadStaticRegisters_ = nullptr;

	// Indexed by FPCR FZ:RN bits for convenience.  Uses SCRATCH2.
	const u8 *convertS0ToSCRATCH1_[8];

	// Note: mutable state used at runtime.
	const u8 *currentRoundingFunc_ = nullptr;

	int jitStartOffset_ = 0;
	int compilingBlockNum_ = -1;
	int logBlocks_ = 0;
	// Only useful in breakpoints, where it's set immediately prior.
	uint32_t lastConstPC_ = 0;
};

class Arm64IRJit : public IRNativeJit {
public:
	Arm64IRJit(MIPSState *mipsState)
		: IRNativeJit(mipsState), arm64Backend_(jo, blocks_) {
		Init(arm64Backend_);
	}

private:
	Arm64JitBackend arm64Backend_;
};

// ME variant with a simpler dispatcher.
class Arm64MEJitBackend : public Arm64JitBackend {
public:
	using Arm64JitBackend::Arm64JitBackend;
	void GenerateFixedCode(MIPSState *mipsState) override;
};

class Arm64MEIRJit : public IRNativeJit {
public:
	Arm64MEIRJit(MIPSState *mipsState)
		: IRNativeJit(mipsState), meBackend_(jo, blocks_) {
		// Do not patch EMUHACKs into RAM. The main CPU JIT shares the same
		// address space.
		jo.enableBlocklink = false;
		// Keep address masking on the MEMBASEREG path for kseg0/kseg1.
		jo.enablePointerify = false;
		// Let CompIR_* be conservative around sensitive MMIO accesses.
		jo.isMeJit = true;
		blocks_.SetPatchMemory(false);
		Init(meBackend_);
		memset(fastCache_, 0, sizeof(fastCache_));
		g_meFastCache = fastCache_;
	}

	// Returns the native entry for pc, compiling on demand.
	const u8 *CompileAndLookup(u32 pc);

	// Runs native blocks until downcount < 0.
	void EnterDispatcher() {
		hooks_.enterDispatcher();
	}

	void ClearCache() override {
		IRNativeJit::ClearCache();
		memset(fastCache_, 0, sizeof(fastCache_));
	}

	void InvalidateCacheAt(u32 em_address, int length = 4) override {
		IRNativeJit::InvalidateCacheAt(em_address, length);
		memset(fastCache_, 0, sizeof(fastCache_));
	}

	// Fast direct-mapped cache: PC -> native entry pointer.
	static constexpr int kFastCacheShift = 2;  // Instructions are 4-byte aligned
	static constexpr int kFastCacheSize = 4096;
	static constexpr int kFastCacheMask = kFastCacheSize - 1;
	struct FastCacheEntry {
		u32 pc;
		u32 pad;  // Align nativeEntry to 8 bytes within 16-byte entry
		const u8 *nativeEntry;
	};
	static_assert(sizeof(FastCacheEntry) == 16, "FastCacheEntry must be 16 bytes for asm indexing");

	// Shared with generated asm.
	static FastCacheEntry *g_meFastCache;

private:
	Arm64MEJitBackend meBackend_;
	FastCacheEntry fastCache_[kFastCacheSize];
};

} // namespace MIPSComp

#endif
