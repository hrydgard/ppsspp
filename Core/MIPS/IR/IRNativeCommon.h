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

#include <map>
#include <unordered_map>

#include "Common/CodeBlock.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

namespace MIPSComp {

typedef void (*IRNativeFuncNoArg)();

enum class IRProfilerStatus : int32_t {
	NOT_RUNNING,
	IN_JIT,
	TIMER_ADVANCE,
	COMPILING,
	MATH_HELPER,
	REPLACEMENT,
	SYSCALL,
	INTERPRET,
	IR_INTERPRET,
};

struct IRNativeHooks {
	IRNativeFuncNoArg enterDispatcher = nullptr;

	const uint8_t *dispatcher = nullptr;
	const uint8_t *dispatchFetch = nullptr;
	const uint8_t *crashHandler = nullptr;
	// The dispatcher's compile path, where a replacement hook's block shadow entry goes.
	const uint8_t *blockShadowHook = nullptr;

	uint32_t *profilerPC = nullptr;
	IRProfilerStatus *profilerStatus = nullptr;
};

struct IRNativeBlockExit {
	int offset;
	int len;
	uint32_t dest;
};

// Names a piece of the fixed code, for the profiler.
struct IRNativeFixedCodeLabel {
	const u8 *ptr;
	const char *name;
	// A leaf routine blocks call, so the return address (LR, [rsp]) says which block.
	bool calledFromBlocks;
};

struct IRNativeBlock {
	// The guest address the block starts at, and the offset of its code (what the block shadow holds).
	u32 startPC = 0;
	int entryOffset = 0;
	int checkedOffset = 0;
	std::vector<IRNativeBlockExit> exits;
	// Code offset of each IR instruction, only kept when profiling.
	std::vector<int> irOffsets;
};

// The native code space of every backend. Blocks have to reach the dispatcher at its start, and
// ARM32 branches reach 32 MB.
constexpr int NATIVE_CODE_SPACE_SIZE = 24 * 1024 * 1024;

class IRNativeBackend {
public:
	virtual ~IRNativeBackend();

	void CompileIRInst(IRInst inst);

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name) const;
	bool CodeInRange(const u8 *ptr) const;
	int OffsetFromCodePtr(const u8 *ptr);

	void SetMIPSState(MIPSState *mips) {
		mips_ = mips;
	}
	virtual void GenerateFixedCode(MIPSState *mipsState) = 0;
	// Block numbers are the caller's, and only grow until ClearAllBlocks.
	virtual bool CompileBlock(int block_num, u32 startPC, const IRInst *instructions, int count) = 0;
	virtual void ClearAllBlocks() = 0;
	virtual void InvalidateBlock(int block_num) = 0;
	// Links the block's exits, and other blocks' exits to it.
	void FinalizeBlock(int block_num, const JitOptions &jo);

	virtual void UpdateFCR31(MIPSState *mipsState) {}

	const IRNativeHooks &GetNativeHooks() const {
		return hooks_;
	}

	const IRNativeBlock *GetNativeBlock(int block_num) const;
	int GetNumNativeBlocks() const {
		return (int)nativeBlocks_.size();
	}
	int GetBlockEntry(int block_num) const {
		return nativeBlocks_[block_num].entryOffset;
	}
	// Where the block's code starts: its entry, or its checked entry if that comes first. Blocks'
	// code follows in block order.
	int GetBlockCodeStart(int block_num) const;
	void SetBlockCheckedOffset(int block_num, int offset);
	void SetProfiling(bool profiling) {
		profiling_ = profiling;
	}

	virtual const CodeBlockCommon &CodeBlock() const = 0;

	// The backend's own routines, without the dispatcher hooks.
	virtual void GetFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const {}
	// Those and the dispatcher hooks, by address.
	void GetAllFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const;
	// The name of the fixed code routine ptr is in, with an offset if not at its start, for the
	// disassembly and profilers (so no spaces).
	std::string DescribeFixedCode(const u8 *ptr) const;

protected:
	virtual void CompIR_Arith(IRInst inst) = 0;
	virtual void CompIR_Assign(IRInst inst) = 0;
	virtual void CompIR_Basic(IRInst inst) = 0;
	virtual void CompIR_Bits(IRInst inst) = 0;
	virtual void CompIR_Breakpoint(IRInst inst) = 0;
	virtual void CompIR_Compare(IRInst inst) = 0;
	virtual void CompIR_CondAssign(IRInst inst) = 0;
	virtual void CompIR_CondStore(IRInst inst) = 0;
	virtual void CompIR_Div(IRInst inst) = 0;
	virtual void CompIR_Exit(IRInst inst) = 0;
	virtual void CompIR_ExitIf(IRInst inst) = 0;
	virtual void CompIR_FArith(IRInst inst) = 0;
	virtual void CompIR_FAssign(IRInst inst) = 0;
	virtual void CompIR_FCompare(IRInst inst) = 0;
	virtual void CompIR_FCondAssign(IRInst inst) = 0;
	virtual void CompIR_FCvt(IRInst inst) = 0;
	virtual void CompIR_FLoad(IRInst inst) = 0;
	virtual void CompIR_FRound(IRInst inst) = 0;
	virtual void CompIR_FSat(IRInst inst) = 0;
	virtual void CompIR_FSpecial(IRInst inst) = 0;
	virtual void CompIR_FStore(IRInst inst) = 0;
	virtual void CompIR_Generic(IRInst inst) = 0;
	virtual void CompIR_HiLo(IRInst inst) = 0;
	virtual void CompIR_Interpret(IRInst inst) = 0;
	virtual void CompIR_Load(IRInst inst) = 0;
	virtual void CompIR_LoadShift(IRInst inst) = 0;
	virtual void CompIR_Logic(IRInst inst) = 0;
	virtual void CompIR_Mult(IRInst inst) = 0;
	virtual void CompIR_RoundingMode(IRInst inst) = 0;
	virtual void CompIR_Shift(IRInst inst) = 0;
	virtual void CompIR_Store(IRInst inst) = 0;
	virtual void CompIR_StoreShift(IRInst inst) = 0;
	virtual void CompIR_System(IRInst inst) = 0;
	virtual void CompIR_Transfer(IRInst inst) = 0;
	virtual void CompIR_VecArith(IRInst inst) = 0;
	virtual void CompIR_VecAssign(IRInst inst) = 0;
	virtual void CompIR_VecHoriz(IRInst inst) = 0;
	virtual void CompIR_VecLoad(IRInst inst) = 0;
	virtual void CompIR_VecPack(IRInst inst) = 0;
	virtual void CompIR_VecStore(IRInst inst) = 0;
	virtual void CompIR_ValidateAddress(IRInst inst) = 0;
	// Kernel code's loads and stores, which may be hardware registers.
	virtual void CompIR_KernelLoadStore(IRInst inst) { CompIR_Generic(inst); }

