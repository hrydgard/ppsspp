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

#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"
#include "Common/Profiler/Profiler.h"

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

static constexpr bool enableDebug = false;

RiscVJit::RiscVJit(MIPSState *mipsState) : IRJit(mipsState), gpr(mipsState, &jo), fpr(mipsState, &jo) {
	// Automatically disable incompatible options.
	if (((intptr_t)Memory::base & 0x00000000FFFFFFFFUL) != 0) {
		jo.enablePointerify = false;
	}

	// Since we store the offset, this is as big as it can be.
	// We could shift off one bit to double it, would need to change RiscVAsm.
	AllocCodeSpace(1024 * 1024 * 16);
	SetAutoCompress(true);

	gpr.Init(this);
	fpr.Init(this);

	GenerateFixedCode(jo);
}

RiscVJit::~RiscVJit() {
}

void RiscVJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher_)();
}

static void NoBlockExits() {
	_assert_msg_(false, "Never exited block, invalid IR?");
}

bool RiscVJit::CompileTargetBlock(IRBlock *block, int block_num, bool preload) {
	if (GetSpaceLeft() < 0x800)
		return false;

	// Don't worry, the codespace isn't large enough to overflow offsets.
	block->SetTargetOffset((int)GetOffset(GetCodePointer()));

	// TODO: Block linking, checked entries and such.

	gpr.Start();
	fpr.Start();

	for (int i = 0; i < block->GetNumInstructions(); ++i) {
		const IRInst &inst = block->GetInstructions()[i];
		CompileIRInst(inst);

		if (jo.Disabled(JitDisable::REGALLOC_GPR)) {
			gpr.FlushAll();
		}
		if (jo.Disabled(JitDisable::REGALLOC_FPR)) {
			fpr.FlushAll();
		}

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800) {
			return false;
		}
	}

	// We should've written an exit above.  If we didn't, bad things will happen.
	if (enableDebug) {
		QuickCallFunction(&NoBlockExits);
		QuickJ(R_RA, crashHandler_);
	}

	FlushIcache();

	return true;
}

void RiscVJit::CompileIRInst(IRInst inst) {
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

static u32 DoIRInst(uint64_t value) {
	IRInst inst;
	memcpy(&inst, &value, sizeof(inst));

	return IRInterpret(currentMIPS, &inst, 1);
}

void RiscVJit::CompIR_Generic(IRInst inst) {
	// If we got here, we're going the slow way.
	uint64_t value;
	memcpy(&value, &inst, sizeof(inst));

	FlushAll();
	LI(X10, value, SCRATCH2);
	SaveStaticRegisters();
	QuickCallFunction(&DoIRInst);
	LoadStaticRegisters();

	// We only need to check the return value if it's a potential exit.
	if ((GetIRMeta(inst.op)->flags & IRFLAG_EXIT) != 0) {
		// Result in X10 aka SCRATCH1.
		_assert_(X10 == SCRATCH1);
		if (BInRange(dispatcherPCInSCRATCH1_)) {
			BNE(X10, R_ZERO, dispatcherPCInSCRATCH1_);
		} else {
			FixupBranch skip = BEQ(X10, R_ZERO);
			QuickJ(R_RA, dispatcherPCInSCRATCH1_);
			SetJumpTarget(skip);
		}
	}
}

void RiscVJit::FlushAll() {
	gpr.FlushAll();
	fpr.FlushAll();
}

bool RiscVJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in disassembly viewer.
	if (ptr == dispatcher_) {
		name = "dispatcher";
	} else if (ptr == dispatcherPCInSCRATCH1_) {
		name = "dispatcher (PC in SCRATCH1)";
	} else if (ptr == dispatcherNoCheck_) {
		name = "dispatcherNoCheck";
	} else if (ptr == saveStaticRegisters_) {
		name = "saveStaticRegisters";
	} else if (ptr == loadStaticRegisters_) {
		name = "loadStaticRegisters";
	} else if (ptr == enterDispatcher_) {
		name = "enterDispatcher";
	} else if (ptr == applyRoundingMode_) {
		name = "applyRoundingMode";
	} else if (!IsInSpace(ptr)) {
		return false;
	} else {
		int offset = (int)GetOffset(ptr);
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
	return true;
}

bool RiscVJit::CodeInRange(const u8 *ptr) const {
	return IsInSpace(ptr);
}

bool RiscVJit::IsAtDispatchFetch(const u8 *ptr) const {
	return ptr == dispatcherFetch_;
}

const u8 *RiscVJit::GetDispatcher() const {
	return dispatcher_;
}

const u8 *RiscVJit::GetCrashHandler() const {
	return crashHandler_;
}

void RiscVJit::ClearCache() {
	IRJit::ClearCache();

	ClearCodeSpace(jitStartOffset_);
	FlushIcacheSection(region + jitStartOffset_, region + region_size - jitStartOffset_);
}

void RiscVJit::RestoreRoundingMode(bool force) {
	FSRMI(Round::NEAREST_EVEN);
}

void RiscVJit::ApplyRoundingMode(bool force) {
	QuickCallFunction(applyRoundingMode_);
}

void RiscVJit::MovFromPC(RiscVReg r) {
	LWU(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJit::MovToPC(RiscVReg r) {
	SW(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJit::SaveStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(saveStaticRegisters_);
	} else {
		// Inline the single operation
		SW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void RiscVJit::LoadStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(loadStaticRegisters_);
	} else {
		LW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void RiscVJit::NormalizeSrc1(IRInst inst, RiscVReg *reg, RiscVReg tempReg, bool allowOverlap) {
	*reg = NormalizeR(inst.src1, allowOverlap ? 0 : inst.dest, tempReg);
}

void RiscVJit::NormalizeSrc12(IRInst inst, RiscVReg *lhs, RiscVReg *rhs, RiscVReg lhsTempReg, RiscVReg rhsTempReg, bool allowOverlap) {
	*lhs = NormalizeR(inst.src1, allowOverlap ? 0 : inst.dest, lhsTempReg);
	*rhs = NormalizeR(inst.src2, allowOverlap ? 0 : inst.dest, rhsTempReg);
}

RiscVReg RiscVJit::NormalizeR(IRRegIndex rs, IRRegIndex rd, RiscVReg tempReg) {
	// For proper compare, we must sign extend so they both match or don't match.
	// But don't change pointers, in case one is SP (happens in LittleBigPlanet.)
	if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0) {
		return R_ZERO;
	} else if (gpr.IsMappedAsPointer(rs) || rs == rd) {
		return gpr.Normalize32(rs, tempReg);
	} else {
		return gpr.Normalize32(rs);
	}
}

} // namespace MIPSComp
