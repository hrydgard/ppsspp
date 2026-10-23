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

#include <cmath>

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"

#include "Common/BitSet.h"
#include "Common/BitScan.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/Interpreter.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HW/MMIO.h"

#define R(i) (mips->r[i])
#define F(i) (mips->f[i])
#define FI(i) (mips->fi[i])
#define FsI(i) (mips->fs[i])
#define PC (mips->pc)

#define _SIMM16_SHL2 ((u32)(s32)(s16)(op & 0xFFFF) << 2)
#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

#define HI mips->hi
#define LO mips->lo

static inline void DelayBranchTo(MIPSState *mips, u32 where) {
	if (!mips->space->IsValidAddress(where) || (where & 3) != 0) {
		Core_ExecException(where, PC, ExecExceptionType::JUMP);
	}
	PC += 4;
	mips->nextPC = where;
	mips->inDelaySlot = true;
}

static inline void SkipLikely(MIPSState *mips) {
	const u32 delaySlotPC = PC + 4;
	const MIPSOpcode delaySlotOp(mips->space->IsValid4AlignedAddress(delaySlotPC) ? mips->space->ReadUnchecked_U32(delaySlotPC) : 0);
	// Don't actually skip if it is a jump (seen in Brooktown High.)
	if (MIPSGetInfo(delaySlotOp) & IS_JUMP) {
		PC += 4;
	} else {
		PC += 8;
		// The skipped slot costs what it would have run for, as in the IR cores.
		mips->downcount -= MIPSGetInstructionCycleEstimate(delaySlotOp);
	}
}

int MIPS_InterpretSingleStep(MIPSState *mips) {
	if (!mips->space->IsValid4AlignedAddress(mips->pc)) {
		Core_ExecException(mips->pc, mips->	pc, ExecExceptionType::JUMP);
		return 0;
	}
	MIPSOpcode op = ReadExecutedOp(*mips->space, mips->pc);
	// Same reason as the run loop in MIPSInterpret_RunUntil - see ApplyHostRoundingMode.
	ApplyHostRoundingMode(mips);
	if (mips->inDelaySlot) {
		MIPSInterpret(mips, op);
		if (mips->inDelaySlot) {
			mips->pc = mips->nextPC;
			mips->inDelaySlot = false;
		}
	} else {
		MIPSInterpret(mips, op);
	}
	RestoreHostRoundingMode();
	return 1;
}

// Hardware registers, for kernel code only.
static u32 ReadMMIO(MIPSState *mips, u32 addr, int size) {
	// The Media Engine always runs in kernel mode.
	if (!mips->IsMediaEngine() && !Memory::MayRunInKernelMode(mips->pc)) {
		Core_MemoryException(addr, size, mips->pc, MemoryExceptionType::READ_WORD, "Kernel mode only");
		return MMIO::UNHANDLED_READ;
	}
	return MMIO::Read(mips, addr, size);
}

static void WriteMMIO(MIPSState *mips, u32 addr, int size, u32 value) {
	if (!mips->IsMediaEngine() && !Memory::MayRunInKernelMode(mips->pc)) {
		Core_MemoryException(addr, size, mips->pc, MemoryExceptionType::WRITE_WORD, "Kernel mode only");
		return;
	}
	MMIO::Write(mips, addr, size, value);
}

namespace MIPSInt {
	void Int_Cache(MIPSState *mips, MIPSOpcode op) {
		int imm = SignExtend16ToS32(op);
		int rs = _RS;
		uint32_t addr = R(rs) + imm;
		int func = (op >> 16) & 0x1F;

		// Let's only report this once per run to be safe from impacting perf.
		static bool loggedAlignment = false;

		// It appears that a cache line is 0x40 (64) bytes, loops in games
		// issue the cache instruction at that interval.

		// These codes might be PSP-specific, they don't match regular MIPS cache codes very well

		// NOTE: If you add support for more, make sure they are handled in the various Jit::Comp_Cache.
		switch (func) {
		// Icache
		case 8:
		{
			// Invalidate the instruction cache at this address.
			// We assume the CPU won't be reset during this, so no locking.
			// Let's over invalidate to be super safe.
			uint32_t alignedAddr = addr & ~0x3F;
			int size = 0x40 + (addr & 0x3F);
			// Through MIPSState, which also drops replacement hooks in functions the game changed.
			mips->InvalidateICacheRangeImmediate(alignedAddr, size);
			if (mips->jit) {
				// Using a bool to avoid locking/etc. in case it's slow.
				if (!loggedAlignment && (addr & 0x3F) != 0) {
					// These are seen exclusively in Lego games, and are really no big deal. Reporting removed.
					WARN_LOG(Log::JIT, "Unaligned icache invalidation of %08x (%08x + %d) at PC=%08x", addr, R(rs), imm, PC);
					loggedAlignment = true;
				}
				if (alignedAddr <= PC + 4 && alignedAddr + size >= PC - 4) {
					// This is probably rare so we don't use a static bool.
					WARN_LOG_REPORT_ONCE(icacheInvalidatePC, Log::JIT, "Invalidating address near PC: %08x (%08x + %d) at PC=%08x", addr, R(rs), imm, PC);
				}
			}
			break;
		}

		// Dcache
		case 24:
			// "Create Dirty Exclusive" - for avoiding a cacheline fill before writing to it.
			// Will cause garbage on the real machine so we just ignore it, the app will overwrite the cacheline.
			break;
		case 25:  // Hit Invalidate - zaps the line if present in cache. Should not writeback???? scary.
			// No need to do anything.
			break;
		case 27:  // D-cube. Hit Writeback Invalidate.  Tony Hawk Underground 2
			break;
		case 30:  // GTA LCS, a lot. Fill (prefetch).   Tony Hawk Underground 2
			break;

		default:
			VERBOSE_LOG(Log::CPU, "cache instruction affecting %08x : function %i", addr, func);
		}

		PC += 4;
	}