	virtual void OverwriteExit(int srcOffset, int len, int block_num) = 0;

	// Returns true when debugging statistics should be compiled in.
	bool DebugStatsEnabled() const;
	bool DebugProfilerEnabled() const;

	// Callback (compile when DebugStatsEnabled()) to log a base interpreter hit.
	// Call the func returned by MIPSGetInterpretFunc(op) directly for interpret.
	static void NotifyMIPSInterpret(const char *name);

	// Callback to log AND perform a base interpreter op.  Alternative to NotifyMIPSInterpret().
	static void DoMIPSInst(uint32_t op, MIPSState *mips);

	// Callback to log AND perform an IR interpreter inst.  Returns 0 or a PC to jump to.
	static uint32_t DoIRInst(uint64_t inst, MIPSState *mips);

	static int ReportBadAddress(uint32_t addr, uint32_t alignment, uint32_t isWrite, MIPSState *mips);

	void SetBlockEntry(int block_num, u32 startPC, int offset);
	// The live block starting at pc, or -1.
	int FindBlockByStart(u32 pc) const;
	void AddLinkableExit(int block_num, uint32_t pc, int exitStartOffset, int exitLen);
	// Forgets the block's links, and its start. -1 for all blocks.
	void EraseAllLinks(int block_num);
	// For the profiler's report. Does nothing unless profiling.
	void RecordIRAddresses(int block_num, const std::vector<const u8 *> &addresses);

	IRNativeHooks hooks_;
	// The CPU the generated code runs, for the calls out of it.
	MIPSState *mips_ = nullptr;
	std::vector<IRNativeBlock> nativeBlocks_;
	std::unordered_multimap<uint32_t, int> linksTo_;
	std::unordered_map<uint32_t, int> blockByStart_;
	bool profiling_ = false;
};

class IRNativeJit : public IRJit {
public:
	IRNativeJit(MIPSState *mipsState);

	void RunLoopUntil(u64 globalticks) override;
	void RunDowncount() override;

	void ClearCache() override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	bool CodeInRange(const u8 *ptr) const override;
	bool IsAtDispatchFetch(const u8 *ptr) const override;
	u32 GetBlockShadowHookValue() const override;
	const u8 *GetDispatcher() const override;
	const u8 *GetCrashHandler() const override;

	void UpdateFCR31() override;
	void FlushProfile() override;

	const u8 *GetCodeBase() const override;
	void GetStats(JitStats *stats) const override;

protected:
	void Init(IRNativeBackend &backend);
	CompileResult CompileTargetBlock(int block_num, const std::vector<IRInst> &instructions) override;
	// Compiles the IR again, since only the native code is kept.
	void GetBlockInstructions(const JitBlock &block, std::vector<IRInst> &instructions) const override;
	int GetBlockCodeSize(int block_num) const override;
	void GetBlockTargetDisasm(int block_num, std::vector<std::string> &lines) const override;
	void GetBlockCodeRange(int block_num, int *startOffset, int *size) const;
	void FinalizeTargetBlock(int block_num) override;
	void InvalidateTargetBlock(int block_num) override;

	void CollectProfileSamples();
	int FindBlockByCodeOffset(int offset) const;
	int FindBlockOffsetEnd() const;
	void WriteProfileReport() override;

	struct BlockProfile {
		int64_t samples = 0;
		// Samples in a routine the block called (like the dot product).
		int64_t routineSamples = 0;
		std::map<int, int64_t> byOffset;
		// routineSamples by the code offset the routine returns to.
		std::map<int, int64_t> routineByReturn;
	};
	std::unordered_map<int, BlockProfile> blockProfiles_;
	std::unordered_map<uintptr_t, int64_t> hostProfile_;
	// Sorted by ptr. Samples by label, split by whether a block was charged for them.
	std::vector<IRNativeFixedCodeLabel> fixedLabels_;
	std::map<std::string, int64_t> fixedCodeByLabel_;
	std::map<std::string, int64_t> routinesByLabel_;
	// All fixed code samples, by code offset.
	std::map<int, int64_t> fixedCodeByOffset_;
	int64_t fixedCodeSamples_ = 0;
	int64_t totalSamples_ = 0;

	IRNativeBackend *backend_ = nullptr;
	IRNativeHooks hooks_;
};

} // namespace MIPSComp
