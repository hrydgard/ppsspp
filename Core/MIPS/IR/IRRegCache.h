#pragma once

// IRRegCache is only to perform pre-constant folding. This is worth it to get cleaner
// IR.

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

enum {
	TOTAL_MAPPABLE_MIPSREGS = 256,
};

struct RegIR {
	bool isImm;
	u32 immVal;
};

class IRWriter;

class IRRegCache {
public:
	void SetImm(MIPSGPReg r, u32 immVal) {
		reg_[r].isImm = true;
		reg_[r].immVal = immVal;
	}

	bool IsImm(MIPSGPReg r) const { return reg_[r].isImm; }
	u32 GetImm(MIPSGPReg r) const { return reg_[r].immVal; }

	void MapIn(MIPSGPReg rd);
	void MapInIn(MIPSGPReg rs, MIPSGPReg rt);
	void MapDirty(MIPSGPReg rd);
	void MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs);
	void MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt);

	void Start(IRWriter *ir);
	void FlushAll();

private:
	void Dirty(MIPSGPReg rd);
	RegIR reg_[TOTAL_MAPPABLE_MIPSREGS];
	IRWriter *ir_;
};
