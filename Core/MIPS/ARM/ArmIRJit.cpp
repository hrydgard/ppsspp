// Copyright (c) 2012- PPSSPP Project.

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
#if PPSSPP_ARCH(ARM)

#include <algorithm>
#include <cstddef>
#include <map>
#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

std::vector<std::string> DisassembleArm2(const u8 *data, int size);

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

// Invalidations need at most two MOVs and a B.
static constexpr int MIN_BLOCK_NORMAL_LEN = 12;
// As long as we can fit a B, we should be fine.
static constexpr int MIN_BLOCK_EXIT_LEN = 4;

ArmJitBackend::ArmJitBackend(JitOptions &jitopt, IRBlockCache &blocks)
	: IRNativeBackend(blocks), jo(jitopt), regs_(&jo) {
	// 32-bit addresses can't be pointerified, and there are too few registers for static allocation.
	jo.enablePointerify = false;
	jo.useStaticAlloc = false;
	jo.optimizeForInterpreter = false;

	// Since we store the offset, this is as big as it can be.
	AllocCodeSpace(1024 * 1024 * 16);

	regs_.Init(this);
}

ArmJitBackend::~ArmJitBackend() {}

static void NoBlockExits() {
	_assert_msg_(false, "Never exited block, invalid IR?");
}

bool ArmJitBackend::CompileBlock(IRBlockCache *irBlockCache, int block_num) {
	if (GetSpaceLeft() < 0x800)
		return false;

	IRBlock *block = irBlockCache->GetBlock(block_num);
	// An accurate dot product gathers its lanes and can flush a few registers around its call.
	size_t sizeEstimate = 0;
	const IRInst *estimateInsts = irBlockCache->GetBlockInstructionPtr(*block);
	for (int i = 0; i < block->GetNumIRInstructions(); ++i)
		sizeEstimate += estimateInsts[i].op == IROp::Vec4VfpuDot ? 96 : 32;
	BeginWrite(std::min(GetSpaceLeft(), sizeEstimate));

	u32 startPC = block->GetOriginalStart();
	bool wroteCheckedOffset = false;
	if (jo.enableBlocklink && !jo.useBackJump) {
		SetBlockCheckedOffset(block_num, (int)GetOffset(GetCodePointer()));
		wroteCheckedOffset = true;

		WriteDebugPC(startPC);

		CMP(DOWNCOUNTREG, Operand2(0));
		FixupBranch normalEntry = B_CC(CC_PL);
		MOVI2R(SCRATCH1, startPC);
		B(outerLoopPCInSCRATCH1_);
		SetJumpTarget(normalEntry);
	}

	// Don't worry, the codespace isn't large enough to overflow offsets.
	const u8 *blockStart = GetCodePointer();
	block->SetNativeOffset((int)GetOffset(blockStart));
	compilingBlockNum_ = block_num;
	lastConstPC_ = 0;

	regs_.Start(irBlockCache, block_num);

	std::vector<const u8 *> addresses;
	addresses.reserve(block->GetNumIRInstructions());
	const IRInst *instructions = irBlockCache->GetBlockInstructionPtr(*block);
	for (int i = 0; i < block->GetNumIRInstructions(); ++i) {
		const IRInst &inst = instructions[i];
		regs_.SetIRIndex(i);
		addresses.push_back(GetCodePtr());

		CompileIRInst(inst);

		if (jo.Disabled(JitDisable::REGALLOC_GPR) || jo.Disabled(JitDisable::REGALLOC_FPR))
			regs_.FlushAll(jo.Disabled(JitDisable::REGALLOC_GPR), jo.Disabled(JitDisable::REGALLOC_FPR));

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800) {
			compilingBlockNum_ = -1;
			return false;
		}
	}

	// We should've written an exit above.  If we didn't, bad things will happen.
	// Only check if debug stats are enabled - needlessly wastes jit space.
	if (DebugStatsEnabled()) {
		QuickCallFunction(R12, &NoBlockExits);
		B(hooks_.crashHandler);
	}

	int len = (int)GetOffset(GetCodePointer()) - block->GetNativeOffset();
	if (len < MIN_BLOCK_NORMAL_LEN) {
		// We need at least this much to invalidate blocks with.
		ReserveCodeSpace(MIN_BLOCK_NORMAL_LEN - len);
	}

	if (!wroteCheckedOffset) {
		// Always record this, even if block link disabled - it's used for size calc.
		SetBlockCheckedOffset(block_num, (int)GetOffset(GetCodePointer()));
	}

	if (jo.enableBlocklink && jo.useBackJump) {
		WriteDebugPC(startPC);

		CMP(DOWNCOUNTREG, Operand2(0));
		B_CC(CC_PL, blockStart);

		MOVI2R(SCRATCH1, startPC);
		B(outerLoopPCInSCRATCH1_);
	}

	RecordIRAddresses(block_num, addresses);

	if (logBlocks_ > 0) {
		--logBlocks_;

		std::map<const u8 *, int> addressesLookup;
		for (int i = 0; i < (int)addresses.size(); ++i)
			addressesLookup[addresses[i]] = i;

		INFO_LOG(Log::JIT, "=============== ARM (%08x, %d bytes) ===============", startPC, len);
		for (const u8 *p = blockStart; p < GetCodePointer(); ) {
			auto it = addressesLookup.find(p);
			if (it != addressesLookup.end()) {
				const IRInst &inst = instructions[it->second];

				char temp[512];
				DisassembleIR(temp, sizeof(temp), inst);
				INFO_LOG(Log::JIT, "IR: #%d %s", it->second, temp);
			}

			auto next = std::next(it);
			const u8 *nextp = next == addressesLookup.end() ? GetCodePointer() : next->first;

			auto lines = DisassembleArm2(p, (int)(nextp - p));
			for (const auto &line : lines)
				INFO_LOG(Log::JIT, " A: %s", line.c_str());
			p = nextp;
		}
	}

	EndWrite();
	FlushIcache();
	compilingBlockNum_ = -1;

	return true;
}

