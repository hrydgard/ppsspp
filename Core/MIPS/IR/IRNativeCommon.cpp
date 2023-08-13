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

#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/IR/IRNativeCommon.h"

using namespace MIPSComp;

namespace MIPSComp {

// Compile time flag to enable debug stats for not compiled ops.
static constexpr bool enableDebugStats = false;

// Used only for debugging when enableDebug is true above.
static std::map<uint8_t, int> debugSeenNotCompiledIR;
static std::map<const char *, int> debugSeenNotCompiled;
static double lastDebugStatsLog = 0.0;

static void LogDebugStats() {
	if (!enableDebugStats)
		return;

	double now = time_now_d();
	if (now < lastDebugStatsLog + 1.0)
		return;
	lastDebugStatsLog = now;

	int worstIROp = -1;
	int worstIRVal = 0;
	for (auto it : debugSeenNotCompiledIR) {
		if (it.second > worstIRVal) {
			worstIRVal = it.second;
			worstIROp = it.first;
		}
	}
	debugSeenNotCompiledIR.clear();

	const char *worstName = nullptr;
	int worstVal = 0;
	for (auto it : debugSeenNotCompiled) {
		if (it.second > worstVal) {
			worstVal = it.second;
			worstName = it.first;
		}
	}
	debugSeenNotCompiled.clear();

	if (worstIROp != -1)
		WARN_LOG(JIT, "Most not compiled IR op: %s (%d)", GetIRMeta((IROp)worstIROp)->name, worstIRVal);
	if (worstName != nullptr)
		WARN_LOG(JIT, "Most not compiled op: %s (%d)", worstName, worstVal);
}

bool IRNativeBackend::DebugStatsEnabled() const {
	return enableDebugStats;
}

void IRNativeBackend::NotifyMIPSInterpret(const char *name) {
	_assert_(enableDebugStats);
	debugSeenNotCompiled[name]++;
}

void IRNativeBackend::DoMIPSInst(uint32_t value) {
	MIPSOpcode op;
	memcpy(&op, &value, sizeof(op));

	if constexpr (enableDebugStats)
		debugSeenNotCompiled[MIPSGetName(op)]++;

	MIPSInterpret(op);
}

uint32_t IRNativeBackend::DoIRInst(uint64_t value) {
	IRInst inst;
	memcpy(&inst, &value, sizeof(inst));

	if constexpr (enableDebugStats)
		debugSeenNotCompiledIR[(uint8_t)inst.op]++;

	return IRInterpret(currentMIPS, &inst, 1);
}

IRNativeBackend::IRNativeBackend(IRBlockCache &blocks) : blocks_(blocks) {}

void IRNativeBackend::CompileIRInst(IRInst inst) {
	switch (inst.op) {
	case IROp::Nop:
		break;

	case IROp::SetConst:
	case IROp::SetConstF:
	case IROp::Downcount:
	case IROp::SetPC:
	case IROp::SetPCConst:
		CompIR_Basic(inst);
		break;

	case IROp::Add:
	case IROp::Sub:
	case IROp::AddConst:
	case IROp::SubConst:
	case IROp::Neg:
		CompIR_Arith(inst);
		break;

	case IROp::And:
	case IROp::Or:
	case IROp::Xor:
	case IROp::AndConst:
	case IROp::OrConst:
	case IROp::XorConst:
	case IROp::Not:
		CompIR_Logic(inst);
		break;

	case IROp::Mov:
	case IROp::Ext8to32:
	case IROp::Ext16to32:
		CompIR_Assign(inst);
		break;

	case IROp::ReverseBits:
	case IROp::BSwap16:
	case IROp::BSwap32:
	case IROp::Clz:
		CompIR_Bits(inst);
		break;

	case IROp::Shl:
	case IROp::Shr:
	case IROp::Sar:
	case IROp::Ror:
	case IROp::ShlImm:
	case IROp::ShrImm:
	case IROp::SarImm:
	case IROp::RorImm:
		CompIR_Shift(inst);
		break;

	case IROp::Slt:
	case IROp::SltConst:
	case IROp::SltU:
	case IROp::SltUConst:
		CompIR_Compare(inst);
		break;

	case IROp::MovZ:
	case IROp::MovNZ:
	case IROp::Max:
	case IROp::Min:
		CompIR_CondAssign(inst);
		break;

	case IROp::MtLo:
	case IROp::MtHi:
	case IROp::MfLo:
	case IROp::MfHi:
		CompIR_HiLo(inst);
		break;

	case IROp::Mult:
	case IROp::MultU:
	case IROp::Madd:
	case IROp::MaddU:
	case IROp::Msub:
	case IROp::MsubU:
		CompIR_Mult(inst);
		break;

	case IROp::Div:
	case IROp::DivU:
		CompIR_Div(inst);
		break;

	case IROp::Load8:
	case IROp::Load8Ext:
	case IROp::Load16:
	case IROp::Load16Ext:
	case IROp::Load32:
	case IROp::Load32Linked:
		CompIR_Load(inst);
		break;

	case IROp::Load32Left:
	case IROp::Load32Right:
		CompIR_LoadShift(inst);
		break;

	case IROp::LoadFloat:
		CompIR_FLoad(inst);
		break;

	case IROp::LoadVec4:
		CompIR_VecLoad(inst);
		break;

	case IROp::Store8:
	case IROp::Store16:
	case IROp::Store32:
		CompIR_Store(inst);
		break;

	case IROp::Store32Conditional:
		CompIR_CondStore(inst);
		break;

	case IROp::Store32Left:
	case IROp::Store32Right:
		CompIR_StoreShift(inst);
		break;

	case IROp::StoreFloat:
		CompIR_FStore(inst);
		break;

	case IROp::StoreVec4:
		CompIR_VecStore(inst);
		break;

	case IROp::FAdd:
	case IROp::FSub:
	case IROp::FMul:
	case IROp::FDiv:
	case IROp::FSqrt:
	case IROp::FNeg:
		CompIR_FArith(inst);
		break;

	case IROp::FMin:
	case IROp::FMax:
		CompIR_FCondAssign(inst);
		break;

	case IROp::FMov:
	case IROp::FAbs:
	case IROp::FSign:
		CompIR_FAssign(inst);
		break;

	case IROp::FRound:
	case IROp::FTrunc:
	case IROp::FCeil:
	case IROp::FFloor:
		CompIR_FRound(inst);
		break;

	case IROp::FCvtWS:
	case IROp::FCvtSW:
	case IROp::FCvtScaledWS:
	case IROp::FCvtScaledSW:
		CompIR_FCvt(inst);
		break;

	case IROp::FSat0_1:
	case IROp::FSatMinus1_1:
		CompIR_FSat(inst);
		break;

	case IROp::FCmp:
	case IROp::FCmovVfpuCC:
	case IROp::FCmpVfpuBit:
	case IROp::FCmpVfpuAggregate:
		CompIR_FCompare(inst);
		break;

	case IROp::RestoreRoundingMode:
	case IROp::ApplyRoundingMode:
	case IROp::UpdateRoundingMode:
		CompIR_RoundingMode(inst);
		break;

	case IROp::SetCtrlVFPU:
	case IROp::SetCtrlVFPUReg:
	case IROp::SetCtrlVFPUFReg:
	case IROp::FpCondFromReg:
	case IROp::FpCondToReg:
	case IROp::FpCtrlFromReg:
	case IROp::FpCtrlToReg:
	case IROp::VfpuCtrlToReg:
	case IROp::FMovFromGPR:
	case IROp::FMovToGPR:
		CompIR_Transfer(inst);
		break;

	case IROp::Vec4Init:
	case IROp::Vec4Shuffle:
	case IROp::Vec4Blend:
	case IROp::Vec4Mov:
		CompIR_VecAssign(inst);
		break;

	case IROp::Vec4Add:
	case IROp::Vec4Sub:
	case IROp::Vec4Mul:
	case IROp::Vec4Div:
	case IROp::Vec4Scale:
	case IROp::Vec4Neg:
	case IROp::Vec4Abs:
		CompIR_VecArith(inst);
		break;

	case IROp::Vec4Dot:
		CompIR_VecHoriz(inst);
		break;

	case IROp::Vec2Unpack16To31:
	case IROp::Vec2Unpack16To32:
	case IROp::Vec4Unpack8To32:
	case IROp::Vec4DuplicateUpperBitsAndShift1:
	case IROp::Vec4Pack31To8:
	case IROp::Vec4Pack32To8:
	case IROp::Vec2Pack31To16:
	case IROp::Vec2Pack32To16:
		CompIR_VecPack(inst);
		break;

	case IROp::Vec4ClampToZero:
	case IROp::Vec2ClampToZero:
		CompIR_VecClamp(inst);
		break;

	case IROp::FSin:
	case IROp::FCos:
	case IROp::FRSqrt:
	case IROp::FRecip:
	case IROp::FAsin:
		CompIR_FSpecial(inst);
		break;

	case IROp::Interpret:
		CompIR_Interpret(inst);
		break;

	case IROp::Syscall:
	case IROp::CallReplacement:
	case IROp::Break:
		CompIR_System(inst);
		break;

	case IROp::Breakpoint:
	case IROp::MemoryCheck:
		CompIR_Breakpoint(inst);
		break;

	case IROp::ValidateAddress8:
	case IROp::ValidateAddress16:
	case IROp::ValidateAddress32:
	case IROp::ValidateAddress128:
		CompIR_ValidateAddress(inst);
		break;

	case IROp::ExitToConst:
	case IROp::ExitToReg:
	case IROp::ExitToPC:
		CompIR_Exit(inst);
		break;

	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
	case IROp::ExitToConstIfGtZ:
	case IROp::ExitToConstIfGeZ:
	case IROp::ExitToConstIfLtZ:
	case IROp::ExitToConstIfLeZ:
	case IROp::ExitToConstIfFpTrue:
	case IROp::ExitToConstIfFpFalse:
		CompIR_ExitIf(inst);
		break;

	default:
		_assert_msg_(false, "Unexpected IR op %d", (int)inst.op);
		CompIR_Generic(inst);
		break;
	}
}

IRNativeJit::IRNativeJit(MIPSState *mipsState)
	: IRJit(mipsState), debugInterface_(blocks_) {}

void IRNativeJit::Init(IRNativeBackend &backend) {
	backend_ = &backend;
	debugInterface_.Init(backend_);
	backend_->GenerateFixedCode(mips_);

	// Wanted this to be a reference, but vtbls get in the way.  Shouldn't change.
	hooks_ = backend.GetNativeHooks();
}

bool IRNativeJit::CompileTargetBlock(IRBlock *block, int block_num, bool preload) {
	return backend_->CompileBlock(block, block_num, preload);
}

void IRNativeJit::FinalizeTargetBlock(IRBlock *block, int block_num) {
	backend_->FinalizeBlock(block, block_num, jo);
}

void IRNativeJit::RunLoopUntil(u64 globalticks) {
	if constexpr (enableDebugStats) {
		LogDebugStats();
	}

	PROFILE_THIS_SCOPE("jit");
	hooks_.enterDispatcher();
}

void IRNativeJit::ClearCache() {
	IRJit::ClearCache();
	backend_->ClearAllBlocks();
}

void IRNativeJit::InvalidateCacheAt(u32 em_address, int length) {
	std::vector<int> numbers = blocks_.FindInvalidatedBlockNumbers(em_address, length);
	for (int block_num : numbers) {
		auto block = blocks_.GetBlock(block_num);
		if (em_address != 0 || length < 0x1FFFFFFF) {
			backend_->InvalidateBlock(block, block_num);
		}
		block->Destroy(block->GetTargetOffset());
	}

	if (em_address == 0 && length >= 0x1FFFFFFF) {
		backend_->ClearAllBlocks();
	}
}

bool IRNativeJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (ptr != nullptr && backend_->DescribeCodePtr(ptr, name))
		return true;

