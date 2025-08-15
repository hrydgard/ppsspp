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

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/IR/IRInst.h"


// Have to account for all of them due to temps, etc.
constexpr int TOTAL_MAPPABLE_IRREGS = 256;
// Arbitrary - increase if your backend has more.
constexpr int TOTAL_POSSIBLE_NATIVEREGS = 128;

typedef int8_t IRNativeReg;  // invalid value is -1

constexpr IRReg IRREG_INVALID = 255;

class IRWriter;
class MIPSState;

namespace MIPSComp {
class IRBlock;
class IRBlockCache;
struct JitOptions;
}

// Transient
class IRImmRegCache {
public:
	IRImmRegCache(IRWriter *ir);

	void SetImm(IRReg r, u32 immVal) {
		isImm_[r] = true;
		immVal_[r] = immVal;
	}

	bool IsImm(IRReg r) const { return isImm_[r]; }
	u32 GetImm(IRReg r) const { return immVal_[r]; }

	void FlushAll();

	void MapDirty(IRReg rd);
	void MapIn(IRReg rd);
	void MapInIn(IRReg rs, IRReg rt);
	void MapInInIn(IRReg rd, IRReg rs, IRReg rt);
	void MapDirtyIn(IRReg rd, IRReg rs);
	void MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt);

private:
	void Flush(IRReg rd);
	void Discard(IRReg rd);

	bool isImm_[TOTAL_MAPPABLE_IRREGS];
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

class IRNativeRegCacheBase {
protected:
	enum class MIPSLoc {
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

	struct RegStatusMIPS {
		// Where is this IR/MIPS register?  Note: base reg if vector.
		MIPSLoc loc = MIPSLoc::MEM;
		// If in a register, what index (into nr array)?
		IRNativeReg nReg = -1;
		// If a known immediate value, what value?
		uint32_t imm = 0;
		// Locked from spilling (i.e. used by current instruction) as of what IR instruction?
		int spillLockIRIndex = -1;
		// If in a multipart reg (vector or HI/LO), which lane?
		int lane = -1;
		// Whether this reg is statically allocated.
		bool isStatic = false;
	};
	struct RegStatusNative {
		// Which IR/MIPS reg is this currently holding?
		IRReg mipsReg = IRREG_INVALID;
		// Locked either as temp or direct reg as of what IR instruction?
		int tempLockIRIndex = -1;
		// Should the register be written back?
		bool isDirty = false;
		// Upper part of the register is used for "pointerification".
		// Depending on backend, this may not be used or some/all operations may work on the lower 32 bits.
		bool pointerified = false;
		// Upper part of the register has a normalized form (i.e. zero or sign extend.)
		// Which this means or if it matters depends on the backend.
		bool normalized32 = false;
	};

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

	virtual void Start(MIPSComp::IRBlockCache *irBlockCache, int blockNum);
	void SetIRIndex(int index) {
		irIndex_ = index;
	}

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
	bool IsRegRead(MIPSLoc type, IRReg r) const;
	IRUsage GetNextRegUsage(const IRSituation &info, MIPSLoc type, IRReg r) const;

	bool IsValidGPR(IRReg r) const;
	bool IsValidGPRNoZero(IRReg r) const;
	bool IsValidFPR(IRReg r) const;

	MIPSComp::JitOptions *jo_;
	int irBlockNum_ = 0;
	const MIPSComp::IRBlock *irBlock_ = nullptr;
	const MIPSComp::IRBlockCache *irBlockCache_ = nullptr;
	int irIndex_ = 0;

	struct {
		int totalNativeRegs = 0;
		bool mapFPUSIMD = false;
		bool mapUseVRegs = false;
	} config_;

	RegStatusNative nr[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mr[TOTAL_MAPPABLE_IRREGS];
	RegStatusNative nrInitial_[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mrInitial_[TOTAL_MAPPABLE_IRREGS];

	bool initialReady_ = false;
};