	void Int_Syscall(MIPSState *mips, MIPSOpcode op) {
		if (mips->IsMediaEngine()) {
			// TODO: Its exception vector.
			if (!MeCore::SyscallDoesNothing(op, mips->pc)) {
				Core_ExecException(mips->pc, mips->pc, ExecExceptionType::ILLEGAL);
				return;
			}
			mips->r[MIPS_REG_V0] = 0;
			if (mips->inDelaySlot) {
				mips->pc = mips->nextPC;
			} else {
				mips->pc += 4;
			}
			mips->inDelaySlot = false;
			return;
		}
		const u32 syscallPC = mips->pc - 4;
		// Need to pre-move PC, as CallSyscall may result in a rescheduling!
		// To do this neater, we'll need a little generated kernel loop that syscall can jump to and then RFI from
		// but I don't see a need to bother.
		if (mips->inDelaySlot) {
			mips->pc = mips->nextPC;
		} else {
			mips->pc += 4;
		}
		mips->inDelaySlot = false;
		// HLE code is host code - it must not run under the guest's rounding mode.
		RestoreHostRoundingMode();
		CallSyscallWithPC(mips, op, syscallPC);
		ApplyHostRoundingMode(mips);
	}

	void Int_Sync(MIPSState *mips, MIPSOpcode op) {
		//DEBUG_LOG(Log::CPU, "sync");
		PC += 4;
	}

	void Int_Break(MIPSState *mips, MIPSOpcode op) {
		Reporting::ReportMessage("BREAK instruction hit");
		Core_BreakException(PC);
		PC += 4;
	}

