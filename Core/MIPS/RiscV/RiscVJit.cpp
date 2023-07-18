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

static constexpr int MAX_ALLOWED_JIT_BLOCKS = 262144;

RiscVJit::RiscVJit(MIPSState *mipsState) : IRJit(mipsState) {
	// Automatically disable incompatible options.
	if (((intptr_t)Memory::base & 0x00000000FFFFFFFFUL) != 0) {
		jo.enablePointerify = false;
	}

	AllocCodeSpace(1024 * 1024 * 16);

	// TODO: Consider replacing block num method form IRJit - this is 2MB.
	blockStartAddrs_ = new const u8 *[MAX_ALLOWED_JIT_BLOCKS];

	// TODO: gpr, fpr

	GenerateFixedCode(jo);
}

RiscVJit::~RiscVJit() {
	delete [] blockStartAddrs_;
}

void RiscVJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher_)();
}

bool RiscVJit::CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) {
	// Check that we're not full (we allow less blocks than IR itself.)
	if (blocks_.GetNumBlocks() >= MAX_ALLOWED_JIT_BLOCKS - 1)
		return false;

	if (!IRJit::CompileBlock(em_address, instructions, mipsBytes, preload))
		return false;

	// TODO: Block linking, checked entries and such.

	int block_num = blocks_.GetBlockNumberFromStartAddress(em_address);
	_assert_msg_(blockStartAddrs_[block_num] == nullptr, "Block reused before clear");
	blockStartAddrs_[block_num] = GetCodePointer();

	// TODO: gpr, fpr.
	for (const IRInst &inst : instructions) {
		CompileIRInst(inst);

		// TODO
		if (jo.Disabled(JitDisable::REGALLOC_GPR)) {
			//gpr.FlushAll();
		}
		if (jo.Disabled(JitDisable::REGALLOC_FPR)) {
			//fpr.FlushAll();
		}

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800) {
			return false;
		}
	}

	// Note: a properly constructed block should never get here.
	// TODO: Need to do more than just this?  Call a func to set an exception?
	LI(SCRATCH2, crashHandler_);
	JALR(R_ZERO, SCRATCH2, 0);

	FlushIcache();

	return true;
}

static u32 DoIRInst(uint64_t value) {
	IRInst inst;
	memcpy(&inst, &value, sizeof(inst));

	return IRInterpret(currentMIPS, &inst, 1);
}

void RiscVJit::CompileIRInst(IRInst inst) {
	// For now, we're gonna do it the slow and ugly way.
	uint64_t value;
	memcpy(&value, &inst, sizeof(inst));

	LI(X10, value, SCRATCH2);
	SaveStaticRegisters();
	QuickCallFunction(SCRATCH2, &DoIRInst);
	LoadStaticRegisters();
	// Result in X10 aka SCRATCH1.
	_assert_(X10 == SCRATCH1);
	if (BInRange(dispatcherPCInSCRATCH1_)) {
		BNE(X10, R_ZERO, dispatcherPCInSCRATCH1_);
	} else {
		FixupBranch skip = BEQ(X10, R_ZERO);
		LI(SCRATCH2, dispatcherPCInSCRATCH1_);
		JALR(R_ZERO, SCRATCH2, 0);
		SetJumpTarget(skip);
	}
}

bool RiscVJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in disassembly viewer.
	if (ptr == dispatcher_) {
		name = "dispatcher";
	} else if (ptr == dispatcherPCInSCRATCH1_) {
		name = "dispatcher (PC in SCRATCH1)";
	} else if (ptr == dispatcherNoCheck_) {
		name = "dispatcherNoCheck";
	} else if (ptr == enterDispatcher_) {
		name = "enterDispatcher";
	} else if (!IsInSpace(ptr)) {
		return false;
	} else {
		uintptr_t uptr = (uintptr_t)ptr;
		int block_num = -1;
		for (int i = 0; i < MAX_ALLOWED_JIT_BLOCKS; ++i) {
			uintptr_t blockptr = (uintptr_t)blockStartAddrs_[i];
			// Out of allocated blocks.
			if (uptr == 0)
				break;

			if (uptr >= blockptr)
				block_num = i;
			if (uptr < blockptr)
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

	memset(blockStartAddrs_, 0, sizeof(blockStartAddrs_[0]) * MAX_ALLOWED_JIT_BLOCKS);
}

void RiscVJit::UpdateFCR31() {
	IRJit::UpdateFCR31();

	// TODO: Handle rounding modes?
}

void RiscVJit::RestoreRoundingMode(bool force) {
	// TODO: Could maybe skip if not hasSetRounding?  But that's on IRFrontend...
	FSRMI(Round::NEAREST_EVEN);
}

void RiscVJit::ApplyRoundingMode(bool force) {
	// TODO: Also could maybe sometimes skip?
	//QuickCallFunction(SCRATCH2, applyRoundingMode_);
}

void RiscVJit::MovFromPC(RiscVGen::RiscVReg r) {
	LWU(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJit::MovToPC(RiscVGen::RiscVReg r) {
	SW(r, CTXREG, offsetof(MIPSState, pc));
}

void RiscVJit::SaveStaticRegisters() {
	// TODO
	//if (jo.useStaticAlloc) {
	//	QuickCallFunction(SCRATCH2, saveStaticRegisters_);
	//} else {
		// Inline the single operation
		SW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	//}
}

void RiscVJit::LoadStaticRegisters() {
	// TODO
	//if (jo.useStaticAlloc) {
	//	QuickCallFunction(SCRATCH2, loadStaticRegisters_);
	//} else {
		LW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	//}
}

} // namespace MIPSComp
