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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <cstddef>
#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

namespace MIPSComp {

using namespace Gen;
using namespace X64IRJitConstants;

// Invalidations just need a MOV and JMP.
static constexpr int MIN_BLOCK_NORMAL_LEN = 10;
// As long as we can fit a JMP, we should be fine.
static constexpr int MIN_BLOCK_EXIT_LEN = 5;

X64JitBackend::X64JitBackend(JitOptions &jitopt, IRBlockCache &blocks)
	: IRNativeBackend(blocks), jo(jitopt), regs_(&jo) {
	// Automatically disable incompatible options.
	if (((intptr_t)Memory::base & 0x00000000FFFFFFFFUL) != 0) {
		jo.enablePointerify = false;
	}
	jo.optimizeForInterpreter = false;

	// Since we store the offset, this is as big as it can be.
	AllocCodeSpace(1024 * 1024 * 16);

	regs_.Init(this);
}

X64JitBackend::~X64JitBackend() {}

static void NoBlockExits() {
	_assert_msg_(false, "Never exited block, invalid IR?");
}

bool X64JitBackend::CompileBlock(IRBlockCache *irBlockCache, int block_num, bool preload) {
	if (GetSpaceLeft() < 0x800)
		return false;

	IRBlock *block = irBlockCache->GetBlock(block_num);
	u32 startPC = block->GetOriginalStart();
	bool wroteCheckedOffset = false;
	if (jo.enableBlocklink && !jo.useBackJump) {
		SetBlockCheckedOffset(block_num, (int)GetOffset(GetCodePointer()));
		wroteCheckedOffset = true;

		WriteDebugPC(startPC);

		// TODO: See if we can get flags to always have the downcount compare.
		if (jo.downcountInRegister) {
			TEST(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG));
		} else {
			CMP(32, MDisp(CTXREG, downcountOffset), Imm32(0));
		}
		FixupBranch normalEntry = J_CC(CC_NS);
		MOV(32, R(SCRATCH1), Imm32(startPC));
		JMP(outerLoopPCInSCRATCH1_, true);
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
		ABI_CallFunction((const void *)&NoBlockExits);
		JMP(hooks_.crashHandler, true);
	}

	int len = (int)GetOffset(GetCodePointer()) - block->GetNativeOffset();
	if (len < MIN_BLOCK_NORMAL_LEN) {
		// We need at least 10 bytes to invalidate blocks with.
		ReserveCodeSpace(MIN_BLOCK_NORMAL_LEN - len);
	}

	if (!wroteCheckedOffset) {
		// Always record this, even if block link disabled - it's used for size calc.
		SetBlockCheckedOffset(block_num, (int)GetOffset(GetCodePointer()));
	}

	if (jo.enableBlocklink && jo.useBackJump) {
		WriteDebugPC(startPC);

		if (jo.downcountInRegister) {
			TEST(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG));
		} else {
			CMP(32, MDisp(CTXREG, downcountOffset), Imm32(0));
		}
		J_CC(CC_NS, blockStart, true);

		MOV(32, R(SCRATCH1), Imm32(startPC));
		JMP(outerLoopPCInSCRATCH1_, true);
	}

	if (logBlocks_ > 0) {
		--logBlocks_;

		std::map<const u8 *, int> addressesLookup;
		for (int i = 0; i < (int)addresses.size(); ++i)
			addressesLookup[addresses[i]] = i;

		INFO_LOG(Log::JIT, "=============== x86 (%08x, %d bytes) ===============", startPC, len);
		const IRInst *instructions = irBlockCache->GetBlockInstructionPtr(*block);
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

			auto lines = DisassembleX86(p, (int)(nextp - p));
			for (const auto &line : lines)
				INFO_LOG(Log::JIT, " X: %s", line.c_str());
			p = nextp;
		}
	}

	compilingBlockNum_ = -1;

	return true;
}

