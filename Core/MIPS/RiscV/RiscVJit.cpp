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

#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

RiscVJitBackend::RiscVJitBackend(MIPSState *mipsState, JitOptions &jitopt)
	: jo(jitopt), gpr(mipsState, &jo), fpr(mipsState, &jo) {
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
}

RiscVJitBackend::~RiscVJitBackend() {
}

static void NoBlockExits() {
	_assert_msg_(false, "Never exited block, invalid IR?");
}

bool RiscVJitBackend::CompileBlock(IRBlock *block, int block_num, bool preload) {
	if (GetSpaceLeft() < 0x800)
		return false;

	// Don't worry, the codespace isn't large enough to overflow offsets.
	block->SetTargetOffset((int)GetOffset(GetCodePointer()));

	// TODO: Block linking, checked entries and such.

	gpr.Start(block);
	fpr.Start(block);

	for (int i = 0; i < block->GetNumInstructions(); ++i) {
		const IRInst &inst = block->GetInstructions()[i];
		gpr.SetIRIndex(i);
		fpr.SetIRIndex(i);

		CompileIRInst(inst);

		if (jo.Disabled(JitDisable::REGALLOC_GPR))
			gpr.FlushAll();
		if (jo.Disabled(JitDisable::REGALLOC_FPR))
			fpr.FlushAll();

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800) {
			return false;
		}
	}

	// We should've written an exit above.  If we didn't, bad things will happen.
	// Only check if debug stats are enabled - needlessly wastes jit space.
	if (DebugStatsEnabled()) {
		QuickCallFunction(&NoBlockExits, SCRATCH2);
		QuickJ(R_RA, hooks_.crashHandler);
	}

	int len = (int)GetOffset(GetCodePointer()) - block->GetTargetOffset();
	if (len < 16) {
		// We need at least 16 bytes to invalidate blocks with, but larger doesn't need to align.
		AlignCode16();
	}

	FlushIcache();

	return true;
}

void RiscVJitBackend::FinalizeBlock(IRBlock *block, int block_num) {
	// TODO
}

void RiscVJitBackend::CompIR_Generic(IRInst inst) {
	// If we got here, we're going the slow way.
	uint64_t value;
	memcpy(&value, &inst, sizeof(inst));

	FlushAll();
	LI(X10, value, SCRATCH2);
	SaveStaticRegisters();
	QuickCallFunction(&DoIRInst, SCRATCH2);
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

void RiscVJitBackend::CompIR_Interpret(IRInst inst) {
	MIPSOpcode op(inst.constant);

	// IR protects us against this being a branching instruction (well, hopefully.)
	FlushAll();
	SaveStaticRegisters();
	if (DebugStatsEnabled()) {
		LI(X10, MIPSGetName(op));
		QuickCallFunction(&NotifyMIPSInterpret, SCRATCH2);
	}
	LI(X10, (int32_t)inst.constant);
	QuickCallFunction((const u8 *)MIPSGetInterpretFunc(op), SCRATCH2);
	LoadStaticRegisters();
}

void RiscVJitBackend::FlushAll() {
	gpr.FlushAll();
	fpr.FlushAll();
}

bool RiscVJitBackend::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	// Used in disassembly viewer.
	if (ptr == dispatcherPCInSCRATCH1_) {
		name = "dispatcher (PC in SCRATCH1)";
	} else if (ptr == dispatcherNoCheck_) {
		name = "dispatcherNoCheck";
	} else if (ptr == saveStaticRegisters_) {
		name = "saveStaticRegisters";
	} else if (ptr == loadStaticRegisters_) {
		name = "loadStaticRegisters";
	} else if (ptr == applyRoundingMode_) {
		name = "applyRoundingMode";
	} else {
		return IRNativeBackend::DescribeCodePtr(ptr, name);
	}
	return true;
}

void RiscVJitBackend::ClearAllBlocks() {
	ClearCodeSpace(jitStartOffset_);
	FlushIcacheSection(region + jitStartOffset_, region + region_size - jitStartOffset_);
}

void RiscVJitBackend::InvalidateBlock(IRBlock *block, int block_num) {
	int offset = block->GetTargetOffset();
	u8 *writable = GetWritablePtrFromCodePtr(GetBasePtr()) + offset;

	// Overwrite the block with a jump to compile it again.
	u32 pc = block->GetOriginalStart();
	if (pc != 0) {
		// Hopefully we always have at least 16 bytes, which should be all we need.
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, 16, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		RiscVEmitter emitter(GetBasePtr() + offset, writable);
		// We sign extend to ensure it will fit in 32-bit and 8 bytes LI.
		// TODO: Would need to change if dispatcher doesn't reload PC.
		emitter.LI(SCRATCH1, (int32_t)pc);
		emitter.J(dispatcherPCInSCRATCH1_);
		emitter.FlushIcache();

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, 16, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}
}

void RiscVJitBackend::RestoreRoundingMode(bool force) {
	FSRMI(Round::NEAREST_EVEN);
}

void RiscVJitBackend::ApplyRoundingMode(bool force) {
	QuickCallFunction(applyRoundingMode_);
}

void RiscVJitBackend::MovFromPC(RiscVReg r) {
	LWU(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJitBackend::MovToPC(RiscVReg r) {
	SW(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJitBackend::SaveStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(saveStaticRegisters_);
	} else {
		// Inline the single operation
		SW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void RiscVJitBackend::LoadStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(loadStaticRegisters_);
	} else {
		LW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void RiscVJitBackend::NormalizeSrc1(IRInst inst, RiscVReg *reg, RiscVReg tempReg, bool allowOverlap) {
	*reg = NormalizeR(inst.src1, allowOverlap ? 0 : inst.dest, tempReg);
}

void RiscVJitBackend::NormalizeSrc12(IRInst inst, RiscVReg *lhs, RiscVReg *rhs, RiscVReg lhsTempReg, RiscVReg rhsTempReg, bool allowOverlap) {
	*lhs = NormalizeR(inst.src1, allowOverlap ? 0 : inst.dest, lhsTempReg);
	*rhs = NormalizeR(inst.src2, allowOverlap ? 0 : inst.dest, rhsTempReg);
}

RiscVReg RiscVJitBackend::NormalizeR(IRRegIndex rs, IRRegIndex rd, RiscVReg tempReg) {
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
