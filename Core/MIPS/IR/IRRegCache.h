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

// Transient
class IRRegCache {
public:
	IRRegCache(IRWriter *ir);

	void SetImm(int r, u32 immVal) {
		reg_[r].isImm = true;
		reg_[r].immVal = immVal;
	}

	bool IsImm(int r) const { return reg_[r].isImm; }
	u32 GetImm(int r) const { return reg_[r].immVal; }

	void FlushAll();

	void MapDirty(int rd);
	void MapIn(int rd);
	void MapInIn(int rs, int rt);
	void MapInInIn(int rd, int rs, int rt);
	void MapDirtyIn(int rd, int rs);
	void MapDirtyInIn(int rd, int rs, int rt);

private:
	void Flush(int rd);
	void Discard(int rd);
	RegIR reg_[TOTAL_MAPPABLE_MIPSREGS];
	IRWriter *ir_;
};
