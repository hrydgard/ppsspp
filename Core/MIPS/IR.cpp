// Copyright (c) 2014- PPSSPP Project.

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

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include "Core/MIPS/IR.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/NativeJit.h"
#include "Core/HLE/ReplaceTables.h"


// Jit brokenness to fix:	08866fe8 in Star Soldier - a b generates unnecessary stuff

namespace MIPSComp {

// Reorderings to do:
//   * Hoist loads and stores upwards as far as they will go
//   * Sort sequences of loads and stores by register to allow compiling into things like LDMIA
//   * Join together "pairs" like mfc0/mtv and mfv/mtc0
//   Returns true when it changed something. Might have to be called repeatedly to get everything done.
static bool Reorder(IRBlock *block);
static void ComputeLiveness(IRBlock *block);
static void DebugPrintBlock(IRBlock *block);

IREntry &IRBlock::AddIREntry(u32 address) {
	MIPSOpcode op = Memory::Read_Opcode_JIT(address);
	IREntry e;
	e.pseudoInstr = PSEUDO_NONE;
	e.origAddress = address;
	e.op = op;
	e.info = MIPSGetInfo(op);
	e.flags = 0;
	e.liveGPR = 0;
	e.liveFPR = 0;
	entries.push_back(e);
	return entries.back();
}

void ExtractIR(const JitOptions &jo, u32 address, IRBlock *block) {
	static int count = 0;
	count++;
	block->entries.clear();
	block->address = address;
	block->analysis = MIPSAnalyst::Analyze(address);

	bool joined = false; // flag to debugprint

	int exitInInstructions = -1;
	std::vector<u32> raStack;   // for inlining leaf functions

	while (true) {
		IREntry &e = block->AddIREntry(address);

		if (e.info & DELAYSLOT) {
			// Check if replaceable JAL. If not, bail in 2 instructions.
			bool replacableJal = false;
			if (e.info & IS_JUMP) {
				if ((e.op >> 26) == 3) {
					// Definitely a JAL
					const ReplacementTableEntry *entry;
					if (CanReplaceJalTo(MIPSCodeUtils::GetJumpTarget(e.origAddress), &entry))
						replacableJal = true;
				}
			}

			if (!replacableJal) {
				exitInInstructions = 2;
			}
		}

		u64 gprIn = 0, gprOut = 0;
		u32 fprIn = 0, fprOut = 0;
		if (e.info & IN_RS) gprIn |= (1ULL << MIPS_GET_RS(e.op));
		if (e.info & IN_RT) gprIn |= (1ULL << MIPS_GET_RT(e.op));
		if (e.info & IN_LO) gprIn |= (1ULL << MIPS_REG_LO);
		if (e.info & IN_HI) gprIn |= (1ULL << MIPS_REG_HI);
		if (e.info & IN_VFPU_CC) gprIn |= (1ULL << MIPS_REG_VFPUCC);
		if (e.info & IN_FPUFLAG) gprIn |= (1ULL << MIPS_REG_FPCOND);
		if (e.info & IN_FS) fprIn |= (1 << MIPS_GET_FS(e.op));
		if (e.info & IN_FT) fprIn |= (1 << MIPS_GET_FT(e.op));
		if (e.info & OUT_RT) gprOut |= (1ULL << MIPS_GET_RT(e.op));
		if (e.info & OUT_RD) gprOut |= (1ULL << MIPS_GET_RD(e.op));
		if (e.info & OUT_RA) gprOut |= (1ULL << MIPS_REG_RA);
		if (e.info & OUT_FD) fprOut |= (1 << MIPS_GET_FD(e.op));
		if (e.info & OUT_FS) fprOut |= (1 << MIPS_GET_FS(e.op));
		if (e.info & OUT_LO) gprOut |= (1ULL << MIPS_REG_LO);
		if (e.info & OUT_HI) gprOut |= (1ULL << MIPS_REG_HI);
		if (e.info & OUT_VFPU_CC) gprOut |= (1ULL << MIPS_REG_VFPUCC);
		if (e.info & OUT_FPUFLAG) gprOut |= (1ULL << MIPS_REG_FPCOND);
		if (e.pseudoInstr == PSEUDO_SAVE_RA) gprOut |= (1ULL << MIPS_REG_RA);

		// TODO: Add VFPU analysis as well...

		e.gprIn = gprIn & ~1;  // The zero register doesn't count.
		e.gprOut = gprOut & ~1;
		e.fprIn = fprIn;
		e.fprOut = fprOut;

		if ((e.info & IS_JUMP) && jo.continueBranches) {
			// Figure out exactly what instruction it is.
			if ((e.op >> 26) == 2) {   // It's a plain j instruction
				// joined = true;
				exitInInstructions = -1;
				// Remove the just added jump instruction
				block->RemoveLast();
				// Add the delay slot to the block
				block->AddIREntry(address + 4);
				address = MIPSCodeUtils::GetJumpTarget(address);
				// NOTICE_LOG(JIT, "Blocks joined! %08x->%08x", block->address, target);
				continue;
			} else if ((e.op >> 26) == 3) {
				// jal instruction. Same as above but save RA. This can be optimized away later if needed.
				// joined = true;
				exitInInstructions = -1;
				// Turn the just added jal instruction into a pseudo SaveRA
				block->entries.back().MakePseudo(PSEUDO_SAVE_RA);
				raStack.push_back(address + 8);
				// Add the delay slot
				block->AddIREntry(address + 4);
				address = MIPSCodeUtils::GetJumpTarget(address);
				// NOTICE_LOG(JIT, "Blocks jal-joined! %08x->%08x", block->address, address);
				continue;
			} else if (e.op == MIPS_MAKE_JR_RA()) {
				// TODO: This is only safe if we don't write to RA manually anywhere.

				MIPSOpcode next = Memory::Read_Opcode_JIT(address + 4);
				if (!MIPSAnalyst::IsSyscall(next) && raStack.size()) {
					exitInInstructions = -1;
					// Remove the just added jump instruction, and add the delay slot
					block->RemoveLast();
					block->AddIREntry(address + 4);
					// We know the return address! Keep compiling there.
					// NOTICE_LOG(JIT, "Inlined leaf function! %08x", block->address);
					u32 returnAddr = raStack.back();
					raStack.pop_back();
					address = returnAddr;
					continue;
				}
				// Else do nothing special, compile as usual.
			}
		}

		address += 4;
		if (exitInInstructions > 0)
			exitInInstructions--;
		if (exitInInstructions == 0)
			break;
	}

	// Reorder until no changes are made.
	while (Reorder(block))
		;

	// Computing liveness must be done _after_ reordering, of course.
	ComputeLiveness(block);

	if (joined) {
		DebugPrintBlock(block);
	}

	// TODO: Compute the proxy blocks from the addresses in the IR instructions.
}

static bool Reorder(IRBlock *block) {
	bool changed = false;

	// TODO: We only do some really safe optimizations now. Can't do fun stuff like hoisting loads/stores until we have unfolded all branch delay slots!
	// Well, maybe we could do some of it, but let's not..

	// Sweep downwards
	for (int i = 0; i < (int)block->entries.size() - 1; i++) {
		IREntry &e1 = block->entries[i];
		IREntry &e2 = block->entries[i + 1];

		// Reorder SW, LWC1, SWC1
		if ((MIPSAnalyst::IsSWInstr(e1.op) && MIPSAnalyst::IsSWInstr(e2.op)) ||
				(MIPSAnalyst::IsLWC1Instr(e1.op) && MIPSAnalyst::IsLWC1Instr(e2.op)) ||
				(MIPSAnalyst::IsSWC1Instr(e1.op) && MIPSAnalyst::IsSWC1Instr(e2.op))) {
			// Compare register numbers and swap if possible.
			if (MIPS_GET_RT(e1.op) > MIPS_GET_RT(e2.op) &&
				  (MIPS_GET_IMM16(e1.op) != MIPS_GET_IMM16(e2.op) ||
					 MIPS_GET_RS(e1.op) != MIPS_GET_RS(e2.op))) {
				std::swap(e1, e2);
#if 0
				const char *type = "SW";
				if (MIPSAnalyst::IsLWC1Instr(e1.op)) type = "LWC1";
				else if (MIPSAnalyst::IsSWC1Instr(e1.op)) type = "SWC1";
				NOTICE_LOG(JIT, "Reordered %s at %08x (%08x)", type, e1.origAddress, block->address);
#endif
				changed = true;
			}
		}

		// LW is tricker because we need to check against the destination of one instruction being used
		// as the base register of the other.
		if (MIPSAnalyst::IsLWInstr(e1.op) && MIPSAnalyst::IsLWInstr(e2.op)) {
			// Compare register numbers and swap if possible.
			if (MIPS_GET_RT(e1.op) != MIPS_GET_RS(e2.op) &&
					MIPS_GET_RS(e1.op) != MIPS_GET_RT(e2.op) &&
					MIPS_GET_RT(e1.op) > MIPS_GET_RT(e2.op) &&
					(MIPS_GET_RT(e1.op) != MIPS_GET_RT(e2.op) || 
					 MIPS_GET_IMM16(e1.op) != MIPS_GET_IMM16(e2.op))) {
				std::swap(e1, e2);
				// NOTICE_LOG(JIT, "Reordered LW at %08x (%08x)", e1.origAddress, block->address);
				changed = true;
			}
		}
	}

	// Then sweep upwards
	/*
	for (int i = (int)block->entries.size() - 1; i >= 0; i--) {
		IREntry &e1 = block->entries[i];
		IREntry &e2 = block->entries[i + 1];
		// Do stuff!
	}
	*/
	return changed;
}

void ToBitString(char *ptr, u64 bits, int numBits) {
	for (int i = numBits - 1; i >= 0; i--) {
		*(ptr++) = (bits & (1ULL << i)) ? '1' : '.';
	}
	*ptr = '\0';
}

#define RN(i) currentDebugMIPS->GetRegName(0,i)

void ToGprLivenessString(char *str, int bufsize, u64 bits) {
	str[0] = 0;
	for (int i = 0; i < 32; i++) {
		if (bits & (1ULL << i)) {
			sprintf(str + strlen(str), "%s ", RN(i));
		}
	}
	if (bits & (1ULL << MIPS_REG_LO)) strcat(str, "lo ");
	if (bits & (1ULL << MIPS_REG_HI)) strcat(str, "hi ");
}

void ToFprLivenessString(char *str, int bufsize, u64 bits) {
	str[0] = 0;
	for (int i = 0; i < 32; i++) {
		if (bits & (1ULL << i)) {
			sprintf(str + strlen(str), "f%d ", i);
		}
	}
}

static void ComputeLiveness(IRBlock *block) {
	// Okay, now let's work backwards and compute liveness information.
	//
	// NOTE: This will not be accurate until all branch delay slots have been unfolded!

	// TODO: By following calling conventions etc, it may be possible to eliminate
	// additional register liveness from "jr ra" upwards. However, this is not guaranteed to work on all games.

	u64 gprLiveness = 0;  // note - nine Fs, for HI/LO/flags-in-registers. To define later.
	u32 fprLiveness = 0;
	for (int i = (int)block->entries.size() - 1; i >= 0; i--) {
		IREntry &e = block->entries[i];
		if (e.op == 0) { // nop
			continue;
		}
		// These are already cleaned from the zero register
		e.liveGPR = gprLiveness;
		e.liveFPR = fprLiveness;
		gprLiveness &= ~e.gprOut;
		fprLiveness &= ~e.fprOut;
		gprLiveness |= e.gprIn;
		fprLiveness |= e.fprIn;
	}
}

std::vector<std::string> IRBlock::ToStringVector() {
	std::vector<std::string> vec;
	char buf[1024];
	for (int i = 0; i < (int)entries.size(); i++) {
		IREntry &e = entries[i];
		char instr[256], liveness1[36 * 3], liveness2[32 * 3];
		memset(instr, 0, sizeof(instr));
		ToGprLivenessString(liveness1, sizeof(liveness1), e.liveGPR);
		ToFprLivenessString(liveness2, sizeof(liveness2), e.liveFPR);
		const char *pseudo = " ";
		switch (e.pseudoInstr) {
			case PSEUDO_SAVE_RA:
				pseudo = " save_ra / ";
				break;
		}
		MIPSDisAsm(e.op, e.origAddress, instr, true);
		snprintf(buf, sizeof(buf), "%08x%s%s : %s %s", e.origAddress, pseudo, instr, liveness1, liveness2);
		vec.push_back(std::string(buf));
	}
	return vec;
}

static void DebugPrintBlock(IRBlock *block) {
	std::vector<std::string> vec = block->ToStringVector();
	for (auto &s : vec) {
		printf("%s\n", s.c_str());
	}
	fflush(stdout);
}

IRBlock::RegisterUsage IRBlock::DetermineRegisterUsage(MIPSGPReg reg, int pos, int instrs) {
	const int start = pos;
	int end = pos + instrs;
	if (end > (int)entries.size())
		end = (int)entries.size();
	bool canClobber = true;
	u64 mask = 1ULL << (int)reg;
	for (; pos < end; pos++) {
		IREntry &e = entries[pos];

		if (e.gprIn & mask)
			return USAGE_INPUT;

		bool clobbered = false;
		if (e.gprOut & mask) {
			clobbered = true;
		}

		if (clobbered) {
			if (!canClobber || (e.info & IS_CONDMOVE))
				return USAGE_UNKNOWN;
			return USAGE_CLOBBERED;
		}

		// Bail early if we hit a branch (could follow each path for continuing?)
		if ((e.info & IS_CONDBRANCH) || (e.info & IS_JUMP)) {
			// Still need to check the delay slot (so end after it.)
			// We'll assume likely are taken.
			end = pos + 2;
			// The reason for the start != addr check is that we compile delay slots before branches.
			// That means if we're starting at the branch, it's not safe to allow the delay slot
			// to clobber, since it might have already been compiled.
			// As for LIKELY, we don't know if it'll run the branch or not.
			canClobber = (e.info & LIKELY) == 0 && start != pos;
		}
	}
	return USAGE_UNKNOWN;
}

bool IRBlock::IsRegisterUsed(MIPSGPReg reg, int pos, int instrs) {
	return DetermineRegisterUsage(reg, pos, instrs) == USAGE_INPUT;
}

bool IRBlock::IsRegisterClobbered(MIPSGPReg reg, int pos, int instrs) {
	return DetermineRegisterUsage(reg, pos, instrs) == USAGE_CLOBBERED;
}

const char *IRBlock::DisasmAt(int pos) {
	static char temp[256];
	MIPSDisAsm(entries[pos].op, 0, temp);
	return temp;
}

void IRBlock::RemoveLast() {
	entries.pop_back();
}

}  // namespace
