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

#pragma once

// IRImmRegCache is only to perform pre-constant folding. This is worth it to get cleaner
// IR.

#include "Common/BitSet.h"
#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRAnalysis.h"


// Have to account for all of them due to temps, etc.
constexpr int TOTAL_MAPPABLE_IRREGS = 256;
// Arbitrary - increase if your backend has more.
constexpr int TOTAL_POSSIBLE_NATIVEREGS = 128;

typedef int8_t IRNativeReg;  // invalid value is -1

constexpr IRReg IRREG_INVALID = 255;

// A set of IRRegs, to visit just the ones in it, in order.
struct IRRegSet {
	uint32_t bits[TOTAL_MAPPABLE_IRREGS / 32]{};

	void Set(int r) {
		bits[r >> 5] |= 1U << (r & 31);
	}
	// Calls f(reg) for each one in [first, end), lowest first.
	template <typename F>
	void ForEach(int first, int end, F f) const {
		for (int w = first >> 5; w < TOTAL_MAPPABLE_IRREGS / 32 && w * 32 < end; ++w) {
			uint32_t word = bits[w];
			if (w * 32 < first)
				word &= ~0U << (first - w * 32);
			if (end - w * 32 < 32)
				word &= (1U << (end - w * 32)) - 1;
			while (word != 0) {
				const int b = LeastSignificantSetBit(word);
				word &= word - 1;
				f((IRReg)(w * 32 + b));
			}
		}
	}
};

class IRWriter;
class MIPSState;

namespace MIPSComp {
struct JitOptions;
}

// Transient
class IRImmRegCache {
public:
	IRImmRegCache(IRWriter *ir);

	void SetImm(IRReg r, u32 immVal) {
		// Setting the value it already holds doesn't need writing again.
		if (!IsImm(r) || immVal_[r] != immVal)
			ClearBit(isWritten_, r);
		SetBit(isImm_, r);
		immVal_[r] = immVal;
	}

	bool IsImm(IRReg r) const { return TestBit(isImm_, r); }
	u32 GetImm(IRReg r) const { return immVal_[r]; }

	// Writes out all pending values. With keepKnown, the values stay known afterward (for a
	// conditional exit), otherwise they're forgotten (anything may change them after a barrier).
	void FlushAll(bool keepKnown = false);

	// The Map functions write out the values of the regs read, which stay known. A dirty reg
	// is forgotten after its inputs are written.
	void MapDirty(IRReg rd);
	void MapIn(IRReg rd);
	void MapInIn(IRReg rs, IRReg rt);
	void MapInInIn(IRReg rd, IRReg rs, IRReg rt);
	void MapDirtyIn(IRReg rd, IRReg rs);
	void MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt);

private:
	void Flush(IRReg rd);
	void Discard(IRReg rd);

	// Bit masks, one bit per reg: cheap to clear for each block, and FlushAll finds the set ones quickly.
	static constexpr int MASK_WORDS = TOTAL_MAPPABLE_IRREGS / 32;
	static bool TestBit(const u32 *mask, IRReg r) { return (mask[r >> 5] >> (r & 31)) & 1; }
	static void SetBit(u32 *mask, IRReg r) { mask[r >> 5] |= 1U << (r & 31); }
	static void ClearBit(u32 *mask, IRReg r) { mask[r >> 5] &= ~(1U << (r & 31)); }

	u32 isImm_[MASK_WORDS];
	// Whether the known value has been written to the reg.
	u32 isWritten_[MASK_WORDS];
	uint32_t immVal_[TOTAL_MAPPABLE_IRREGS];
	IRWriter *ir_;
};

// Initing is the default so the flag is reversed.
// 8 bits - upper 4 are reserved for backend purposes.
enum class MIPSMap : uint8_t {
	INIT = 0,
	DIRTY = 1,
	NOINIT = 2 | DIRTY,

	BACKEND_MASK = 0xF0,
};
static inline MIPSMap operator |(const MIPSMap &lhs, const MIPSMap &rhs) {
	return MIPSMap((uint8_t)lhs | (uint8_t)rhs);
}
static inline MIPSMap operator &(const MIPSMap &lhs, const MIPSMap &rhs) {
	return MIPSMap((uint8_t)lhs & (uint8_t)rhs);
}

struct IRInst;

class IRNativeRegCacheBase {
protected:
	enum class MIPSLoc : uint8_t {
		// Known immediate value (only in regcache.)
		IMM,
		// In a general reg.
		REG,
		// In a general reg, but an adjusted pointer (not pointerified - unaligned.)
		REG_AS_PTR,
		// In a general reg, but also has a known immediate value.
		REG_IMM,
		// In a native floating-point reg.
		FREG,
		// In a native vector reg.  Note: if FREGs and VREGS overlap, just use FREG.
		VREG,
		// Away in memory (in the mips context struct.)
		MEM,
	};

