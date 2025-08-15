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
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include "Core/MemMap.h"
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for load/store instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Arm64Gen;
using namespace Arm64IRJitConstants;

static int IROpToByteWidth(IROp op) {
	switch (op) {
	case IROp::Load8:
	case IROp::Load8Ext:
	case IROp::Store8:
		return 1;

	case IROp::Load16:
	case IROp::Load16Ext:
	case IROp::Store16:
		return 2;

	case IROp::Load32:
	case IROp::Load32Linked:
	case IROp::Load32Left:
	case IROp::Load32Right:
	case IROp::LoadFloat:
	case IROp::Store32:
	case IROp::Store32Conditional:
	case IROp::Store32Left:
	case IROp::Store32Right:
	case IROp::StoreFloat:
		return 4;

	case IROp::LoadVec4:
	case IROp::StoreVec4:
		return 16;

	default:
		_assert_msg_(false, "Unexpected op: %s", GetIRMeta(op) ? GetIRMeta(op)->name : "?");
		return -1;
	}
}

Arm64JitBackend::LoadStoreArg Arm64JitBackend::PrepareSrc1Address(IRInst inst) {
	const IRMeta *m = GetIRMeta(inst.op);

	bool src1IsPointer = regs_.IsGPRMappedAsPointer(inst.src1);
	bool readsFromSrc1 = inst.src1 == inst.src3 && (m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0;
	// If it's about to be clobbered, don't waste time pointerifying.  Use displacement.
	bool clobbersSrc1 = !readsFromSrc1 && regs_.IsGPRClobbered(inst.src1);

	int64_t imm = (int32_t)inst.constant;
	// It can't be this negative, must be a constant address with the top bit set.
	if ((imm & 0xC0000000) == 0x80000000) {
		imm = (uint64_t)(uint32_t)inst.constant;
	}

	LoadStoreArg addrArg;
	if (inst.src1 == MIPS_REG_ZERO) {
		// The constant gets applied later.
		addrArg.base = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		imm &= Memory::MEMVIEW32_MASK;
#endif
	} else if (!jo.enablePointerify && readsFromSrc1) {
#ifndef MASKED_PSP_MEMORY
		if (imm == 0) {
			addrArg.base = MEMBASEREG;
			addrArg.regOffset = regs_.MapGPR(inst.src1);
			addrArg.useRegisterOffset = true;
			addrArg.signExtendRegOffset = false;
		}
#endif

		// Since we can't modify src1, let's just use a temp reg while copying.
		if (!addrArg.useRegisterOffset) {
			ADDI2R(SCRATCH1, regs_.MapGPR(inst.src1), imm, SCRATCH2);
#ifdef MASKED_PSP_MEMORY
			ANDI2R(SCRATCH1, SCRATCH1, Memory::MEMVIEW32_MASK, SCRATCH2);
#endif

			addrArg.base = MEMBASEREG;
			addrArg.regOffset = SCRATCH1;
			addrArg.useRegisterOffset = true;
			addrArg.signExtendRegOffset = false;
		}
	} else if ((jo.cachePointers && !clobbersSrc1) || src1IsPointer) {
		// The offset gets set later.
		addrArg.base = regs_.MapGPRAsPointer(inst.src1);
	} else {
		ADDI2R(SCRATCH1, regs_.MapGPR(inst.src1), imm, SCRATCH2);
#ifdef MASKED_PSP_MEMORY
		ANDI2R(SCRATCH1, SCRATCH1, Memory::MEMVIEW32_MASK, SCRATCH2);
#endif

		addrArg.base = MEMBASEREG;
		addrArg.regOffset = SCRATCH1;
		addrArg.useRegisterOffset = true;
		addrArg.signExtendRegOffset = false;
	}

	// That's src1 taken care of, and possibly imm.
	// If useRegisterOffset is false, imm still needs to be accounted for.
	if (!addrArg.useRegisterOffset && imm != 0) {
#ifdef MASKED_PSP_MEMORY
		// In case we have an address + offset reg.
		if (imm > 0)
			imm &= Memory::MEMVIEW32_MASK;
#endif

		int scale = IROpToByteWidth(inst.op);
		if (imm > 0 && (imm & (scale - 1)) == 0 && imm <= 0xFFF * scale) {
			// Okay great, use the LDR/STR form.
			addrArg.immOffset = (int)imm;
			addrArg.useUnscaled = false;
		} else if (imm >= -256 && imm < 256) {
			// An unscaled offset (LDUR/STUR) should work fine for this range.
			addrArg.immOffset = (int)imm;
			addrArg.useUnscaled = true;
		} else {
			// No luck, we'll need to load into a register.
			MOVI2R(SCRATCH1, imm);
			addrArg.regOffset = SCRATCH1;
			addrArg.useRegisterOffset = true;
			addrArg.signExtendRegOffset = true;
		}
	}

	return addrArg;
}

void Arm64JitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (inst.op != IROp::Store32Conditional)
		INVALIDOP;

	regs_.SpillLockGPR(IRREG_LLBIT, inst.src3, inst.src1);
	LoadStoreArg addrArg = PrepareSrc1Address(inst);
	ARM64Reg valueReg = regs_.MapGPR(inst.src3, MIPSMap::INIT);

	regs_.MapGPR(IRREG_LLBIT, MIPSMap::INIT);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	FixupBranch condFailed = CBZ(regs_.R(IRREG_LLBIT));

	if (addrArg.useRegisterOffset) {
		STR(valueReg, addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
	} else if (addrArg.useUnscaled) {
		STUR(valueReg, addrArg.base, addrArg.immOffset);
	} else {
		STR(INDEX_UNSIGNED, valueReg, addrArg.base, addrArg.immOffset);
	}

	if (inst.dest != MIPS_REG_ZERO) {
		MOVI2R(regs_.R(inst.dest), 1);
		FixupBranch finish = B();

		SetJumpTarget(condFailed);
		MOVI2R(regs_.R(inst.dest), 0);
		SetJumpTarget(finish);
	} else {
		SetJumpTarget(condFailed);
	}
}

void Arm64JitBackend::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoadStoreArg addrArg = PrepareSrc1Address(inst);

	switch (inst.op) {
	case IROp::LoadFloat:
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		if (addrArg.useRegisterOffset) {
			fp_.LDR(32, regs_.F(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			fp_.LDUR(32, regs_.F(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			fp_.LDR(32, INDEX_UNSIGNED, regs_.F(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoadStoreArg addrArg = PrepareSrc1Address(inst);

	switch (inst.op) {
	case IROp::StoreFloat:
		regs_.MapFPR(inst.src3);
		if (addrArg.useRegisterOffset) {
			fp_.STR(32, regs_.F(inst.src3), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			fp_.STUR(32, regs_.F(inst.src3), addrArg.base, addrArg.immOffset);
		} else {
			fp_.STR(32, INDEX_UNSIGNED, regs_.F(inst.src3), addrArg.base, addrArg.immOffset);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.dest, inst.src1);
	LoadStoreArg addrArg = PrepareSrc1Address(inst);
	// With NOINIT, MapReg won't subtract MEMBASEREG even if dest == src1.
	regs_.MapGPR(inst.dest, MIPSMap::NOINIT);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Load8:
		if (addrArg.useRegisterOffset) {
			LDRB(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			LDURB(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			LDRB(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Load8Ext:
		if (addrArg.useRegisterOffset) {
			LDRSB(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			LDURSB(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			LDRSB(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Load16:
		if (addrArg.useRegisterOffset) {
			LDRH(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			LDURH(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			LDRH(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Load16Ext:
		if (addrArg.useRegisterOffset) {
			LDRSH(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			LDURSH(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			LDRSH(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Load32:
		if (addrArg.useRegisterOffset) {
			LDR(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			LDUR(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			LDR(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Load32Linked:
		if (inst.dest != MIPS_REG_ZERO) {
			if (addrArg.useRegisterOffset) {
				LDR(regs_.R(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
			} else if (addrArg.useUnscaled) {
				LDUR(regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
			} else {
				LDR(INDEX_UNSIGNED, regs_.R(inst.dest), addrArg.base, addrArg.immOffset);
			}
		}
		regs_.SetGPRImm(IRREG_LLBIT, 1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_LoadShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Load32Left:
	case IROp::Load32Right:
		// Should not happen if the pass to split is active.
		DISABLE;
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.src3, inst.src1);
	LoadStoreArg addrArg = PrepareSrc1Address(inst);

	ARM64Reg valueReg = regs_.TryMapTempImm(inst.src3);
	if (valueReg == INVALID_REG)
		valueReg = regs_.MapGPR(inst.src3);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Store8:
		if (addrArg.useRegisterOffset) {
			STRB(valueReg, addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			STURB(valueReg, addrArg.base, addrArg.immOffset);
		} else {
			STRB(INDEX_UNSIGNED, valueReg, addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Store16:
		if (addrArg.useRegisterOffset) {
			STRH(valueReg, addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			STURH(valueReg, addrArg.base, addrArg.immOffset);
		} else {
			STRH(INDEX_UNSIGNED, valueReg, addrArg.base, addrArg.immOffset);
		}
		break;

	case IROp::Store32:
		if (addrArg.useRegisterOffset) {
			STR(valueReg, addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			STUR(valueReg, addrArg.base, addrArg.immOffset);
		} else {
			STR(INDEX_UNSIGNED, valueReg, addrArg.base, addrArg.immOffset);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_StoreShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Store32Left:
	case IROp::Store32Right:
		// Should not happen if the pass to split is active.
		DISABLE;
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoadStoreArg addrArg = PrepareSrc1Address(inst);

	switch (inst.op) {
	case IROp::LoadVec4:
		regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
		if (addrArg.useRegisterOffset) {
			fp_.LDR(128, regs_.FQ(inst.dest), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			fp_.LDUR(128, regs_.FQ(inst.dest), addrArg.base, addrArg.immOffset);
		} else {
			fp_.LDR(128, INDEX_UNSIGNED, regs_.FQ(inst.dest), addrArg.base, addrArg.immOffset);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoadStoreArg addrArg = PrepareSrc1Address(inst);

	switch (inst.op) {
	case IROp::StoreVec4:
		regs_.MapVec4(inst.src3);
		if (addrArg.useRegisterOffset) {
			fp_.STR(128, regs_.FQ(inst.src3), addrArg.base, ArithOption(addrArg.regOffset, false, addrArg.signExtendRegOffset));
		} else if (addrArg.useUnscaled) {
			fp_.STUR(128, regs_.FQ(inst.src3), addrArg.base, addrArg.immOffset);
		} else {
			fp_.STR(128, INDEX_UNSIGNED, regs_.FQ(inst.src3), addrArg.base, addrArg.immOffset);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