void X64JitBackend::WriteConstExit(uint32_t pc) {
	int block_num = blocks_.GetBlockNumberFromStartAddress(pc);
	const IRNativeBlock *nativeBlock = GetNativeBlock(block_num);

	int exitStart = (int)GetOffset(GetCodePointer());
	if (block_num >= 0 && jo.enableBlocklink && nativeBlock && nativeBlock->checkedOffset != 0) {
		JMP(GetBasePtr() + nativeBlock->checkedOffset, true);
	} else {
		MOV(32, R(SCRATCH1), Imm32(pc));
		JMP(dispatcherPCInSCRATCH1_, true);
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

void X64JitBackend::OverwriteExit(int srcOffset, int len, int block_num) {
	_dbg_assert_(len >= MIN_BLOCK_EXIT_LEN);

	const IRNativeBlock *nativeBlock = GetNativeBlock(block_num);
	if (nativeBlock) {
		u8 *writable = GetWritablePtrFromCodePtr(GetBasePtr()) + srcOffset;
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, len, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		XEmitter emitter(writable);
		emitter.JMP(GetBasePtr() + nativeBlock->checkedOffset, true);
		int bytesWritten = (int)(emitter.GetWritableCodePtr() - writable);
		_dbg_assert_(bytesWritten <= MIN_BLOCK_EXIT_LEN);
		if (bytesWritten < len)
			emitter.ReserveCodeSpace(len - bytesWritten);

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, 16, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}
}

void X64JitBackend::CompIR_Generic(IRInst inst) {
	// If we got here, we're going the slow way.
	uint64_t value;
	memcpy(&value, &inst, sizeof(inst));

	FlushAll();
	SaveStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IR_INTERPRET);
#if PPSSPP_ARCH(AMD64)
	ABI_CallFunctionP((const void *)&DoIRInst, (void *)value);
#else
	ABI_CallFunctionCC((const void *)&DoIRInst, (u32)(value & 0xFFFFFFFF), (u32)(value >> 32));
#endif
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	LoadStaticRegisters();

	// We only need to check the return value if it's a potential exit.
	if ((GetIRMeta(inst.op)->flags & IRFLAG_EXIT) != 0) {
		// Result in RAX aka SCRATCH1.
		_assert_(RAX == SCRATCH1);
		CMP(32, R(SCRATCH1), Imm32(0));
		J_CC(CC_NE, dispatcherPCInSCRATCH1_);
	}
}

void X64JitBackend::CompIR_Interpret(IRInst inst) {
	MIPSOpcode op(inst.constant);

	// IR protects us against this being a branching instruction (well, hopefully.)
	FlushAll();
	SaveStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::INTERPRET);
	if (DebugStatsEnabled()) {
		ABI_CallFunctionP((const void *)&NotifyMIPSInterpret, (void *)MIPSGetName(op));
	}
	ABI_CallFunctionC((const void *)MIPSGetInterpretFunc(op), inst.constant);
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	LoadStaticRegisters();
}

void X64JitBackend::FlushAll() {
	regs_.FlushAll();
}

bool X64JitBackend::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	// Used in disassembly viewer and profiling tools.
	// Don't use spaces; profilers get confused or truncate them.
	if (ptr == dispatcherPCInSCRATCH1_) {
		name = "dispatcherPCInSCRATCH1";
	} else if (ptr == outerLoopPCInSCRATCH1_) {
		name = "outerLoopPCInSCRATCH1";
	} else if (ptr == dispatcherNoCheck_) {
		name = "dispatcherNoCheck";
	} else if (ptr == saveStaticRegisters_) {
		name = "saveStaticRegisters";
	} else if (ptr == loadStaticRegisters_) {
		name = "loadStaticRegisters";
	} else if (ptr == restoreRoundingMode_) {
		name = "restoreRoundingMode";
	} else if (ptr == applyRoundingMode_) {
		name = "applyRoundingMode";
	} else if (ptr >= GetBasePtr() && ptr < GetBasePtr() + jitStartOffset_) {
		if (ptr == constants.noSignMask) {
			name = "constants.noSignMask";
		} else if (ptr == constants.signBitAll) {
			name = "constants.signBitAll";
		} else if (ptr == constants.positiveZeroes) {
			name = "constants.positiveZeroes";
		} else if (ptr == constants.positiveInfinity) {
			name = "constants.positiveInfinity";
		} else if (ptr == constants.positiveOnes) {
			name = "constants.positiveOnes";
		} else if (ptr == constants.negativeOnes) {
			name = "constants.negativeOnes";
		} else if (ptr == constants.qNAN) {
			name = "constants.qNAN";
		} else if (ptr == constants.maxIntBelowAsFloat) {
			name = "constants.maxIntBelowAsFloat";
		} else if ((const float *)ptr >= constants.mulTableVi2f && (const float *)ptr < constants.mulTableVi2f + 32) {
			name = StringFromFormat("constants.mulTableVi2f[%d]", (int)((const float *)ptr - constants.mulTableVi2f));
		} else if ((const float *)ptr >= constants.mulTableVf2i && (const float *)ptr < constants.mulTableVf2i + 32) {
			name = StringFromFormat("constants.mulTableVf2i[%d]", (int)((const float *)ptr - constants.mulTableVf2i));
		} else if ((const Float4Constant *)ptr >= constants.vec4InitValues && (const Float4Constant *)ptr < constants.vec4InitValues + 8) {
			name = StringFromFormat("constants.vec4InitValues[%d]", (int)((const Float4Constant *)ptr - constants.vec4InitValues));
		} else {
			name = "fixedCode";
		}
	} else {
		return IRNativeBackend::DescribeCodePtr(ptr, name);
	}
	return true;
}

