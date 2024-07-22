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

#include <unordered_map>
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

	uint32_t *profilerPC = nullptr;
	IRProfilerStatus *profilerStatus = nullptr;
};

struct IRNativeBlockExit {
	int offset;
	int len;
	uint32_t dest;
};

struct IRNativeBlock {
	int checkedOffset = 0;
	std::vector<IRNativeBlockExit> exits;
};

class IRNativeBackend {
public:
	IRNativeBackend(IRBlockCache &blocks);
	virtual ~IRNativeBackend();

	void CompileIRInst(IRInst inst);

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name) const;
	bool CodeInRange(const u8 *ptr) const;
	int OffsetFromCodePtr(const u8 *ptr);

	virtual void GenerateFixedCode(MIPSState *mipsState) = 0;
	virtual bool CompileBlock(IRBlockCache *irBlockCache, int block_num, bool preload) = 0;
	virtual void ClearAllBlocks() = 0;
	virtual void InvalidateBlock(IRBlockCache *irBlockCache, int block_num) = 0;
	void FinalizeBlock(IRBlockCache *irBlockCache, int block_num, const JitOptions &jo);

	virtual void UpdateFCR31(MIPSState *mipsState) {}

	const IRNativeHooks &GetNativeHooks() const {
		return hooks_;
	}

	const IRNativeBlock *GetNativeBlock(int block_num) const;
	void SetBlockCheckedOffset(int block_num, int offset);

	virtual const CodeBlockCommon &CodeBlock() const = 0;

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
	virtual void CompIR_VecClamp(IRInst inst) = 0;
	virtual void CompIR_VecHoriz(IRInst inst) = 0;
	virtual void CompIR_VecLoad(IRInst inst) = 0;
	virtual void CompIR_VecPack(IRInst inst) = 0;
	virtual void CompIR_VecStore(IRInst inst) = 0;
	virtual void CompIR_ValidateAddress(IRInst inst) = 0;

	virtual void OverwriteExit(int srcOffset, int len, int block_num) = 0;

	// Returns true when debugging statistics should be compiled in.
	bool DebugStatsEnabled() const;
	bool DebugProfilerEnabled() const;

	// Callback (compile when DebugStatsEnabled()) to log a base interpreter hit.
	// Call the func returned by MIPSGetInterpretFunc(op) directly for interpret.
	static void NotifyMIPSInterpret(const char *name);

	// Callback to log AND perform a base interpreter op.  Alternative to NotifyMIPSInterpret().
	static void DoMIPSInst(uint32_t op);

	// Callback to log AND perform an IR interpreter inst.  Returns 0 or a PC to jump to.
	static uint32_t DoIRInst(uint64_t inst);

	static int ReportBadAddress(uint32_t addr, uint32_t alignment, uint32_t isWrite);

	void AddLinkableExit(int block_num, uint32_t pc, int exitStartOffset, int exitLen);
	void EraseAllLinks(int block_num);

	IRNativeHooks hooks_;
	IRBlockCache &blocks_;
	std::vector<IRNativeBlock> nativeBlocks_;
	std::unordered_multimap<uint32_t, int> linksTo_;
};

class IRNativeBlockCacheDebugInterface : public JitBlockCacheDebugInterface {
public:
	IRNativeBlockCacheDebugInterface(const MIPSComp::IRBlockCache &irBlocks);
	void Init(const IRNativeBackend *backend);
	int GetNumBlocks() const override;
	int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true) const override;
	JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const override;
	JitBlockMeta GetBlockMeta(int blockNum) const override;
	JitBlockProfileStats GetBlockProfileStats(int blockNum) const override;
	void ComputeStats(BlockCacheStats &bcStats) const override;
	bool IsValidBlock(int blockNum) const override;

private:
	void GetBlockCodeRange(int blockNum, int *startOffset, int *size) const;

	const MIPSComp::IRBlockCache &irBlocks_;
	const CodeBlockCommon *codeBlock_ = nullptr;
	const IRNativeBackend *backend_ = nullptr;
};

class IRNativeJit : public IRJit {
public:
	IRNativeJit(MIPSState *mipsState);

	void RunLoopUntil(u64 globalticks) override;

	void ClearCache() override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	bool CodeInRange(const u8 *ptr) const override;
	bool IsAtDispatchFetch(const u8 *ptr) const override;
	const u8 *GetDispatcher() const override;
	const u8 *GetCrashHandler() const override;

	void UpdateFCR31() override;

	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override;

protected:
	void Init(IRNativeBackend &backend);
	bool CompileNativeBlock(IRBlockCache *irBlockCache, int block_num, bool preload) override;
	void FinalizeNativeBlock(IRBlockCache *irBlockCache, int block_num) override;

	IRNativeBackend *backend_ = nullptr;
	IRNativeHooks hooks_;
	IRNativeBlockCacheDebugInterface debugInterface_;
};

} // namespace MIPSComp