	// Kept small, since Start() copies all 256 for each block.
	struct RegStatusMIPS {
		// Where is this IR/MIPS register?  Note: base reg if vector.
		MIPSLoc loc = MIPSLoc::MEM;
		// If in a register, what index (into nr array)?
		IRNativeReg nReg = -1;
		// If in a multipart reg (vector or HI/LO), which lane?
		int8_t lane = -1;
		// Whether this reg is statically allocated.
		bool isStatic = false;
		// If a known immediate value, what value?
		uint32_t imm = 0;
		// Locked from spilling (i.e. used by current instruction) as of what IR instruction?
		int spillLockIRIndex = -1;
	};
	static_assert(sizeof(RegStatusMIPS) == 12, "RegStatusMIPS grew");
	// Ordered to avoid padding, since Start() copies these for each block too.
	struct RegStatusNative {
		// Which IR/MIPS reg is this currently holding?
		IRReg mipsReg = IRREG_INVALID;
		// Should the register be written back?
		bool isDirty = false;
		// Upper part of the register is used for "pointerification".
		// Depending on backend, this may not be used or some/all operations may work on the lower 32 bits.
		bool pointerified = false;
		// Upper part of the register has a normalized form (i.e. zero or sign extend.)
		// Which this means or if it matters depends on the backend.
		bool normalized32 = false;
		// Locked either as temp or direct reg as of what IR instruction?
		int tempLockIRIndex = -1;
	};
	static_assert(sizeof(RegStatusNative) == 8, "RegStatusNative grew");

	struct StaticAllocation {
		IRReg mr;
		IRNativeReg nr;
		// Register type.
		MIPSLoc loc;
		// Whether the reg should be marked pointerified by default.
		bool pointerified = false;
		// Whether the reg should be considered always normalized at the start of a block.
		bool normalized32 = false;
	};

public:
	IRNativeRegCacheBase(MIPSComp::JitOptions *jo);
	virtual ~IRNativeRegCacheBase() {}

	virtual void Start(u32 startPC, const IRInst *instructions, int count);
	// The guest memory base of the CPU the code is for, where pointerified registers point.
	void SetMemoryBase(const u8 *base) {
		membase_ = base;
	}
	void SetIRIndex(int index) {
		irIndex_ = index;
	}
	// For a path that leaves the block (like reporting a bad address): flushes everything there, then
	// puts the cache back as it was, for the code after that path.
	void FlushAllOnSidePath();

	bool IsGPRInRAM(IRReg gpr);
	bool IsFPRInRAM(IRReg fpr);
	bool IsGPRMapped(IRReg gpr);
	bool IsFPRMapped(IRReg fpr);
	bool IsGPRMappedAsPointer(IRReg gpr);
	bool IsGPRMappedAsStaticPointer(IRReg gpr);
	int GetFPRLane(IRReg fpr);
	int GetFPRLaneCount(IRReg fpr);

	bool IsGPRImm(IRReg gpr);
	bool IsGPR2Imm(IRReg base);
	uint32_t GetGPRImm(IRReg gpr);
	uint64_t GetGPR2Imm(IRReg first);
	void SetGPRImm(IRReg gpr, uint32_t immval);
	void SetGPR2Imm(IRReg first, uint64_t immval);

	// Protect the native registers containing register froms spilling, to ensure that
	// it's being kept allocated.
	void SpillLockGPR(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);
	void SpillLockFPR(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);
	void ReleaseSpillLockGPR(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);
	void ReleaseSpillLockFPR(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);

	void MarkGPRDirty(IRReg gpr, bool andNormalized32 = false);
	void MarkGPRAsPointerDirty(IRReg gpr);

	bool IsGPRClobbered(IRReg gpr) const;
	bool IsFPRClobbered(IRReg gpr) const;

	struct Mapping {
		char type = '?';
		IRReg reg = IRREG_INVALID;
		uint8_t lanes = 1;
		MIPSMap flags = MIPSMap::INIT;
	};

	void Map(const IRInst &inst);
	void MapWithExtra(const IRInst &inst, std::vector<Mapping> extra);
	virtual void FlushAll(bool gprs = true, bool fprs = true);

protected:
	virtual void SetupInitialRegs();
	virtual const int *GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const = 0;
	virtual const StaticAllocation *GetStaticAllocations(int &count) const {
		count = 0;
		return nullptr;
	}