	int offset = backend_->OffsetFromCodePtr(ptr);
	if (offset == -1)
		return false;

	int block_num = -1;
	for (int i = 0; i < blocks_.GetNumBlocks(); ++i) {
		const auto &b = blocks_.GetBlock(i);
		// We allocate linearly.
		if (b->GetTargetOffset() <= offset)
			block_num = i;
		if (b->GetTargetOffset() > offset)
			break;
	}

	if (block_num == -1) {
		name = "(unknown or deleted block)";
		return true;
	}

	const IRBlock *block = blocks_.GetBlock(block_num);
	if (block) {
		u32 start = 0, size = 0;
		block->GetRange(start, size);
		name = StringFromFormat("(block %d at %08x)", block_num, start);
		return true;
	}
	return false;
}

bool IRNativeJit::CodeInRange(const u8 *ptr) const {
	return backend_->CodeInRange(ptr);
}

bool IRNativeJit::IsAtDispatchFetch(const u8 *ptr) const {
	return ptr == backend_->GetNativeHooks().dispatchFetch;
}

const u8 *IRNativeJit::GetDispatcher() const {
	return backend_->GetNativeHooks().dispatcher;
}

const u8 *IRNativeJit::GetCrashHandler() const {
	return backend_->GetNativeHooks().crashHandler;
}

