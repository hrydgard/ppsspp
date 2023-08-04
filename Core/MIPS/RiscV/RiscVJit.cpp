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
#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"
#include "Common/Profiler/Profiler.h"

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

static constexpr bool enableDebug = false;

static std::map<uint8_t, int> debugSeenNotCompiledIR;
static std::map<const char *, int> debugSeenNotCompiled;
double lastDebugLog = 0.0;

static void LogDebugNotCompiled() {
	if (!enableDebug)
		return;

	double now = time_now_d();
	if (now < lastDebugLog + 1.0)
		return;
	lastDebugLog = now;

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

RiscVJit::RiscVJit(MIPSState *mipsState)
	: IRNativeJit(mipsState), gpr(mipsState, &jo), fpr(mipsState, &jo), debugInterface_(blocks_, *this) {
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
	if constexpr (enableDebug) {
		LogDebugNotCompiled();
	}

	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher_)();
}

JitBlockCacheDebugInterface *MIPSComp::RiscVJit::GetBlockCacheDebugInterface() {
	return &debugInterface_;
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
	if (enableDebug) {
		QuickCallFunction(&NoBlockExits);
		QuickJ(R_RA, crashHandler_);
	}

	FlushIcache();

	return true;
}

static u32 DoIRInst(uint64_t value) {
	IRInst inst;
	memcpy(&inst, &value, sizeof(inst));

	if constexpr (enableDebug)
		debugSeenNotCompiledIR[(uint8_t)inst.op]++;

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

static void DebugInterpretHit(const char *name) {
	if (enableDebug)
		debugSeenNotCompiled[name]++;
}

void RiscVJit::CompIR_Interpret(IRInst inst) {
	MIPSOpcode op(inst.constant);

	// IR protects us against this being a branching instruction (well, hopefully.)
	FlushAll();
	SaveStaticRegisters();
	if (enableDebug) {
		LI(X10, MIPSGetName(op));
		QuickCallFunction(&DebugInterpretHit);
	}
	LI(X10, (int32_t)inst.constant);
	QuickCallFunction((const u8 *)MIPSGetInterpretFunc(op));
	LoadStaticRegisters();
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
	IRNativeJit::ClearCache();

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