	void Int_RelBranch(MIPSState *mips, MIPSOpcode op) {
		int imm = _SIMM16_SHL2;
		int rs = _RS;
		int rt = _RT;
		u32 addr = PC + imm + 4;

		switch (op >> 26)
		{
		case 4:  if (R(rt) == R(rs))  DelayBranchTo(mips, addr); else PC += 4; break; //beq
		case 5:  if (R(rt) != R(rs))  DelayBranchTo(mips, addr); else PC += 4; break; //bne
		case 6:  if ((s32)R(rs) <= 0) DelayBranchTo(mips, addr); else PC += 4; break; //blez
		case 7:  if ((s32)R(rs) > 0) DelayBranchTo(mips, addr); else PC += 4; break; //bgtz

		case 20: if (R(rt) == R(rs))  DelayBranchTo(mips, addr); else SkipLikely(mips); break; //beql
		case 21: if (R(rt) != R(rs))  DelayBranchTo(mips, addr); else SkipLikely(mips); break; //bnel
		case 22: if ((s32)R(rs) <= 0) DelayBranchTo(mips, addr); else SkipLikely(mips); break; //blezl
		case 23: if ((s32)R(rs) >  0) DelayBranchTo(mips, addr); else SkipLikely(mips); break; //bgtzl

		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_RelBranchRI(MIPSState *mips, MIPSOpcode op) {
		int imm = _SIMM16_SHL2;
		int rs = _RS;
		u32 addr = PC + imm + 4;
		// The linking ones test rs from before they link, ra included (cpu/cpu_alu/branch_link_ra).
		const s32 value = (s32)R(rs);

		switch ((op>>16) & 0x1F) {
		case 0: if (value <  0) DelayBranchTo(mips, addr); else PC += 4; break;//bltz
		case 1: if (value >= 0) DelayBranchTo(mips, addr); else PC += 4; break;//bgez
		case 2: if (value <  0) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bltzl
		case 3: if (value >= 0) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bgezl
		case 16: R(MIPS_REG_RA) = PC + 8; if (value <  0) DelayBranchTo(mips, addr); else PC += 4; break;//bltzal
		case 17: R(MIPS_REG_RA) = PC + 8; if (value >= 0) DelayBranchTo(mips, addr); else PC += 4; break;//bgezal
		case 18: R(MIPS_REG_RA) = PC + 8; if (value <  0) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bltzall
		case 19: R(MIPS_REG_RA) = PC + 8; if (value >= 0) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bgezall
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_VBranch(MIPSState *mips, MIPSOpcode op) {
		int imm = _SIMM16_SHL2;
		u32 addr = PC + imm + 4;

		// x, y, z, w, any, all, (invalid), (invalid)
		int imm3 = (op>>18)&7;
		int val = (mips->vfpuCtrl[VFPU_CTRL_CC] >> imm3) & 1;

		switch ((op >> 16) & 3) {
		case 0: if (!val) DelayBranchTo(mips, addr); else PC += 4; break; //bvf
		case 1: if ( val) DelayBranchTo(mips, addr); else PC += 4; break; //bvt
		case 2: if (!val) DelayBranchTo(mips, addr); else SkipLikely(mips); break; //bvfl
		case 3: if ( val) DelayBranchTo(mips, addr); else SkipLikely(mips); break; //bvtl
		}
	}

	void Int_FPUBranch(MIPSState *mips, MIPSOpcode op) {
		int imm = _SIMM16_SHL2;
		u32 addr = PC + imm + 4;
		switch((op >> 16) & 0x1f)
		{
		case 0: if (!mips->fpcond) DelayBranchTo(mips, addr); else PC += 4; break;//bc1f
		case 1: if ( mips->fpcond) DelayBranchTo(mips, addr); else PC += 4; break;//bc1t
		case 2: if (!mips->fpcond) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bc1fl
		case 3: if ( mips->fpcond) DelayBranchTo(mips, addr); else SkipLikely(mips); break;//bc1tl
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_JumpType(MIPSState *mips, MIPSOpcode op) {
		if (mips->inDelaySlot) {
			// There's one of these in Star Soldier at 0881808c, which seems benign.
			ERROR_LOG(Log::CPU, "Jump in delay slot at %08x", mips->pc);
		}

		const u32 off = ((op & 0x03FFFFFF) << 2);
		const u32 addr = (mips->pc & 0xF0000000) | off;

		switch (op >> 26) {
		case 2: //j
			if (!mips->inDelaySlot)
				DelayBranchTo(mips, addr);
			break;
		case 3: //jal
			R(MIPS_REG_RA) = PC + 8;
			if (!mips->inDelaySlot)
				DelayBranchTo(mips, addr);
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_JumpRegType(MIPSState *mips, MIPSOpcode op) {
		if (mips->inDelaySlot) {
			// There's one of these in Star Soldier at 0881808c, which seems benign.
			ERROR_LOG(Log::CPU, "Jump in delay slot at %08x", mips->pc);
		}

		int rs = _RS;
		int rd = _RD;
		// The CPUs ignore the target's low two bits (tests/cpu/jump/unaligned). Sony's ME firmware keeps
		// flags there, and the PS1 emulator returns through such an address.
		u32 addr = R(rs) & ~3;
		switch (op & 0x3f) {
		case 8: //jr
			if (!mips->inDelaySlot)
				DelayBranchTo(mips, addr);
			break;
		case 9: //jalr
			if (rd != 0)
				R(rd) = PC + 8;
			// Update rd, but otherwise do not take the branch if we're branching.
			if (!mips->inDelaySlot)
				DelayBranchTo(mips, addr);
			break;
		}
	}

	void Int_IType(MIPSState *mips, MIPSOpcode op) {
		u32 uimm = op & 0xFFFF;
		u32 suimm = SignExtend16ToU32(op);
		s32 simm = SignExtend16ToS32(op);

		int rt = _RT;
		int rs = _RS;

		if (rt == 0) { //destination register is zero register
			PC += 4;
			return; //nop
		}

		switch (op>>26)
		{
		case 8:	R(rt) = R(rs) + simm; break; //addi
		case 9:	R(rt) = R(rs) + simm; break;	//addiu
		case 10: R(rt) = (s32)R(rs) < simm; break; //slti
		case 11: R(rt) = R(rs) < suimm; break; //sltiu
		case 12: R(rt) = R(rs) & uimm; break; //andi
		case 13: R(rt) = R(rs) | uimm; break; //ori
		case 14: R(rt) = R(rs) ^ uimm; break; //xori
		case 15: R(rt) = uimm << 16;	 break; //lui
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_StoreSync(MIPSState *mips, MIPSOpcode op) {
		int imm = (signed short)(op & 0xFFFF);
		int rt = _RT;
		int rs = _RS;
		u32 addr = R(rs) + imm;

		switch (op >> 26) {
		case 48: // ll
			if (rt != 0) {
				if (!mips->space->IsValid4AlignedAddress(addr)) {
					if (Memory::IsMMIOAccess(addr)) {
						R(rt) = ReadMMIO(mips, addr, 4);
					} else {
						Core_MemoryException(addr, 4, PC, MemoryExceptionType::READ_WORD, "ll");
						R(rt) = 0;
					}
				} else {
					R(rt) = mips->space->ReadUnchecked_U32(addr);
				}
			}
			mips->llBit = 1;
			break;
		case 56: // sc
			if (mips->llBit) {
				if (!mips->space->IsValid4AlignedAddress(addr)) {
					if (Memory::IsMMIOAccess(addr))
						WriteMMIO(mips, addr, 4, R(rt));
					else
						Core_MemoryException(addr, 4, PC, MemoryExceptionType::WRITE_WORD, "sc");
				} else {
					mips->space->WriteUnchecked_U32(R(rt), addr);
				}
				// Report success even if the store got dropped - reporting failure just makes
				// the usual retry loop spin on the same bad address forever.
				if (rt != 0) {
					R(rt) = 1;
				}
			} else if (rt != 0) {
				R(rt) = 0;
			}
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_RType3(MIPSState *mips, MIPSOpcode op) {
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0) {
			PC += 4;
			return;
		}

		switch (op & 63) {
		case 10: if (R(rt) == 0) R(rd) = R(rs); break; //movz
		case 11: if (R(rt) != 0) R(rd) = R(rs); break; //movn
		case 32: R(rd) = R(rs) + R(rt);		break; //add (exception on overflow)
		case 33: R(rd) = R(rs) + R(rt);		break; //addu
		case 34: R(rd) = R(rs) - R(rt);		break; //sub (exception on overflow)
		case 35: R(rd) = R(rs) - R(rt);		break; //subu
		case 36: R(rd) = R(rs) & R(rt);		break; //and
		case 37: R(rd) = R(rs) | R(rt);		break; //or
		case 38: R(rd) = R(rs) ^ R(rt);		break; //xor
		case 39: R(rd) = ~(R(rs) | R(rt)); break; //nor
		case 42: R(rd) = (s32)R(rs) < (s32)R(rt); break; //slt
		case 43: R(rd) = R(rs) < R(rt);		break; //sltu
		case 44: R(rd) = ((s32)R(rs) > (s32)R(rt)) ? R(rs) : R(rt); break; //max
		case 45: R(rd) = ((s32)R(rs) < (s32)R(rt)) ? R(rs) : R(rt); break;//min
		default:
			_dbg_assert_msg_( 0, "Unknown MIPS instruction %08x", op.encoding);
			break;
		}
		PC += 4;
	}

	// On a bad access that's set to be ignored (the default), we mirror what
	// Memory::ReadOrException_*/WriteOrException_* do, which is also what the JIT's slow
	// path calls: loads produce zero, stores are dropped, and PC advances either way.
	// Returning without advancing PC instead would just re-execute the same instruction forever.
	// When the access is set to break, Core_MemoryException has already stopped the core.
	void Int_ITypeMem(MIPSState *mips, MIPSOpcode op) {
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		const u32 addr = R(rs) + imm;

		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			PC += 4;
			return;
		}

		switch (op >> 26) {
		case 32:
			if (!mips->space->IsValidAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a read from MMIO, so we can handle it.
					R(rt) = SignExtend8ToU32(ReadMMIO(mips, addr, 1));
					break;
				}
				Core_MemoryException(addr, 1, PC, MemoryExceptionType::READ_WORD, "lb");
				R(rt) = 0;
				break;
			}
			R(rt) = SignExtend8ToU32(mips->space->ReadUnchecked_U8(addr));
			break; //lb
		case 33:
			if (!mips->space->IsValid2AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a read from MMIO, so we can handle it.
					R(rt) = SignExtend16ToU32(ReadMMIO(mips, addr, 2));
					break;
				}
				Core_MemoryException(addr, 2, PC, MemoryExceptionType::READ_WORD, "lh");
				R(rt) = 0;
				break;
			}
			R(rt) = SignExtend16ToU32(mips->space->ReadUnchecked_U16(addr));
			break; //lh
		case 35:
			if (!mips->space->IsValid4AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a read from MMIO, so we can handle it.
					R(rt) = ReadMMIO(mips, addr, 4);
					break;
				}
				Core_MemoryException(addr, 4, PC, MemoryExceptionType::READ_WORD, "lw");
				R(rt) = 0;
				break;
			}
			R(rt) = mips->space->ReadUnchecked_U32(addr);
			break; //lw
		case 36:
			if (!mips->space->IsValidAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a read from MMIO, so we can handle it.
					R(rt) = ReadMMIO(mips, addr, 1);
					break;
				}
				Core_MemoryException(addr, 1, PC, MemoryExceptionType::READ_WORD, "lbu");
				R(rt) = 0;
				break;
			}
			R(rt) = mips->space->ReadUnchecked_U8(addr);
			break; //lbu
		case 37:
			if (!mips->space->IsValid2AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a read from MMIO, so we can handle it.
					R(rt) = ReadMMIO(mips, addr, 2);
					break;
				}
				Core_MemoryException(addr, 2, PC, MemoryExceptionType::READ_WORD, "lhu");
				R(rt) = 0;
				break;
			}
			R(rt) = mips->space->ReadUnchecked_U16(addr);
			break; //lhu
		case 40:
			if (!mips->space->IsValidAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a write to MMIO, so we can handle it.
					WriteMMIO(mips, addr, 1, (u8)R(rt));
					break;
				}
				Core_MemoryException(addr, 1, PC, MemoryExceptionType::WRITE_WORD, "sb");
				break;
			}
			mips->space->WriteUnchecked_U8(R(rt), addr);
			break; //sb
		case 41:
			if (!mips->space->IsValid2AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a write to MMIO, so we can handle it.
					WriteMMIO(mips, addr, 2, (u16)R(rt));
					break;
				}
				Core_MemoryException(addr, 2, PC, MemoryExceptionType::WRITE_WORD, "sh");
				break;
			}
			mips->space->WriteUnchecked_U16(R(rt), addr);
			break; //sh
		case 43:
			if (!mips->space->IsValid4AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					// This is a write to MMIO, so we can handle it.
					WriteMMIO(mips, addr, 4, R(rt));
					break;
				}
				Core_MemoryException(addr, 4, PC, MemoryExceptionType::WRITE_WORD, "sw");
				break;
			}
			mips->space->WriteUnchecked_U32(R(rt), addr);
			break; //sw

		// When there's an LWL and an LWR together, we should be able to peephole optimize that
		// into a single non-alignment-checking LW.
		case 34: //lwl
			{
				// Not checking for alignment here - the actual read will be aligned.
				u32 mem = 0;
				if (mips->space->IsValidAddress(addr)) {
					mem = mips->space->ReadUnchecked_U32(addr & 0xfffffffc);
				} else if (Memory::IsMMIOAccess(addr)) {
					mem = ReadMMIO(mips, addr & 0xfffffffc, 4);
				} else {
					Core_MemoryException(addr, 4, PC, MemoryExceptionType::READ_WORD, "lwl");
				}
				u32 shift = (addr & 3) * 8;
				u32 result = ( u32(R(rt)) & (0x00ffffff >> shift) ) | ( mem << (24 - shift) );
				R(rt) = result;
			}
			break;

		case 38: //lwr
			{
				// Not checking for alignment here - the actual read will be aligned.
				u32 mem = 0;
				if (mips->space->IsValidAddress(addr)) {
					mem = mips->space->ReadUnchecked_U32(addr & 0xfffffffc);
				} else if (Memory::IsMMIOAccess(addr)) {
					mem = ReadMMIO(mips, addr & 0xfffffffc, 4);
				} else {
					Core_MemoryException(addr, 4, PC, MemoryExceptionType::READ_WORD, "lwr");
				}
				u32 shift = (addr & 3) * 8;
				u32 regval = R(rt);
				u32 result = ( regval & (0xffffff00 << (24 - shift)) ) | ( mem	>> shift );
				R(rt) = result;
			}
			break;

		case 42: //swl
			{
				// Not checking for alignment here - the actual read/write will be aligned.
				if (!mips->space->IsValidAddress(addr)) {
					Core_MemoryException(addr, 4, PC, MemoryExceptionType::WRITE_WORD, "swl");
					break;
				}
				u32 shift = (addr & 3) * 8;
				u32 mem = mips->space->ReadUnchecked_U32(addr & 0xfffffffc);
				u32 result = ( ( u32(R(rt)) >>	(24 - shift) ) ) | (	mem & (0xffffff00 << shift) );
				mips->space->WriteUnchecked_U32(result, (addr & 0xfffffffc));
			}
			break;

		case 46: //swr
			{
				// Not checking for alignment here - the actual read/write will be aligned.
				if (!mips->space->IsValidAddress(addr)) {
					Core_MemoryException(addr, 4, PC, MemoryExceptionType::WRITE_WORD, "swr");
					break;
				}
				u32 shift = (addr & 3) << 3;
				u32 mem = mips->space->ReadUnchecked_U32(addr & 0xfffffffc);
				u32 result = ( ( u32(R(rt)) << shift ) | (mem	& (0x00ffffff >> (24 - shift)) ) );
				mips->space->WriteUnchecked_U32(result, (addr & 0xfffffffc));
			}
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret Mem instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPULS(MIPSState *mips, MIPSOpcode op) 	{
		s32 offset = (s16)(op & 0xFFFF);
		int ft = _FT;
		int rs = _RS;
		u32 addr = R(rs) + offset;

		switch (op >> 26) {
		case 49:
			if (!mips->space->IsValid4AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					FI(ft) = ReadMMIO(mips, addr, 4);
					break;
				}
				Core_MemoryException(addr, 4, PC, MemoryExceptionType::READ_WORD, "lwc1");
				FI(ft) = 0;
				break;
			}
			FI(ft) = mips->space->ReadUnchecked_U32(addr);
			break; //lwc1
		case 57:
			if (!mips->space->IsValid4AlignedAddress(addr)) {
				if (Memory::IsMMIOAccess(addr)) {
					WriteMMIO(mips, addr, 4, FI(ft));
					break;
				}
				Core_MemoryException(addr, 4, PC, MemoryExceptionType::WRITE_WORD, "swc1");
				break;
			}
			mips->space->WriteUnchecked_U32(FI(ft), addr);
			break; //swc1
		default:
			_dbg_assert_msg_(false,"Trying to interpret FPULS instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_mxc1(MIPSState *mips, MIPSOpcode op)
	{
		int fs = _FS;
		int rt = _RT;

		switch ((op>>21)&0x1f) {
		case 0: //mfc1
			if (rt != 0)
				R(rt) = FI(fs);
			break;

		case 2: //cfc1
			if (rt != 0) {
				if (fs == 31) {
					mips->fcr31 = (mips->fcr31 & ~(1<<23)) | ((mips->fpcond & 1)<<23);
					R(rt) = mips->fcr31;
				} else if (fs == 0) {
					R(rt) = MIPSState::FCR0_VALUE;
				} else {
					WARN_LOG_REPORT(Log::CPU, "ReadFCR: Unexpected reg %d", fs);
					R(rt) = 0;
				}
				break;
			}
			break;

		case 4: //mtc1
			FI(fs) = R(rt);
			break;

		case 6: //ctc1
			{
				u32 value = R(rt);
				if (fs == 31) {
					mips->fcr31 = value & 0x0181FFFF;
					mips->fpcond = (value >> 23) & 1;
					// Don't bother locking, assuming the CPU can't be reset now anyway.
					if (mips->jit) {
						// In case of DISABLE, we need to tell jit we updated FCR31.
						mips->jit->UpdateFCR31();
					} else {
						// The interpreter emulates the rounding mode by putting the host FPU in it,
						// so it has to switch right here rather than at the next block boundary.
						ApplyHostRoundingMode(mips);
					}
				} else {
					WARN_LOG_REPORT(Log::CPU, "WriteFCR: Unexpected reg %d (value %08x)", fs, value);
				}
				DEBUG_LOG(Log::CPU, "FCR%i written to, value %08x", fs, value);
				break;
			}

		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	// COP0, for kernel-mode code (e.g. flash0:/reboot.bin) that reads/writes it directly - ordinary
	// PSP user-mode code never executes these instructions (kernel state is reached via HLE
	// syscalls instead). Writes read back; nothing models interrupts, exceptions or the TLB.
	void Int_Cop0(MIPSState *mips, MIPSOpcode op) {
		int rt = _RT;
		int rd = _RD;

		switch ((op >> 21) & 0x1f) {
		case 0: //mfc0
			if (rt != 0) {
				// Real MIPS COP0 register 9 (Count) free-runs on its own (incrementing every
				// other cycle) regardless of software writes - unlike the rest of this dummy
				// shadow file, a plain "read back whatever was last written" (defaulting to a
				// constant 0) would be wrong here and could plausibly starve boot-time
				// calibration code of a nonzero seed/divisor. Tie it to CoreTiming instead;
				// still not real COP0 semantics (no Compare-triggered interrupt), just a
				// closer approximation. See docs/VSHBootInvestigation.md "Attempt 9".
				if (mips->IsMediaEngine())
					R(rt) = MeCore::ReadCop0(rd);
				else if (rd == 9)
					R(rt) = (u32)CoreTiming::GetTicks(mips);
				else
					R(rt) = mips->cop0[rd];
			}
			break;

		case 2: //cfc0
			if (rt != 0)
				R(rt) = mips->cop0Control[rd];
			break;

		case 6: //ctc0
			mips->cop0Control[rd] = R(rt);
			break;

		case 4: //mtc0
			if (mips->IsMediaEngine())
				MeCore::WriteCop0(rd, R(rt));
			else
				mips->cop0[rd] = R(rt);
			break;

		case 10: //rdpgpr
			if (rt != 0)
				R(rt) = R(rd);
			break;

		case 11: //mfmc0 (di/ei)
			if (rt != 0)
				R(rt) = mips->cop0[12];  // Status
			break;

		case 14: //wrpgpr
			R(rd) = R(rt);
			break;

		default:
			_dbg_assert_msg_(false, "Trying to interpret COP0 instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	// Return from an exception, as MIPS32 defines it (cpu/cop0/eret): to ErrorEPC (30) if Status.ERL
	// is set, clearing only that, otherwise to EPC (14), clearing Status.EXL. No delay slot.
	void Int_Eret(MIPSState *mips, MIPSOpcode op) {
		u32 &status = mips->cop0[12];
		if (status & 4) {
			mips->pc = mips->cop0[30];
			status &= ~4;
		} else {
			mips->pc = mips->cop0[14];
			status &= ~2;
		}
		mips->llBit = 0;
	}

	void Int_RType2(MIPSState *mips, MIPSOpcode op) {
		int rs = _RS;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0) {
			PC += 4;
			return;
		}

		switch (op & 63) {
		case 22:	//clz
			R(rd) = clz32(R(rs));
			break;
		case 23: //clo
			R(rd) = clz32(~R(rs));
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_MulDivType(MIPSState *mips, MIPSOpcode op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		switch (op & 63)
		{
		case 24: //mult
			{
				s64 result = (s64)(s32)R(rs) * (s64)(s32)R(rt);
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 25: //multu
			{
				u64 resultBits = (u64)R(rs) * (u64)R(rt);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 28: //madd
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origValBits = (u64)lo | ((u64)(hi)<<32);
				s64 origVal = (s64)origValBits;
				s64 result = origVal + (s64)(s32)a * (s64)(s32)b;
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 29: //maddu
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origVal = (u64)lo | ((u64)(hi)<<32);
				u64 result = origVal + (u64)a * (u64)b;
				LO = (u32)(result);
				HI = (u32)(result>>32);
			}
			break;
		case 46: //msub
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origValBits = (u64)lo | ((u64)(hi)<<32);
				s64 origVal = (s64)origValBits;
				s64 result = origVal - (s64)(s32)a * (s64)(s32)b;
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 47: //msubu
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origVal = (u64)lo | ((u64)(hi)<<32);
				u64 result = origVal - (u64)a * (u64)b;
				LO = (u32)(result);
				HI = (u32)(result>>32);
			}
			break;
		case 16: if (rd != 0) R(rd) = HI; break; //mfhi
		case 17: HI = R(rs); break; //mthi
		case 18: if (rd != 0) R(rd) = LO; break; //mflo
		case 19: LO = R(rs); break; //mtlo
		case 26: //div
			{
				s32 a = (s32)R(rs);
				s32 b = (s32)R(rt);
				if (a == (s32)0x80000000 && b == -1) {
					// The one overflow. Hardware leaves the remainder at zero (cpu/cpu_alu/cpu_div).
					LO = 0x80000000;
					HI = 0;
				} else if (b != 0) {
					LO = (u32)(a / b);
					HI = (u32)(a % b);
				} else {
					LO = a < 0 ? 1 : -1;
					HI = a;
				}
			}
			break;
		case 27: //divu
			{
				u32 a = R(rs);
				u32 b = R(rt);
				if (b != 0) {
					LO = (a/b);
					HI = (a%b);
				} else {
					LO = a <= 0xFFFF ? 0xFFFF : -1;
					HI = a;
				}
			}
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_ShiftType(MIPSState *mips, MIPSOpcode op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		int sa = _FD;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}

		switch (op & 0x3f)
		{
		case 0: R(rd) = R(rt) << sa; break; //sll
		case 2:
			// Only the low bit of the unused rs field counts (cpu/cpu_alu/shift_encodings).
			if (_RS & 1) { //rotr
				R(rd) = __rotr(R(rt), sa);
			} else { //srl
				R(rd) = R(rt) >> sa;
			}
			break;

		case 3: R(rd) = (u32)(((s32)R(rt)) >> sa); break; //sra
		case 4: R(rd) = R(rt) << (R(rs) & 0x1F); break; //sllv
		case 6:
			// Same for the unused sa field.
			if (_FD & 1) { // rotrv
				R(rd) = __rotr(R(rt), R(rs));
			} else { //srlv
				R(rd) = R(rt) >> (R(rs) & 0x1F);
			}
			break;
		case 7: R(rd) = (u32)(((s32)R(rt)) >> (R(rs) & 0x1F)); break; //srav
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex(MIPSState *mips, MIPSOpcode op) {
		int rt = _RT;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0) {
			PC += 4;
			return;
		}

		switch (AllegrexBitOp(op)) {
		case 16: // seb
			R(rd) = SignExtend8ToU32(R(rt));
			break;

		case 20: // bitrev
			R(rd) = ReverseBits32(R(rt));
			break;

		case 24: // seh
			R(rd) = SignExtend16ToU32(R(rt));
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex2(MIPSState *mips, MIPSOpcode op) {
		int rt = _RT;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0) {
			PC += 4;
			return;
		}

		switch (AllegrexBitOp(op)) {
		case 2: //wsbh
			R(rd) = ((R(rt) & 0xFF00FF00) >> 8) | ((R(rt) & 0x00FF00FF) << 8);
			break;
		case 3: //wsbw
			R(rd) = swap32(R(rt));
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	// Only the Media Engine runs code that halts. It sleeps until an interrupt.
	void Int_Halt(MIPSState *mips, MIPSOpcode op) {
		if (!mips->IsMediaEngine()) {
			Core_ExecException(PC, PC, ExecExceptionType::ILLEGAL);
			return;
		}
		PC += 4;
		MeCore::Halt();
	}

	// An unknown Media Engine instruction (primary opcode 0x2C): Sony's ME firmware runs 0xB0E20000
	// (with v0 = 1) and 0xB0E00000 around a reset of sysreg 0x50/0x54 bits when it resumes from sleep
	// (t2img 0x88300948). Possibly a VME control write. Ignored.
	void Int_Me2C(MIPSState *mips, MIPSOpcode op) {
		if (!mips->IsMediaEngine()) {
			Core_ExecException(PC, PC, ExecExceptionType::ILLEGAL);
			return;
		}
		PC += 4;
	}

	// The interrupt enable flag, shared with the HLE sceKernelCpuSuspendIntr/ResumeIntr, which on
	// hardware are just mfic v0, $0; mtic zero, $0 and mtic a0, $0. Only $0 exists.
	void Int_Special2(MIPSState *mips, MIPSOpcode op) {
		int rt = _RT;
		switch (op & 0x3F) {
		case 36:  // mfic
			if (rt != 0 && mips->IsMediaEngine())
				R(rt) = MeCore::g_meState->interruptEnable;
			else if (rt != 0)
				R(rt) = __InterruptsEnabled() ? 1 : 0;
			break;
		case 38:  // mtic
			if (mips->IsMediaEngine()) {
				MeCore::SetInterruptEnable(R(rt));
				break;
			}
			// Only bit 0 counts: mtic 2 disables. Pending interrupts run at the next scheduling point,
			// not right away as with sceKernelCpuResumeIntr.
			if (R(rt) & 1)
				__EnableInterrupts();
			else
				__DisableInterrupts();
			break;
		}
		PC += 4;
	}

	void Int_Special3(MIPSState *mips, MIPSOpcode op) {
		int rs = _RS;
		int rt = _RT;
		int pos = _POS;

		// Don't change $zr.
		if (rt == 0) {
			PC += 4;
			return;
		}

		switch (op & 0x3f) {
		case 0x0: //ext
			{
				int size = _SIZE + 1;
				u32 sourcemask = 0xFFFFFFFFUL >> (32 - size);
				R(rt) = (R(rs) >> pos) & sourcemask;
			}
			break;
		case 0x4: //ins
			{
				// The size field actually holds msb (= pos + size - 1), so build the mask from
				// that and shift it down, the way the JITs do. Computing the width as
				// (_SIZE + 1) - pos instead would shift by 32 or more when msb < pos - undefined
				// behavior, and on x86 it yields an all-ones mask that writes bits the JITs leave
				// alone. Hardware calls that encoding unpredictable, so all we need is to be
				// consistent and not invoke UB.
				const u32 mask = 0xFFFFFFFFUL >> (31 - _SIZE);
				const u32 sourcemask = mask >> pos;
				const u32 destmask = sourcemask << pos;
				R(rt) = (R(rt) & ~destmask) | ((R(rs) & sourcemask) << pos);
			}
			break;
		}

		PC += 4;
	}

	void Int_FPU2op(MIPSState *mips, MIPSOpcode op) {
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 4:	//sqrt
			F(fd) = sqrtf(F(fs));
			// A negative input gives a positive NaN, not the host's (cpu/fpu/roundmode).
			if (F(fs) < 0.0f) {
				FsI(fd) = 0x7FC00000;
			}
			break;
		case 5:	F(fd)	= fabsf(F(fs)); break; //abs
		case 6:	F(fd)	= F(fs); break; //mov
		case 7:	F(fd)	= -F(fs); break; //neg
		case 12:
		case 13:
		case 14:
		case 15:
			switch (op & 0x3f)
			{
			// round.w.s is round-half-to-even, not half-away-from-zero - and its mode is fixed,
			// so unlike cvt.w.s below it must not follow fcr31. round_ieee_754 is both.
			case 12: FsI(fd) = SaturatedFloatToInt(round_ieee_754(FlushForConversion(mips, F(fs)))); break; //round.w.s
			case 13: FsI(fd) = SaturatedFloatToInt(truncf(FlushForConversion(mips, F(fs)))); break; //trunc.w.s
			case 14: FsI(fd) = SaturatedFloatToInt(ceilf(FlushForConversion(mips, F(fs)))); break; //ceil.w.s
			case 15: FsI(fd) = SaturatedFloatToInt(floorf(FlushForConversion(mips, F(fs)))); break; //floor.w.s
			}
			break;
		case 32: F(fd) = (float)FsI(fs); break; //cvt.s.w

		case 36:
			switch (mips->fcr31 & 3)
			{
			case 0: FsI(fd) = SaturatedFloatToInt(round_ieee_754(FlushForConversion(mips, F(fs)))); break;  // RINT_0
			case 1: FsI(fd) = SaturatedFloatToInt(truncf(FlushForConversion(mips, F(fs)))); break;  // CAST_1
			case 2: FsI(fd) = SaturatedFloatToInt(ceilf(FlushForConversion(mips, F(fs)))); break;  // CEIL_2
			case 3: FsI(fd) = SaturatedFloatToInt(floorf(FlushForConversion(mips, F(fs)))); break;  // FLOOR_3
			}
			break; //cvt.w.s
		default:
			_dbg_assert_msg_(false,"Trying to interpret FPU2Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPUComp(MIPSState *mips, MIPSOpcode op) {
		int fs = _FS;
		int ft = _FT;
		bool cond;
		switch (op & 0xf)
		{
		case 0: //f
		case 8: //sf
			cond = false;
			break;

		case 1: //un
		case 9: //ngle
			cond = my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 2: //eq
		case 10: //seq
			cond = !my_isnan(F(fs)) && !my_isnan(F(ft)) && (F(fs) == F(ft));
			break;

		case 3: //ueq
		case 11: //ngl
			cond = (F(fs) == F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 4: //olt
		case 12: //lt
			cond = (F(fs) < F(ft));
			break;

		case 5: //ult
		case 13: //nge
			cond = (F(fs) < F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 6: //ole
		case 14: //le
			cond = (F(fs) <= F(ft));
			break;

		case 7: //ule
		case 15: //ngt
			cond = (F(fs) <= F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret FPUComp instruction that can't be interpreted");
			cond = false;
			break;
		}
		mips->fpcond = cond;
		PC += 4;
	}

	void Int_FPU3op(MIPSState *mips, MIPSOpcode op) {
		int ft = _FT;
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 0: F(fd) = F(fs) + F(ft); break; // add.s
		case 1: F(fd) = F(fs) - F(ft); break; // sub.s
		case 2: // mul.s
			if ((my_isinf(F(fs)) && F(ft) == 0.0f) || (my_isinf(F(ft)) && F(fs) == 0.0f)) {
				// Must be positive NAN, see #12519.
				FI(fd) = 0x7fc00000;
			} else {
				F(fd) = F(fs) * F(ft);
			}
			break;
		case 3: // div.s
		{
			// The only FPU exception we implement so far. IEEE 754 raises divide-by-zero only when
			// an exact infinity comes out of finite operands, so 0/0 (invalid operation, which we
			// don't detect) and inf/0 and NaN operands (no exception at all) are all excluded.
			// Cause is per-instruction, so clear it here rather than leaving the last one set.
			// Real hardware clears all five Cause bits in every FPU op, we only manage this one.
			mips->fcr31 &= ~FCR31_CAUSE_DIV_BY_ZERO;
			if (F(ft) == 0.0f && F(fs) != 0.0f && !my_isnanorinf(F(fs))) {
				mips->fcr31 |= FCR31_CAUSE_DIV_BY_ZERO;
				if (mips->fcr31 & FCR31_ENABLE_DIV_BY_ZERO) {
					// Unmasked. Hardware takes the trap here, leaving fd alone and not setting the
					// sticky flag - the handler is expected to deal with it. We can't run the
					// guest's handler, and PSP threads start with this trap enabled (fcr31 =
					// 0x00000e00) while games divide by zero all the time, so acting on it at all
					// is a developer opt-in. The fcr31 bits above are updated either way, so what
					// the game sees when it reads the register doesn't depend on the setting.
					if (g_Config.bEnableFPUExceptionTraps) {
						Core_FPUException(PC, FPUExceptionType::DIVIDE_BY_ZERO);
						break;
					}
				} else {
					// Masked: sticky flag, and the default result (a correctly signed infinity).
					mips->fcr31 |= FCR31_FLAG_DIV_BY_ZERO;
				}
			}
			F(fd) = F(fs) / F(ft);
			break;
		}
		default:
			_dbg_assert_msg_(false,"Trying to interpret FPU3Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Interrupt(MIPSState *mips, MIPSOpcode op) {
		static int reported = 0;
		switch (op & 1)
		{
		case 0:
			// unlikely to be legitimately used
			if (!reported) {
				Reporting::ReportMessage("INTERRUPT instruction hit (%08x) at %08x", op.encoding, mips->pc);
				WARN_LOG(Log::CPU, "Disable/Enable Interrupt CPU instruction");
				reported = 1;
			}
			break;
		}
		PC += 4;
	}

	void Int_CallReplacement(MIPSState *mips, MIPSOpcode op) {
		_assert_((PC & 3) == 0);

		// The CPU cores substitute these for replaced instructions, memory never holds them.
		// If it does, the game is executing data.
		if (Memory::Read_Instruction(PC).encoding == op.encoding) {
			Core_ExecException(PC, PC, ExecExceptionType::ILLEGAL);
			return;
		}

		// It's a replacement func!
		int index = op.encoding & MIPS_REPLACEMENT_INDEX_MASK;
		const ReplacementTableEntry *entry = GetReplacementFunc(index);
		if (entry && entry->replaceFunc && (entry->flags & REPFLAG_DISABLED) == 0) {
			// Like a syscall, a replacement function is host code - see Int_Syscall.
			RestoreHostRoundingMode();
			int cycles = entry->replaceFunc(mips);
			ApplyHostRoundingMode(mips);

			if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
				// Interpret the original instruction under the hook.
				MIPSInterpret(mips, Memory::Read_Instruction(PC));
			} else if (cycles < 0) {
				// Leave PC unchanged, call the replacement again (assumes args are modified.)
				mips->downcount += cycles;
			} else {
				PC = mips->r[MIPS_REG_RA];
				mips->downcount -= cycles;
			}
		} else {
			if (!entry || !entry->replaceFunc) {
				ERROR_LOG(Log::CPU, "Bad replacement function index %i", index);
			}
			// Interpret the original instruction under it.
			MIPSInterpret(mips, Memory::Read_Instruction(PC));
		}
	}
}