void ArmJitBackend::WriteConstExit(uint32_t pc) {
	int block_num = blocks_.GetBlockNumberFromStartAddress(pc);
	const IRNativeBlock *nativeBlock = GetNativeBlock(block_num);

	int exitStart = (int)GetOffset(GetCodePointer());
	if (block_num >= 0 && jo.enableBlocklink && nativeBlock && nativeBlock->checkedOffset != 0) {
		B(GetBasePtr() + nativeBlock->checkedOffset);
	} else {
		MOVI2R(SCRATCH1, pc);
		B(dispatcherPCInSCRATCH1_);
	}

	if (jo.enableBlocklink) {
		// In case of compression or early link, make sure it's large enough.
		int len = (int)GetOffset(GetCodePointer()) - exitStart;
		if (len < MIN_BLOCK_EXIT_LEN) {
			ReserveCodeSpace(MIN_BLOCK_EXIT_LEN - len);
			len = MIN_BLOCK_EXIT_LEN;
		}

		AddLinkableExit(compilingBlockNum_, pc, exitStart, len);
	}
}

void ArmJitBackend::OverwriteExit(int srcOffset, int len, int block_num) {
	_dbg_assert_(len >= MIN_BLOCK_EXIT_LEN);

	const IRNativeBlock *nativeBlock = GetNativeBlock(block_num);
	if (nativeBlock) {
		u8 *writable = GetWritablePtrFromCodePtr(GetBasePtr()) + srcOffset;
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, len, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		ARMXEmitter emitter(writable);
		emitter.B(GetBasePtr() + nativeBlock->checkedOffset);
		int bytesWritten = (int)(emitter.GetWritableCodePtr() - writable);
		_dbg_assert_(bytesWritten <= MIN_BLOCK_EXIT_LEN);
		if (bytesWritten < len)
			emitter.ReserveCodeSpace(len - bytesWritten);
		emitter.FlushIcache();

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, 16, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}
}

void ArmJitBackend::CompIR_Generic(IRInst inst) {
	// If we got here, we're going the slow way.
	uint64_t value;
	memcpy(&value, &inst, sizeof(inst));

	FlushAll();
	SaveStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IR_INTERPRET);
	// The 64-bit argument goes in R0:R1.
	MOVI2R(R0, (u32)value);
	MOVI2R(R1, (u32)(value >> 32));
	QuickCallFunction(R12, &DoIRInst);
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	LoadStaticRegisters();

	// We only need to check the return value if it's a potential exit.
	if ((GetIRMeta(inst.op)->flags & IRFLAG_EXIT) != 0) {
		// The PC is already in R0 (SCRATCH1.)
		CMP(R0, Operand2(0));
		B_CC(CC_NEQ, dispatcherPCInSCRATCH1_);
	}
}

