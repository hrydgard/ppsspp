#include <cstring>
#include "Common/Log.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"

void IRRegCache::Flush(IRReg rd) {
	if (rd == 0) {
		return;
	}
	if (reg_[rd].isImm) {
		_assert_((rd > 0 && rd < 32) || (rd >= IRTEMP_0 && rd < IRREG_VFPU_CTRL_BASE));
		ir_->WriteSetConstant(rd, reg_[rd].immVal);
		reg_[rd].isImm = false;
	}
}

void IRRegCache::Discard(IRReg rd) {
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
		Flush(i);
	}
}

void IRRegCache::MapIn(IRReg rd) {
	Flush(rd);
}

void IRRegCache::MapDirty(IRReg rd) {
	Discard(rd);
}

void IRRegCache::MapInIn(IRReg rs, IRReg rt) {
	Flush(rs);
	Flush(rt);
}

void IRRegCache::MapInInIn(IRReg rd, IRReg rs, IRReg rt) {
	Flush(rd);
	Flush(rs);
	Flush(rt);
}

void IRRegCache::MapDirtyIn(IRReg rd, IRReg rs) {
	if (rs != rd) {
		Discard(rd);
	}
	Flush(rs);
}

void IRRegCache::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt) {
	if (rs != rd && rt != rd) {
		Discard(rd);
	}
	Flush(rs);
	Flush(rt);
}

