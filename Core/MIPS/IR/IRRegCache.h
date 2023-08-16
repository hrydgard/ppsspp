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
#include "Core/MIPS/IR/IRInst.h"


// Have to account for all of them due to temps, etc.
constexpr int TOTAL_MAPPABLE_IRREGS = 256;
// Arbitrary - increase if your backend has more.
constexpr int TOTAL_POSSIBLE_NATIVEREGS = 128;

constexpr IRReg IRREG_INVALID = 255;

class IRWriter;
class MIPSState;

namespace MIPSComp {
class IRBlock;
struct JitOptions;
}

// Transient
class IRImmRegCache {
public:
	IRImmRegCache(IRWriter *ir);

	void SetImm(IRReg r, u32 immVal) {
		reg_[r].isImm = true;
		reg_[r].immVal = immVal;
	}

	bool IsImm(IRReg r) const { return reg_[r].isImm; }
	u32 GetImm(IRReg r) const { return reg_[r].immVal; }

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

	struct RegIR {
		bool isImm;
		u32 immVal;
	};

	RegIR reg_[TOTAL_MAPPABLE_IRREGS];
	IRWriter *ir_;
};

class IRNativeRegCache {
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
		// In a native vector reg.
		VREG,
		// Away in memory (in the mips context struct.)
		MEM,
	};

	struct RegStatusMIPS {
		// Where is this IR/MIPS register?
		MIPSLoc loc = MIPSLoc::MEM;
		// If in a register, what index (into nr array)?
		int8_t nReg = -1;
		// If a known immediate value, what value?
		uint64_t imm = 0;
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
		int8_t nr;
		// Whether the reg should be marked pointerified by default.
		bool pointerified = false;
		// Whether the reg should be considered always normalized at the start of a block.
		bool normalized32 = false;
	};

public:
	IRNativeRegCache(MIPSComp::JitOptions *jo);
	virtual ~IRNativeRegCache() {}

	virtual void Start(MIPSComp::IRBlock *irBlock);
	void SetIRIndex(int index) {
		irIndex_ = index;
	}

protected:
	virtual void SetupInitialRegs();
	virtual const StaticAllocation *GetStaticAllocations(int &count) {
		count = 0;
		return nullptr;
	}

	MIPSComp::JitOptions *jo_;
	MIPSComp::IRBlock *irBlock_ = nullptr;
	int irIndex_ = 0;
	int totalNativeRegs_ = 0;

	RegStatusNative nr[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mr[TOTAL_MAPPABLE_IRREGS];
	RegStatusNative nrInitial_[TOTAL_POSSIBLE_NATIVEREGS];
	RegStatusMIPS mrInitial_[TOTAL_MAPPABLE_IRREGS];

	bool initialReady_ = false;
	bool pendingFlush_ = false;
};