JitBlockCacheDebugInterface *IRNativeJit::GetBlockCacheDebugInterface() {
	return &debugInterface_;
}

bool IRNativeBackend::CodeInRange(const u8 *ptr) const {
	return CodeBlock().IsInSpace(ptr);
}

bool IRNativeBackend::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	if (!CodeBlock().IsInSpace(ptr))
		return false;

	// Used in disassembly viewer.
	if (ptr == (const uint8_t *)hooks_.enterDispatcher) {
		name = "enterDispatcher";
	} else if (ptr == hooks_.dispatcher) {
		name = "dispatcher";
	} else if (ptr == hooks_.dispatchFetch) {
		name = "dispatchFetch";
	} else if (ptr == hooks_.crashHandler) {
		name = "crashHandler";
	} else {
		return false;
	}
	return true;
}

int IRNativeBackend::OffsetFromCodePtr(const u8 *ptr) {
	auto &codeBlock = CodeBlock();
	if (!codeBlock.IsInSpace(ptr))
		return -1;
	return (int)codeBlock.GetOffset(ptr);
}

void IRNativeBackend::FinalizeBlock(IRBlock *block, int block_num, const JitOptions &jo) {
	if (jo.enableBlocklink) {
		uint32_t pc = block->GetOriginalStart();

		// First, link other blocks to this one now that it's finalized.
		auto incoming = linksTo_.equal_range(pc);
		for (auto it = incoming.first; it != incoming.second; ++it) {
			auto &exits = nativeBlocks_[it->second].exits;
			for (auto &blockExit : exits) {
				if (blockExit.dest == pc)
					OverwriteExit(blockExit.offset, blockExit.len, block_num);
			}
		}

		// And also any blocks from this one, in case we're finalizing it later.
		auto &outgoing = nativeBlocks_[block_num].exits;
		for (auto &blockExit : outgoing) {
			int dstBlockNum = blocks_.GetBlockNumberFromStartAddress(blockExit.dest);
			const IRNativeBlock *nativeBlock = GetNativeBlock(dstBlockNum);
			if (nativeBlock)
				OverwriteExit(blockExit.offset, blockExit.len, dstBlockNum);
		}
	}
}

