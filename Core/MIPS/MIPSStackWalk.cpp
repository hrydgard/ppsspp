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

#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSStackWalk.h"

#define _RS ((op >> 21) & 0x1F)
#define _RT ((op >> 16) & 0x1F)
#define _RD ((op >> 11) & 0x1F)
#define _IMM16 ((signed short)(op & 0xFFFF))

#define MIPSTABLE_IMM_MASK 0xFC000000
#define MIPSTABLE_SPECIAL_MASK 0xFC00003F

namespace MIPSStackWalk {
	using namespace MIPSCodeUtils;

	// In the worst case, we scan this far above the pc for an entry.
	const int MAX_FUNC_SIZE = 32768 * 4;
	// After this we assume we're stuck.
	const size_t MAX_DEPTH = 1024;

	static u32 GuessEntry(u32 pc) {
		SymbolInfo info;
		if (symbolMap.GetSymbolInfo(&info, pc)) {
			return info.address;
		}
		return INVALIDTARGET;
	}

	bool IsSWInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xAC000000;
	}

	bool IsAddImmInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0x20000000 || (op & MIPSTABLE_IMM_MASK) == 0x24000000;
	}

	bool IsMovRegsInstr(MIPSOpcode op) {
		// TODO: There are more options here.  Let's assume addu for now.
		if ((op & MIPSTABLE_SPECIAL_MASK) == 0x00000021) {
			return _RS == 0 || _RT == 0;
		}
		return false;
	}

	bool ScanForAllocaSignature(u32 pc) {
		// In God Eater Burst, for example, after 0880E750, there's what looks like an alloca().
		// It's surrounded by "mov fp, sp" and "mov sp, fp", which is unlikely to be used for other reasons.

		// It ought to be pretty close.
		u32 stop = pc - 32 * 4;
		for (; Memory::IsValidAddress(pc) && pc >= stop; pc -= 4) {
			MIPSOpcode op = Memory::Read_Instruction(pc, true);

			// We're looking for a "mov fp, sp" close by a "addiu sp, sp, -N".
			if (IsMovRegsInstr(op) && _RD == MIPS_REG_FP && (_RS == MIPS_REG_SP || _RT == MIPS_REG_SP)) {
				return true;
			}
		}
		return false;
	}

	bool ScanForEntry(StackFrame &frame, u32 entry, u32 &ra) {
		// TODO: Check if found entry is in the same symbol?  Might be wrong sometimes...

		int ra_offset = -1;
		u32 stop = entry == INVALIDTARGET ? 0 : entry;
		for (u32 pc = frame.pc; Memory::IsValidAddress(pc) && pc >= stop; pc -= 4) {
			MIPSOpcode op = Memory::Read_Instruction(pc, true);

			// Here's where they store the ra address.
			if (IsSWInstr(op) && _RT == MIPS_REG_RA && _RS == MIPS_REG_SP) {
				ra_offset = _IMM16;
			}

			if (IsAddImmInstr(op) && _RT == MIPS_REG_SP && _RS == MIPS_REG_SP) {
				// A positive imm either means alloca() or we went too far.
				if (_IMM16 > 0) {
					// TODO: Maybe check for any alloca() signature and bail?
					continue;
				}
				if (ScanForAllocaSignature(pc)) {
					continue;
				}

				frame.entry = pc;
				frame.stackSize = -_IMM16;
				if (ra_offset != -1 && Memory::IsValidAddress(frame.sp + ra_offset)) {
					ra = Memory::Read_U32(frame.sp + ra_offset);
				}
				return true;
			}
		}
		return false;
	}

	bool DetermineFrameInfo(StackFrame &frame, u32 possibleEntry, u32 threadEntry, u32 &ra) {
		if (ScanForEntry(frame, possibleEntry, ra)) {
			// Awesome, found one that looks right.
			return true;
		} else if (ra != INVALIDTARGET && possibleEntry != INVALIDTARGET) {
			// Let's just assume it's a leaf.
			frame.entry = possibleEntry;
			frame.stackSize = 0;
			return true;
		}

		// Okay, we failed to get one.  Our possibleEntry could be wrong, it often is.
		// Let's just scan upward.
		u32 newPossibleEntry = frame.pc > threadEntry ? threadEntry : frame.pc - MAX_FUNC_SIZE;
		if (ScanForEntry(frame, newPossibleEntry, ra)) {
			return true;
		} else {
			return false;
		}
	}

	std::vector<StackFrame> Walk(u32 pc, u32 ra, u32 sp, u32 threadEntry, u32 threadStackTop) {
		std::vector<StackFrame> frames;
		StackFrame current;
		current.pc = pc;
		current.sp = sp;
		current.entry = INVALIDTARGET;
		current.stackSize = -1;

		u32 prevEntry = INVALIDTARGET;
		while (pc != threadEntry) {
			u32 possibleEntry = GuessEntry(current.pc);
			if (DetermineFrameInfo(current, possibleEntry, threadEntry, ra)) {
				frames.push_back(current);
				if (current.entry == threadEntry || GuessEntry(current.entry) == threadEntry) {
					break;
				}
				if (current.entry == prevEntry || frames.size() >= MAX_DEPTH) {
					// Recursion, means we're screwed.  Let's just give up.
					break;
				}
				prevEntry = current.entry;

				current.pc = ra;
				current.sp += current.stackSize;
				ra = INVALIDTARGET;
				current.entry = INVALIDTARGET;
				current.stackSize = -1;
			} else {
				// Well, we got as far as we could.
				current.entry = possibleEntry;
				current.stackSize = 0;
				frames.push_back(current);
				break;
			}
		}

		return frames;
	}
};