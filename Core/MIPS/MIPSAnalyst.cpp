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

#include <map>
#include <set>
#include "ext/cityhash/city.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "ext/xxhash.h"

using namespace MIPSCodeUtils;

// Not in a namespace because MSVC's debugger doesn't like it
static std::vector<MIPSAnalyst::AnalyzedFunction> functions;

// TODO: Try multimap instead
// One function can appear in multiple copies in memory, and they will all have 
// the same hash and should all be replaced if possible.
static std::map<u64, std::vector<MIPSAnalyst::AnalyzedFunction*>> hashToFunction;

struct HashMapFunc {
	char name[64];
	u64 hash;
	u32 size; //number of bytes

	bool operator < (const HashMapFunc &other) const {
		return hash < other.hash || (hash == other.hash && size < other.size);
	}
};

static std::set<HashMapFunc> hashMap;

static std::string hashmapFileName;

#define MIPSTABLE_IMM_MASK 0xFC000000

namespace MIPSAnalyst {
	// Only can ever output a single reg.
	MIPSGPReg GetOutGPReg(MIPSOpcode op) {
		MIPSInfo opinfo = MIPSGetInfo(op);
		if (opinfo & OUT_RT) {
			return MIPS_GET_RT(op);
		}
		if (opinfo & OUT_RD) {
			return MIPS_GET_RD(op);
		}
		if (opinfo & OUT_RA) {
			return MIPS_REG_RA;
		}
		return MIPS_REG_INVALID;
	}

	bool ReadsFromGPReg(MIPSOpcode op, MIPSGPReg reg) {
		MIPSInfo info = MIPSGetInfo(op);
		if ((info & IN_RS) != 0 && MIPS_GET_RS(op) == reg) {
			return true;
		}
		if ((info & IN_RT) != 0 && MIPS_GET_RT(op) == reg) {
			return true;
		}
		return false;
	}

	bool IsDelaySlotNiceReg(MIPSOpcode branchOp, MIPSOpcode op, MIPSGPReg reg1, MIPSGPReg reg2) {
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		// $0 is never an out reg, it's always 0.
		if (reg1 != MIPS_REG_ZERO && GetOutGPReg(op) == reg1) {
			return false;
		}
		if (reg2 != MIPS_REG_ZERO && GetOutGPReg(op) == reg2) {
			return false;
		}
		return true;
	}

	bool IsDelaySlotNiceVFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		return (info & OUT_VFPU_CC) == 0;
	}

	bool IsDelaySlotNiceFPU(MIPSOpcode branchOp, MIPSOpcode op) {
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IS_CONDBRANCH) {
			return false;
		}
		return (info & OUT_FPUFLAG) == 0;
	}

	bool IsSyscall(MIPSOpcode op) {
		// Syscalls look like this: 0000 00-- ---- ---- ---- --00 1100
		return (op >> 26) == 0 && (op & 0x3f) == 12;
	}

	static bool IsSWInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xAC000000;
	}
	static bool IsSBInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA0000000;
	}
	static bool IsSHInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA4000000;
	}

	static bool IsSWLInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xA8000000;
	}
	static bool IsSWRInstr(MIPSOpcode op) {
		return (op & MIPSTABLE_IMM_MASK) == 0xB8000000;
	}

	bool OpWouldChangeMemory(u32 pc, u32 addr) {
		auto op = Memory::Read_Instruction(pc);
		int gprMask = 0;
		// TODO: swl/swr are annoying, not handled yet.
		if (IsSWInstr(op))
			gprMask = 0xFFFFFFFF;
		if (IsSHInstr(op))
			gprMask = 0x0000FFFF;
		if (IsSBInstr(op))
			gprMask = 0x000000FF;

		if (gprMask != 0)
		{
			MIPSGPReg reg = MIPS_GET_RT(op);
			u32 writeVal = currentMIPS->r[reg] & gprMask;
			u32 prevVal = Memory::Read_U32(addr) & gprMask;

			// TODO: Technically, the break might be for 1 byte in the middle of a sw.
			return writeVal != prevVal;
		}

		// TODO: Not handled yet.
		return true;
	}

	AnalysisResults Analyze(u32 address) {
		const int MAX_ANALYZE = 10000;

		AnalysisResults results;

		//set everything to -1 (FF)
		memset(&results, 255, sizeof(AnalysisResults));
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			results.r[i].used = false;
			results.r[i].readCount = 0;
			results.r[i].writeCount = 0;
			results.r[i].readAsAddrCount = 0;
		}

		for (u32 addr = address, endAddr = address + MAX_ANALYZE; addr <= endAddr; addr += 4) {
			MIPSOpcode op = Memory::Read_Instruction(addr);
			MIPSInfo info = MIPSGetInfo(op);

			MIPSGPReg rs = MIPS_GET_RS(op);
			MIPSGPReg rt = MIPS_GET_RT(op);

			if (info & IN_RS) {
				if ((info & IN_RS_ADDR) == IN_RS_ADDR) {
					results.r[rs].MarkReadAsAddr(addr);
				} else {
					results.r[rs].MarkRead(addr);
				}
			}

			if (info & IN_RT) {
				results.r[rt].MarkRead(addr);
			}

			MIPSGPReg outReg = GetOutGPReg(op);
			if (outReg != MIPS_REG_INVALID) {
				results.r[outReg].MarkWrite(addr);
			}

			if (info & DELAYSLOT) {
				// Let's just finish the delay slot before bailing.
				endAddr = addr + 4;
			}
		}

		int numUsedRegs = 0;
		static int totalUsedRegs = 0;
		static int numAnalyzings = 0;
		for (int i = 0; i < MIPS_NUM_GPRS; i++) {
			if (results.r[i].used) {
				numUsedRegs++;
			}
		}
		totalUsedRegs += numUsedRegs;
		numAnalyzings++;
		VERBOSE_LOG(CPU, "[ %08x ] Used regs: %i Average: %f", address, numUsedRegs, (float)totalUsedRegs / (float)numAnalyzings);

		return results;
	}
	
	void Reset()	{
		functions.clear();
		hashToFunction.clear();
	}

	void UpdateHashToFunctionMap() {
		hashToFunction.clear();
		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			AnalyzedFunction &f = *iter;
			if (f.hasHash && f.size > 16) {
				hashToFunction[f.hash].push_back(&f);
			}
		}
	}

	// Look forwards to find if a register is used again in this block.
	// Don't think we use this yet.
	bool IsRegisterUsed(MIPSGPReg reg, u32 addr) {
		while (true) {
			MIPSOpcode op = Memory::Read_Instruction(addr);
			MIPSInfo info = MIPSGetInfo(op);
			if ((info & IN_RS) && (MIPS_GET_RS(op) == reg))
				return true;
			if ((info & IN_RT) && (MIPS_GET_RT(op) == reg))
				return true;
			if ((info & IS_CONDBRANCH))
				return true; // could also follow both paths
			if ((info & IS_JUMP))
				return true; // could also follow the path
			if ((info & OUT_RT) && (MIPS_GET_RT(op) == reg))
				return false; //the reg got clobbed! yay!
			if ((info & OUT_RD) && (MIPS_GET_RD(op) == reg))
				return false; //the reg got clobbed! yay!
			if ((info & OUT_RA) && (reg == MIPS_REG_RA))
				return false; //the reg got clobbed! yay!
			addr += 4;
		}
		return true;
	}

	void HashFunctions() {
		std::vector<u32> buffer;

		for (auto iter = functions.begin(), end = functions.end(); iter != end; iter++) {
			AnalyzedFunction &f = *iter;

			// This is unfortunate.  In case of emuhacks or relocs, we have to make a copy.
			buffer.resize((f.end - f.start + 4) / 4);
			size_t pos = 0;
			for (u32 addr = f.start; addr <= f.end; addr += 4) {
				u32 validbits = 0xFFFFFFFF;
				MIPSOpcode instr = Memory::Read_Instruction(addr, true);
				if (MIPS_IS_EMUHACK(instr)) {
					f.hasHash = false;
					goto skip;
				}

				MIPSInfo flags = MIPSGetInfo(instr);
				if (flags & IN_IMM16)
					validbits &= ~0xFFFF;
				if (flags & IN_IMM26)
					validbits &= ~0x03FFFFFF;
				buffer[pos++] = instr & validbits;
			}

			f.hash = CityHash64((const char *) &buffer[0], buffer.size() * sizeof(u32));
			f.hasHash = true;
skip:
			;
		}
	}

	static const char *DefaultFunctionName(char buffer[256], u32 startAddr) {
		sprintf(buffer, "z_un_%08x", startAddr);
		return buffer;
	}

	static bool IsDefaultFunction(const char *name) {
		if (name == NULL) {
			// Must be I guess?
			return true;
		}

		// Assume any z_un, not just the address, is a default func.
		return !strncmp(name, "z_un_", strlen("z_un_")) || !strncmp(name, "u_un_", strlen("u_un_"));
	}

	static bool IsDefaultFunction(const std::string &name) {
		if (name.empty()) {
			// Must be I guess?
			return true;
		}

		return IsDefaultFunction(name.c_str());
	}

	static u32 ScanAheadForJumpback(u32 fromAddr, u32 knownStart, u32 knownEnd) {
		static const u32 MAX_AHEAD_SCAN = 0x1000;
		// Maybe a bit high... just to make sure we don't get confused by recursive tail recursion.
		static const u32 MAX_FUNC_SIZE = 0x20000;

		if (fromAddr > knownEnd + MAX_FUNC_SIZE) {
			return INVALIDTARGET;
		}

		// Code might jump halfway up to before fromAddr, but after knownEnd.
		// In that area, there could be another jump up to the valid range.
		// So we track that for a second scan.
		u32 closestJumpbackAddr = INVALIDTARGET;
		u32 closestJumpbackTarget = fromAddr;

		// We assume the furthest jumpback is within the func.
		u32 furthestJumpbackAddr = INVALIDTARGET;

		for (u32 ahead = fromAddr; ahead < fromAddr + MAX_AHEAD_SCAN; ahead += 4) {
			MIPSOpcode aheadOp = Memory::Read_Instruction(ahead);
			u32 target = GetBranchTargetNoRA(ahead, aheadOp);
			if (target == INVALIDTARGET && ((aheadOp & 0xFC000000) == 0x08000000)) {
				target = GetJumpTarget(ahead);
			}

			if (target != INVALIDTARGET) {
				// Only if it comes back up to known code within this func.
				if (target >= knownStart && target <= knownEnd) {
					furthestJumpbackAddr = ahead;
				}
				// But if it jumps above fromAddr, we should scan that area too...
				if (target < closestJumpbackTarget && target < fromAddr && target > knownEnd) {
					closestJumpbackAddr = ahead;
					closestJumpbackTarget = target;
				}
			}
		}

		if (closestJumpbackAddr != INVALIDTARGET && furthestJumpbackAddr == INVALIDTARGET) {
			for (u32 behind = closestJumpbackTarget; behind < fromAddr; behind += 4) {
				MIPSOpcode behindOp = Memory::Read_Instruction(behind);
				u32 target = GetBranchTargetNoRA(behind, behindOp);
				if (target == INVALIDTARGET && ((behindOp & 0xFC000000) == 0x08000000)) {
					target = GetJumpTarget(behind);
				}

				if (target != INVALIDTARGET) {
					if (target >= knownStart && target <= knownEnd) {
						furthestJumpbackAddr = closestJumpbackAddr;
					}
				}
			}
		}

		return furthestJumpbackAddr;
	}

	void ScanForFunctions(u32 startAddr, u32 endAddr, bool insertSymbols) {
		AnalyzedFunction currentFunction = {startAddr};

		u32 furthestBranch = 0;
		bool looking = false;
		bool end = false;
		bool isStraightLeaf = true;

		u32 addr;
		u32 addrNextSym = 0;
		for (addr = startAddr; addr <= endAddr; addr += 4) {
			// Use pre-existing symbol map info if available. May be more reliable.
			SymbolInfo syminfo;
			if (addrNextSym <= addr) {
				addrNextSym = symbolMap.FindPossibleFunctionAtAfter(addr);
			}
			if (addrNextSym <= addr && symbolMap.GetSymbolInfo(&syminfo, addr, ST_FUNCTION)) {
				addr = syminfo.address + syminfo.size - 4;

				// We still need to insert the func for hashing purposes.
				currentFunction.start = syminfo.address;
				currentFunction.end = syminfo.address + syminfo.size - 4;
				currentFunction.foundInSymbolMap = true;
				functions.push_back(currentFunction);
				currentFunction.foundInSymbolMap = false;
				currentFunction.start = addr + 4;
				furthestBranch = 0;
				looking = false;
				end = false;
				continue;
			}

			MIPSOpcode op = Memory::Read_Instruction(addr);
			u32 target = GetBranchTargetNoRA(addr, op);
			if (target != INVALIDTARGET) {
				isStraightLeaf = false;
				if (target > furthestBranch) {
					furthestBranch = target;
				}
			} else if ((op & 0xFC000000) == 0x08000000) {
				u32 sureTarget = GetJumpTarget(addr);
				// Check for a tail call.  Might not even have a jr ra.
				if (sureTarget != INVALIDTARGET && sureTarget < currentFunction.start) {
					if (furthestBranch > addr) {
						looking = true;
						addr += 4;
					} else {
						end = true;
					}
				} else if (sureTarget != INVALIDTARGET && sureTarget > addr && sureTarget > furthestBranch) {
					// A jump later.  Probably tail, but let's check if it jumps back.
					u32 knownEnd = furthestBranch == 0 ? addr : furthestBranch;
					u32 jumpback = ScanAheadForJumpback(sureTarget, currentFunction.start, knownEnd);
					if (jumpback != INVALIDTARGET && jumpback > addr && jumpback > knownEnd) {
						furthestBranch = jumpback;
					} else {
						if (furthestBranch > addr) {
							looking = true;
							addr += 4;
						} else {
							end = true;
						}
					}
				}
			}
			if (op == MIPS_MAKE_JR_RA()) {
				// If a branch goes to the jr ra, it's still ending here.
				if (furthestBranch > addr) {
					looking = true;
					addr += 4;
				} else {
					end = true;
				}
			}

			if (looking) {
				if (addr >= furthestBranch) {
					u32 sureTarget = GetSureBranchTarget(addr);
					// Regular j only, jals are to new funcs.
					if (sureTarget == INVALIDTARGET && ((op & 0xFC000000) == 0x08000000)) {
						sureTarget = GetJumpTarget(addr);
					}

					if (sureTarget != INVALIDTARGET && sureTarget < addr) {
						end = true;
					} else if (sureTarget != INVALIDTARGET) {
						// Okay, we have a downward jump.  Might be an else or a tail call...
						// If there's a jump back upward in spitting distance of it, it's an else.
						u32 knownEnd = furthestBranch == 0 ? addr : furthestBranch;
						u32 jumpback = ScanAheadForJumpback(sureTarget, currentFunction.start, knownEnd);
						if (jumpback != INVALIDTARGET && jumpback > addr && jumpback > knownEnd) {
							furthestBranch = jumpback;
						}
					}
				}
			}

			if (end) {
				currentFunction.end = addr + 4;
				currentFunction.isStraightLeaf = isStraightLeaf;
				functions.push_back(currentFunction);
				furthestBranch = 0;
				addr += 4;
				looking = false;
				end = false;
				isStraightLeaf = true;
				currentFunction.start = addr+4;
			}
		}

		currentFunction.end = addr + 4;
		functions.push_back(currentFunction);

		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			iter->size = iter->end - iter->start + 4;
			if (insertSymbols && !iter->foundInSymbolMap) {
				char temp[256];
				symbolMap.AddFunction(DefaultFunctionName(temp, iter->start), iter->start, iter->end - iter->start + 4);
			}
		}

		HashFunctions();

		std::string hashMapFilename = GetSysDirectory(DIRECTORY_SYSTEM) + "knownfuncs.ini";
		if (g_Config.bFuncHashMap) {
			LoadHashMap(hashMapFilename);
			StoreHashMap(hashMapFilename);
			if (insertSymbols) {
				ApplyHashMap();
			}
			if (g_Config.bFuncHashMap) {
				ReplaceFunctions();
			}
		}
	}

	void RegisterFunction(u32 startAddr, u32 size, const char *name) {
		// Check if we have this already
		for (auto iter = functions.begin(); iter != functions.end(); iter++) {
			if (iter->start == startAddr) {
				// Let's just add it to the hashmap.
				if (iter->hasHash && size > 16) {
					HashMapFunc hfun;
					hfun.hash = iter->hash;
					strncpy(hfun.name, name, 64);
					hfun.name[63] = 0;
					hfun.size = size;
					hashMap.insert(hfun);
					return;
				} else if (!iter->hasHash || size == 0) {
					ERROR_LOG(HLE, "%s: %08x %08x : match but no hash (%i) or no size", name, startAddr, size, iter->hasHash);
				}
			}
		}

		// Cheats a little.
		AnalyzedFunction fun;
		fun.start = startAddr;
		fun.end = startAddr + size - 4;
		fun.isStraightLeaf = false;  // dunno really
		strncpy(fun.name, name, 64);
		fun.name[63] = 0;
		functions.push_back(fun);

		HashFunctions();
	}

	void ForgetFunctions(u32 startAddr, u32 endAddr) {
		// It makes sense to forget functions as modules are unloaded but it breaks
		// the easy way of saving a hashmap by unloading and loading a game. I added
		// an alternative way.

		// TODO: speedup
		auto iter = functions.begin();
		while (iter != functions.end()) {
			if (iter->start >= startAddr && iter->start <= endAddr) {
				iter = functions.erase(iter);
			} else {
				iter++;
			}
		}

		RestoreReplacedInstructions(startAddr, endAddr);

		// TODO: Also wipe them from hash->function map
	}

	void ReplaceFunctions() {
		for (size_t i = 0; i < functions.size(); i++) {
			WriteReplaceInstruction(functions[i].start, functions[i].hash, functions[i].size);
		}
	}

	void UpdateHashMap() {
		for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
			const AnalyzedFunction &f = *it;
			// Small functions aren't very interesting.
			if (!f.hasHash || f.size <= 16) {
				continue;
			}
			// Functions with default names aren't very interesting either.
			const std::string name = symbolMap.GetLabelString(f.start);
			if (IsDefaultFunction(name)) {
				continue;
			}

			HashMapFunc mf = { "", f.hash, f.size };
			strncpy(mf.name, name.c_str(), sizeof(mf.name) - 1);
			hashMap.insert(mf);
		}
	}

	const char *LookupHash(u64 hash, int funcsize) {
		for (auto it = hashMap.begin(), end = hashMap.end(); it != end; ++it) {
			if (it->hash == hash && (int)it->size == funcsize) {
				return it->name;
			}
		}
		return 0;
	}

	void SetHashMapFilename(std::string filename) {
		if (filename.empty())
			hashmapFileName = GetSysDirectory(DIRECTORY_SYSTEM) + "knownfuncs.ini";
		else
			hashmapFileName = filename;
	}

	void StoreHashMap(std::string filename) {
		if (filename.empty())
			filename = hashmapFileName;

		UpdateHashMap();
		if (hashMap.empty()) {
			return;
		}

		FILE *file = File::OpenCFile(filename, "wt");
		if (!file) {
			WARN_LOG(LOADER, "Could not store hash map: %s", filename.c_str());
			return;
		}

		for (auto it = hashMap.begin(), end = hashMap.end(); it != end; ++it) {
			const HashMapFunc &mf = *it;
			if (fprintf(file, "%016llx:%d = %s\n", mf.hash, mf.size, mf.name) <= 0) {
				WARN_LOG(LOADER, "Could not store hash map: %s", filename.c_str());
				break;
			}
		}
		fclose(file);
	}

	void ApplyHashMap() {
		UpdateHashToFunctionMap();

		for (auto mf = hashMap.begin(), end = hashMap.end(); mf != end; ++mf) {
			auto iter = hashToFunction.find(mf->hash);
			if (iter == hashToFunction.end()) {
				continue;
			}

			// Yay, found a function.

			for (unsigned int i = 0; i < iter->second.size(); i++) {
				AnalyzedFunction &f = *(iter->second[i]);
				if (f.hash == mf->hash && f.size == mf->size) {
					strncpy(f.name, mf->name, sizeof(mf->name) - 1);

					std::string existingLabel = symbolMap.GetLabelString(f.start);
					char defaultLabel[256];
					// If it was renamed, keep it.  Only change the name if it's still the default.
					if (existingLabel.empty() || existingLabel == DefaultFunctionName(defaultLabel, f.start)) {
						symbolMap.SetLabelName(mf->name, f.start);
					}
				}
			}
		}
	}

	void LoadHashMap(std::string filename) {
		FILE *file = File::OpenCFile(filename, "rt");
		if (!file) {
			WARN_LOG(LOADER, "Could not load hash map: %s", filename.c_str());
			return;
		}
		hashmapFileName = filename;

		while (!feof(file)) {
			HashMapFunc mf = { "" };
			if (fscanf(file, "%llx:%d = %63s\n", &mf.hash, &mf.size, mf.name) < 3) {
				char temp[1024];
				fgets(temp, 1024, file);
				continue;
			}

			hashMap.insert(mf);
		}
		fclose(file);
	}

	std::vector<MIPSGPReg> GetInputRegs(MIPSOpcode op) {
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if (info & IN_RS) vec.push_back(MIPS_GET_RS(op));
		if (info & IN_RT) vec.push_back(MIPS_GET_RT(op));
		return vec;
	}

	std::vector<MIPSGPReg> GetOutputRegs(MIPSOpcode op) {
		std::vector<MIPSGPReg> vec;
		MIPSInfo info = MIPSGetInfo(op);
		if (info & OUT_RD) vec.push_back(MIPS_GET_RD(op));
		if (info & OUT_RT) vec.push_back(MIPS_GET_RT(op));
		if (info & OUT_RA) vec.push_back(MIPS_REG_RA);
		return vec;
	}

	MipsOpcodeInfo GetOpcodeInfo(DebugInterface* cpu, u32 address) {
		MipsOpcodeInfo info;
		memset(&info, 0, sizeof(info));

		if (!Memory::IsValidAddress(address)) {
			return info;
		}

		info.cpu = cpu;
		info.opcodeAddress = address;
		info.encodedOpcode = Memory::Read_Instruction(address);

		MIPSOpcode op = info.encodedOpcode;
		MIPSInfo opInfo = MIPSGetInfo(op);
		info.isLikelyBranch = (opInfo & LIKELY) != 0;

		// gather relevant address for alu operations
		// that's usually the value of the dest register
		switch (MIPS_GET_OP(op)) {
		case 0:		// special
			switch (MIPS_GET_FUNC(op)) {
			case 0x20:	// add
			case 0x21:	// addu
				info.hasRelevantAddress = true;
				info.releventAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))+cpu->GetRegValue(0,MIPS_GET_RT(op));
				break;
			case 0x22:	// sub
			case 0x23:	// subu
				info.hasRelevantAddress = true;
				info.releventAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))-cpu->GetRegValue(0,MIPS_GET_RT(op));
				break;
			}
			break;
		case 0x08:	// addi
		case 0x09:	// adiu
			info.hasRelevantAddress = true;
			info.releventAddress = cpu->GetRegValue(0,MIPS_GET_RS(op))+((s16)(op & 0xFFFF));
			break;
		}

		//j , jal, ...
		if (opInfo & IS_JUMP) {
			info.isBranch = true;
			if ((opInfo & OUT_RA) || (opInfo & OUT_RD)) {	// link
				info.isLinkedBranch = true;
			}

			if (opInfo & IN_RS) { // to register
				info.isBranchToRegister = true;
				info.branchRegisterNum = (int)MIPS_GET_RS(op);
				info.branchTarget = cpu->GetRegValue(0,info.branchRegisterNum);
			} else {				// to immediate
				info.branchTarget = GetJumpTarget(address);
			}
		}

		// movn, movz
		if (opInfo & IS_CONDMOVE) {
			info.isConditional = true;

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			switch (opInfo & CONDTYPE_MASK) {
			case CONDTYPE_EQ:
				info.conditionMet = (rt == 0);
				break;
			case CONDTYPE_NE:
				info.conditionMet = (rt != 0);
				break;
			}
		}

		// beq, bgtz, ...
		if (opInfo & IS_CONDBRANCH) {
			info.isBranch = true;
			info.isConditional = true;
			info.branchTarget = GetBranchTarget(address);

			if (opInfo & OUT_RA) {  // link
				info.isLinkedBranch = true;
			}

			u32 rt = cpu->GetRegValue(0, (int)MIPS_GET_RT(op));
			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			switch (opInfo & CONDTYPE_MASK) {
			case CONDTYPE_EQ:
				if (opInfo & IN_FPUFLAG) {	// fpu branch
					info.conditionMet = currentMIPS->fpcond == 0;
				} else {
					info.conditionMet = (rt == rs);
					if (MIPS_GET_RT(op) == MIPS_GET_RS(op))	{	// always true
						info.isConditional = false;
					}
				}
				break;
			case CONDTYPE_NE:
				if (opInfo & IN_FPUFLAG) {	// fpu branch
					info.conditionMet = currentMIPS->fpcond != 0;
				} else {
					info.conditionMet = (rt != rs);
					if (MIPS_GET_RT(op) == MIPS_GET_RS(op))	{	// always true
						info.isConditional = false;
					}
				}
				break;
			case CONDTYPE_LEZ:
				info.conditionMet = (((s32)rs) <= 0);
				break;
			case CONDTYPE_GTZ:
				info.conditionMet = (((s32)rs) > 0);
				break;
			case CONDTYPE_LTZ:
				info.conditionMet = (((s32)rs) < 0);
				break;
			case CONDTYPE_GEZ:
				info.conditionMet = (((s32)rs) >= 0);
				break;
			}
		}

		// lw, sh, ...
		if ((opInfo & IN_MEM) || (opInfo & OUT_MEM)) {
			info.isDataAccess = true;
			switch (opInfo & MEMTYPE_MASK) {
			case MEMTYPE_BYTE:
				info.dataSize = 1;
				break;
			case MEMTYPE_HWORD:
				info.dataSize = 2;
				break;
			case MEMTYPE_WORD:
			case MEMTYPE_FLOAT:
				info.dataSize = 4;
				break;

			case MEMTYPE_VQUAD:
				info.dataSize = 16;
			}

			u32 rs = cpu->GetRegValue(0, (int)MIPS_GET_RS(op));
			s16 imm16 = op & 0xFFFF;
			info.dataAddress = rs + imm16;

			info.hasRelevantAddress = true;
			info.releventAddress = info.dataAddress;
		}

		return info;
	}
}
