#include <cstring>
#include "Common/Log.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"

void IRImmRegCache::Flush(IRReg rd) {
	if (rd == 0) {
		return;
	}
	if (reg_[rd].isImm) {
		_assert_((rd > 0 && rd < 32) || (rd >= IRTEMP_0 && rd < IRREG_VFPU_CTRL_BASE));
		ir_->WriteSetConstant(rd, reg_[rd].immVal);
		reg_[rd].isImm = false;
	}
}

void IRImmRegCache::Discard(IRReg rd) {
	if (rd == 0) {
		return;
	}
	reg_[rd].isImm = false;
}

IRImmRegCache::IRImmRegCache(IRWriter *ir) : ir_(ir) {
	memset(&reg_, 0, sizeof(reg_));
	reg_[0].isImm = true;
	ir_ = ir;
}

void IRImmRegCache::FlushAll() {
	for (int i = 0; i < TOTAL_MAPPABLE_MIPSREGS; i++) {
		Flush(i);
	}
}

void IRImmRegCache::MapIn(IRReg rd) {
	Flush(rd);
}

void IRImmRegCache::MapDirty(IRReg rd) {
	Discard(rd);
}

void IRImmRegCache::MapInIn(IRReg rs, IRReg rt) {
	Flush(rs);
	Flush(rt);
}

void IRImmRegCache::MapInInIn(IRReg rd, IRReg rs, IRReg rt) {
	Flush(rd);
	Flush(rs);
	Flush(rt);
}

void IRImmRegCache::MapDirtyIn(IRReg rd, IRReg rs) {
	if (rs != rd) {
		Discard(rd);
	}
	Flush(rs);
}

void IRImmRegCache::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt) {
	if (rs != rd && rt != rd) {
		Discard(rd);
	}
	Flush(rs);
	Flush(rt);
}

