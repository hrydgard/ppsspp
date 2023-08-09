#pragma once

// IRImmRegCache is only to perform pre-constant folding. This is worth it to get cleaner
// IR.

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRInst.h"

enum {
	TOTAL_MAPPABLE_MIPSREGS = 256,
};

struct RegIR {
	bool isImm;
	u32 immVal;
};

class IRWriter;

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
	RegIR reg_[TOTAL_MAPPABLE_MIPSREGS];
	IRWriter *ir_;
};