void ArmJitBackend::CompIR_Interpret(IRInst inst) {
	MIPSOpcode op(inst.constant);

	// IR protects us against this being a branching instruction (well, hopefully.)
	FlushAll();
	SaveStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::INTERPRET);
	if (DebugStatsEnabled()) {
		MOVP2R(R0, MIPSGetName(op));
		QuickCallFunction(R12, &NotifyMIPSInterpret);
	}
	MOVI2R(R0, inst.constant);
	QuickCallFunction(R12, &MIPSInterpretTrampoline);
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	LoadStaticRegisters();
}

void ArmJitBackend::FlushAll() {
	regs_.FlushAll();
}

bool ArmJitBackend::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	// Used in disassembly viewer and profiling tools.
	// Don't use spaces; profilers get confused or truncate them.
	if (ptr == dispatcherPCInSCRATCH1_) {
		name = "dispatcherPCInSCRATCH1";
	} else if (ptr == outerLoopPCInSCRATCH1_) {
		name = "outerLoopPCInSCRATCH1";
	} else if (ptr == dispatcherNoCheck_) {
		name = "dispatcherNoCheck";
	} else if (ptr == restoreRoundingMode_) {
		name = "restoreRoundingMode";
	} else if (ptr == applyRoundingMode_) {
		name = "applyRoundingMode";
	} else if (ptr >= GetBasePtr() && ptr < GetBasePtr() + jitStartOffset_) {
		name = DescribeFixedCode(ptr);
	} else {
		return IRNativeBackend::DescribeCodePtr(ptr, name);
	}
	return true;
}

void ArmJitBackend::ClearAllBlocks() {
	ClearCodeSpace(jitStartOffset_);
	FlushIcacheSection(region + jitStartOffset_, region + region_size - jitStartOffset_);
	EraseAllLinks(-1);
}

void ArmJitBackend::InvalidateBlock(IRBlockCache *irBlockCache, int block_num) {
	IRBlock *block = irBlockCache->GetBlock(block_num);
	int offset = block->GetNativeOffset();
	u8 *writable = GetWritablePtrFromCodePtr(GetBasePtr()) + offset;

	// Overwrite the block with a jump to compile it again.
	u32 pc = block->GetOriginalStart();
	if (pc != 0) {
		// Hopefully we always have at least 12 bytes, which should be all we need.
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, MIN_BLOCK_NORMAL_LEN, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		ARMXEmitter emitter(writable);
		emitter.MOVI2R(SCRATCH1, pc);
		emitter.B(dispatcherPCInSCRATCH1_);
		int bytesWritten = (int)(emitter.GetWritableCodePtr() - writable);
		if (bytesWritten < MIN_BLOCK_NORMAL_LEN)
			emitter.ReserveCodeSpace(MIN_BLOCK_NORMAL_LEN - bytesWritten);
		emitter.FlushIcache();

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, MIN_BLOCK_NORMAL_LEN, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}

	EraseAllLinks(block_num);
}

void ArmJitBackend::RestoreRoundingMode(bool force) {
	QuickCallFunction(R12, restoreRoundingMode_);
}

void ArmJitBackend::ApplyRoundingMode(bool force) {
	QuickCallFunction(R12, applyRoundingMode_);
}

void ArmJitBackend::UpdateRoundingMode(bool force) {
	// Conversions read the mode from FPSCR, so there's nothing to cache.
}

void ArmJitBackend::MovFromPC(ARMReg r) {
	LDR(r, CTXREG, offsetof(MIPSState, pc));
}

void ArmJitBackend::MovToPC(ARMReg r) {
	STR(r, CTXREG, offsetof(MIPSState, pc));
}

void ArmJitBackend::WriteDebugPC(uint32_t pc) {
	if (hooks_.profilerPC) {
		MOVI2R(R12, pc);
		WriteDebugPC(R12);
	}
}

void ArmJitBackend::WriteDebugPC(ARMReg r) {
	if (hooks_.profilerPC) {
		_dbg_assert_(r != SCRATCH2);
		MOVP2R(SCRATCH2, hooks_.profilerPC);
		STR(r, SCRATCH2, 0);
	}
}

void ArmJitBackend::WriteDebugProfilerStatus(IRProfilerStatus status) {
	if (hooks_.profilerPC) {
		MOVP2R(SCRATCH2, hooks_.profilerStatus);
		MOVI2R(R12, (int)status);
		STR(R12, SCRATCH2, 0);
	}
}

void ArmJitBackend::SaveStaticRegisters() {
	STR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void ArmJitBackend::LoadStaticRegisters() {
	LDR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

} // namespace MIPSComp

#endif