void X64JitBackend::ClearAllBlocks() {
	ClearCodeSpace(jitStartOffset_);
	EraseAllLinks(-1);
}

void X64JitBackend::InvalidateBlock(IRBlockCache *irBlockCache, int block_num) {
	IRBlock *block = irBlockCache->GetBlock(block_num);
	int offset = block->GetNativeOffset();
	u8 *writable = GetWritablePtrFromCodePtr(GetBasePtr()) + offset;

	// Overwrite the block with a jump to compile it again.
	u32 pc = block->GetOriginalStart();
	if (pc != 0) {
		// Hopefully we always have at least 16 bytes, which should be all we need.
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, MIN_BLOCK_NORMAL_LEN, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		XEmitter emitter(writable);
		emitter.MOV(32, R(SCRATCH1), Imm32(pc));
		emitter.JMP(dispatcherPCInSCRATCH1_, true);
		int bytesWritten = (int)(emitter.GetWritableCodePtr() - writable);
		if (bytesWritten < MIN_BLOCK_NORMAL_LEN)
			emitter.ReserveCodeSpace(MIN_BLOCK_NORMAL_LEN - bytesWritten);

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(writable, MIN_BLOCK_NORMAL_LEN, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}

	EraseAllLinks(block_num);
}

void X64JitBackend::RestoreRoundingMode(bool force) {
	CALL(restoreRoundingMode_);
}

void X64JitBackend::ApplyRoundingMode(bool force) {
	CALL(applyRoundingMode_);
}

void X64JitBackend::MovFromPC(X64Reg r) {
	MOV(32, R(r), MDisp(CTXREG, pcOffset));
}

void X64JitBackend::MovToPC(X64Reg r) {
	MOV(32, MDisp(CTXREG, pcOffset), R(r));
}

void X64JitBackend::WriteDebugPC(uint32_t pc) {
	if (hooks_.profilerPC)
		MOV(32, M(hooks_.profilerPC), Imm32(pc));
}

void X64JitBackend::WriteDebugPC(Gen::X64Reg r) {
	if (hooks_.profilerPC)
		MOV(32, M(hooks_.profilerPC), R(r));
}

void X64JitBackend::WriteDebugProfilerStatus(IRProfilerStatus status) {
	if (hooks_.profilerPC)
		MOV(32, M(hooks_.profilerStatus), Imm32((int32_t)status));
}

void X64JitBackend::SaveStaticRegisters() {
	if (jo.useStaticAlloc) {
		//CALL(saveStaticRegisters_);
	} else if (jo.downcountInRegister) {
		// Inline the single operation
		MOV(32, MDisp(CTXREG, downcountOffset), R(DOWNCOUNTREG));
	}
}

void X64JitBackend::LoadStaticRegisters() {
	if (jo.useStaticAlloc) {
		//CALL(loadStaticRegisters_);
	} else if (jo.downcountInRegister) {
		MOV(32, R(DOWNCOUNTREG), MDisp(CTXREG, downcountOffset));
	}
}

void X64JitBackend::EmitConst4x32(const void **c, uint32_t v) {
	*c = AlignCode16();
	for (int i = 0; i < 4; ++i)
		Write32(v);
}

} // namespace MIPSComp

#endif
