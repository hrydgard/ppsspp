#include <cstring>
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"

void IRRegCache::Flush(int rd) {
	if (rd == 0) {
		return;
	}
	if (reg_[rd].isImm) {
		ir_->WriteSetConstant(rd, reg_[rd].immVal);
		reg_[rd].isImm = false;
	}
}

void IRRegCache::Discard(int rd) {
	if (rd == 0) {
		return;
	}
	reg_[rd].isImm = false;
}

IRRegCache::IRRegCache(IRWriter *ir) : ir_(ir) {
	memset(&reg_, 0, sizeof(reg_));
	reg_[0].isImm = true;
	ir_ = ir;
}

void IRRegCache::FlushAll() {
	for (int i = 0; i < TOTAL_MAPPABLE_MIPSREGS; i++) {
		//if (i < IRTEMP_0)
			Flush(i);
	}
}

void IRRegCache::MapIn(int rd) {
	Flush(rd);
}

void IRRegCache::MapDirty(int rd) {
	Discard(rd);
}

void IRRegCache::MapInIn(int rs, int rt) {
	Flush(rs);
	Flush(rt);
}

void IRRegCache::MapInInIn(int rd, int rs, int rt) {
	Flush(rd);
	Flush(rs);
	Flush(rt);
}

void IRRegCache::MapDirtyIn(int rd, int rs) {
	if (rs != rd) {
		Discard(rd);
	}
	Flush(rs);
}

void IRRegCache::MapDirtyInIn(int rd, int rs, int rt) {
	if (rs != rd && rt != rd) {
		Discard(rd);
	}
	Flush(rs);
	Flush(rt);
}