const IRNativeBlock *IRNativeBackend::GetNativeBlock(int block_num) const {
	if (block_num < 0 || block_num >= (int)nativeBlocks_.size())
		return nullptr;
	return &nativeBlocks_[block_num];
}

void IRNativeBackend::SetBlockCheckedOffset(int block_num, int offset) {
	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);

	nativeBlocks_[block_num].checkedOffset = offset;
}

void IRNativeBackend::AddLinkableExit(int block_num, uint32_t pc, int exitStartOffset, int exitLen) {
	linksTo_.insert(std::make_pair(pc, block_num));

	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);
	IRNativeBlockExit blockExit;
	blockExit.offset = exitStartOffset;
	blockExit.len = exitLen;
	blockExit.dest = pc;
	nativeBlocks_[block_num].exits.push_back(blockExit);
}

void IRNativeBackend::EraseAllLinks(int block_num) {
	if (block_num == -1) {
		linksTo_.clear();
		nativeBlocks_.clear();
	} else {
		linksTo_.erase(block_num);
		if (block_num < (int)nativeBlocks_.size())
			nativeBlocks_[block_num].exits.clear();
	}
}

IRNativeBlockCacheDebugInterface::IRNativeBlockCacheDebugInterface(const IRBlockCache &irBlocks)
	: irBlocks_(irBlocks) {}