	IRNativeReg AllocateReg(MIPSLoc type, MIPSMap flags);
	IRNativeReg FindFreeReg(MIPSLoc type, MIPSMap flags) const;
	IRNativeReg FindBestToSpill(MIPSLoc type, MIPSMap flags, bool unusedOnly, bool *clobbered) const;
	virtual bool IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes);
	virtual void DiscardNativeReg(IRNativeReg nreg);
	virtual void FlushNativeReg(IRNativeReg nreg);
	virtual void DiscardReg(IRReg mreg);
	virtual void FlushReg(IRReg mreg);
	virtual void AdjustNativeRegAsPtr(IRNativeReg nreg, bool state);
	virtual void MapNativeReg(MIPSLoc type, IRNativeReg nreg, IRReg first, int lanes, MIPSMap flags);
	virtual bool TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags);
	virtual IRNativeReg MapNativeReg(MIPSLoc type, IRReg first, int lanes, MIPSMap flags);
	IRNativeReg MapNativeRegAsPointer(IRReg gpr);

	IRNativeReg MapWithTemp(const IRInst &inst, MIPSLoc type);

	void MappingFromInst(const IRInst &inst, Mapping mapping[3]);
	void ApplyMapping(const Mapping *mapping, int count);
	void CleanupMapping(const Mapping *mapping, int count);

	// Load data from memory (possibly multiple lanes) into a native reg.
	virtual void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) = 0;
	// Store data in a native reg back into memory.
	virtual void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) = 0;
	// Set a native reg to a specific integer value.
	virtual void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) = 0;
	// Store the imm value for a reg to memory (not currently in a native reg.)
	virtual void StoreRegValue(IRReg mreg, uint32_t imm) = 0;

	void SetSpillLockIRIndex(IRReg reg, IRReg reg2, IRReg reg3, IRReg reg4, int offset, int index);
	void SetSpillLockIRIndex(IRReg reg, int index);
	int GetMipsRegOffset(IRReg r);

	bool IsRegClobbered(MIPSLoc type, IRReg r) const;
	// Every reg that may be out of memory: in a native reg, or an IMM. Flushing and discarding only
	// put regs back in memory, so it stays a superset through a FlushAll.
	struct NotInMemoryRegs {
		IRRegSet regs;
		// The native regs that held an FPR (as the first lane), as bits by index into nr.
		uint32_t fprNativeRegs[TOTAL_POSSIBLE_NATIVEREGS / 32]{};
	};
	NotInMemoryRegs NotInMemory() const;
	// At an exit, drops the temps nothing later in the block reads, so FlushAll doesn't store them.
	// regs: NotInMemory(), if already known.
	void DiscardDeadTempsAtExit(const NotInMemoryRegs *regs = nullptr);
	// FlushAll, after the dead temps are discarded, with regs from NotInMemory() from before.
	void FlushAllFrom(const NotInMemoryRegs &regs, bool gprs, bool fprs);
	bool IsRegRead(MIPSLoc type, IRReg r) const;
	IRUsage GetNextRegUsage(const IRSituation &info, MIPSLoc type, IRReg r) const;

	// Inline, since the register allocator asks all the time.
	bool IsValidGPR(IRReg r) const {
		// See MIPSState for these offsets.

		// Don't allow FPU regs, VFPU regs, or VFPU temps here.
		if (r >= 32 && IsValidFPR(r - 32))
			return false;
		// Don't allow nextPC, etc. since it's probably a mistake.
		if (r > IRREG_FPCOND && r != IRREG_LLBIT)
			return false;
		// Don't allow PC either.
		if (r == 241)
			return false;

		return true;
	}
	bool IsValidGPRNoZero(IRReg r) const {
		return IsValidGPR(r) && r != MIPS_REG_ZERO;
	}
	bool IsValidFPR(IRReg r) const {
		// FPR parameters are off by 32 within the MIPSState object.
		if (r >= TOTAL_MAPPABLE_IRREGS - 32)
			return false;

		// See MIPSState for these offsets.
		int index = r + 32;

		// Allow FPU or VFPU regs here.
		if (index >= 32 && index < 32 + 32 + 128)
			return true;
		// Also allow VFPU temps.
		if (index >= 224 && index < 224 + 16)
			return true;

		// Nothing else is allowed for the FPU side.
		return false;
	}

	MIPSComp::JitOptions *jo_;
	const u8 *membase_ = nullptr;
	// The block being compiled, and its IR.
	u32 irStartPC_ = 0;
	const IRInst *irInsts_ = nullptr;
	int irCount_ = 0;
	int irIndex_ = 0;
	// The exit DiscardDeadTempsAtExit last ran at.
	int deadTempsDiscardedIndex_ = -1;
	// Every reg made an IMM since Start(), for NotInMemory(). Only IMMs aren't in a native reg.
	IRRegSet immRegs_;

	struct {
		int totalNativeRegs = 0;
		bool mapFPUSIMD = false;
		bool mapUseVRegs = false;
	} config_;

	RegStatusNative nr[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mr[TOTAL_MAPPABLE_IRREGS];
	RegStatusNative nrInitial_[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mrInitial_[TOTAL_MAPPABLE_IRREGS];
	// For FlushAllOnSidePath().
	RegStatusNative nrSaved_[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mrSaved_[TOTAL_MAPPABLE_IRREGS];

	bool initialReady_ = false;
};
