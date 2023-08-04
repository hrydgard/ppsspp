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
#include "Core/MIPS/IR/IRNativeCommon.h"

using namespace MIPSComp;

namespace MIPSComp {

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
	case IROp::FpCondToReg:
	case IROp::ZeroFpCond:
	case IROp::FpCtrlFromReg:
	case IROp::FpCtrlToReg:
	case IROp::VfpuCtrlToReg:
	case IROp::FMovFromGPR:
	case IROp::FMovToGPR:
		CompIR_Transfer(inst);
		break;

	case IROp::Vec4Init:
	case IROp::Vec4Shuffle:
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

} // namespace MIPSComp

IRNativeBlockCacheDebugInterface::IRNativeBlockCacheDebugInterface(IRBlockCache &irBlocks, CodeBlockCommon &codeBlock)
	: irBlocks_(irBlocks), codeBlock_(codeBlock) {}

int IRNativeBlockCacheDebugInterface::GetNumBlocks() const {
	return irBlocks_.GetNumBlocks();
}

int IRNativeBlockCacheDebugInterface::GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly) const {
	return irBlocks_.GetBlockNumberFromStartAddress(em_address, realBlocksOnly);
}

void IRNativeBlockCacheDebugInterface::GetBlockCodeRange(int blockNum, int *startOffset, int *size) const {
	int blockOffset = irBlocks_.GetBlock(blockNum)->GetTargetOffset();
	int endOffset;
	// We assume linear allocation.  Maybe a bit dangerous, should always be right.
	if (blockNum + 1 >= GetNumBlocks()) {
		// Last block, get from current code pointer.
		endOffset = (int)codeBlock_.GetOffset(codeBlock_.GetCodePtr());
	} else {
		endOffset = irBlocks_.GetBlock(blockNum + 1)->GetTargetOffset();
		_assert_msg_(endOffset >= blockOffset, "Next block not sequential, block=%d/%08x, next=%d/%08x", blockNum, blockOffset, blockNum + 1, endOffset);
	}

	*startOffset = blockOffset;
	*size = endOffset - blockOffset;
}

JitBlockDebugInfo IRNativeBlockCacheDebugInterface::GetBlockDebugInfo(int blockNum) const {
	JitBlockDebugInfo debugInfo = irBlocks_.GetBlockDebugInfo(blockNum);

	int blockOffset, codeSize;
	GetBlockCodeRange(blockNum, &blockOffset, &codeSize);

	// TODO: Normal entry?
	const u8 *blockStart = codeBlock_.GetBasePtr() + blockOffset;
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