void IRNativeBlockCacheDebugInterface::Init(const IRNativeBackend *backend) {
	codeBlock_ = &backend->CodeBlock();
	backend_ = backend;
}

int IRNativeBlockCacheDebugInterface::GetNumBlocks() const {
	return irBlocks_.GetNumBlocks();
}

int IRNativeBlockCacheDebugInterface::GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly) const {
	return irBlocks_.GetBlockNumberFromStartAddress(em_address, realBlocksOnly);
}

void IRNativeBlockCacheDebugInterface::GetBlockCodeRange(int blockNum, int *startOffset, int *size) const {
	int blockOffset = irBlocks_.GetBlock(blockNum)->GetTargetOffset();
	int endOffset = backend_->GetNativeBlock(blockNum)->checkedOffset;

	// If endOffset is before, the checked entry is before the block start.
	if (endOffset < blockOffset) {
		// We assume linear allocation.  Maybe a bit dangerous, should always be right.
		if (blockNum + 1 >= GetNumBlocks()) {
			// Last block, get from current code pointer.
			endOffset = (int)codeBlock_->GetOffset(codeBlock_->GetCodePtr());
		} else {
			endOffset = irBlocks_.GetBlock(blockNum + 1)->GetTargetOffset();
			_assert_msg_(endOffset >= blockOffset, "Next block not sequential, block=%d/%08x, next=%d/%08x", blockNum, blockOffset, blockNum + 1, endOffset);
		}
	}

	*startOffset = blockOffset;
	*size = endOffset - blockOffset;
}

JitBlockDebugInfo IRNativeBlockCacheDebugInterface::GetBlockDebugInfo(int blockNum) const {
	JitBlockDebugInfo debugInfo = irBlocks_.GetBlockDebugInfo(blockNum);

	int blockOffset, codeSize;
	GetBlockCodeRange(blockNum, &blockOffset, &codeSize);

	const u8 *blockStart = codeBlock_->GetBasePtr() + blockOffset;
#if PPSSPP_ARCH(ARM)
	debugInfo.targetDisasm = DisassembleArm2(blockStart, codeSize);
#elif PPSSPP_ARCH(ARM64)
	debugInfo.targetDisasm = DisassembleArm64(blockStart, codeSize);
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	debugInfo.targetDisasm = DisassembleX86(blockStart, codeSize);
#elif PPSSPP_ARCH(RISCV64)
	debugInfo.targetDisasm = DisassembleRV64(blockStart, codeSize);
#endif
	return debugInfo;
}

void IRNativeBlockCacheDebugInterface::ComputeStats(BlockCacheStats &bcStats) const {
	double totalBloat = 0.0;
	double maxBloat = 0.0;
	double minBloat = 1000000000.0;
	int numBlocks = GetNumBlocks();
	for (int i = 0; i < numBlocks; ++i) {
		const IRBlock &b = *irBlocks_.GetBlock(i);

		// Native size, not IR size.
		int blockOffset, codeSize;
		GetBlockCodeRange(i, &blockOffset, &codeSize);
		if (codeSize == 0)
			continue;

		// MIPS (PSP) size.
		u32 origAddr, origSize;
		b.GetRange(origAddr, origSize);

		double bloat = (double)codeSize / (double)origSize;
		if (bloat < minBloat) {
			minBloat = bloat;
			bcStats.minBloatBlock = origAddr;
		}
		if (bloat > maxBloat) {
			maxBloat = bloat;
			bcStats.maxBloatBlock = origAddr;
		}
		totalBloat += bloat;
		bcStats.bloatMap[(float)bloat] = origAddr;
	}
	bcStats.numBlocks = numBlocks;
	bcStats.minBloat = (float)minBloat;
	bcStats.maxBloat = (float)maxBloat;
	bcStats.avgBloat = (float)(totalBloat / (double)numBlocks);
}

} // namespace MIPSComp
