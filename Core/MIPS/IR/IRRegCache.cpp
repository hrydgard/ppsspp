#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"

void IRRegCache::Dirty(MIPSGPReg rd) {
	if (rd == 0) {
		return;
	}
	if (reg_[rd].isImm) {
		ir_->WriteSetConstant(rd, reg_[rd].immVal);
		reg_[rd].isImm = false;
	}
}

void IRRegCache::MapIn(MIPSGPReg rd) {
	Dirty(rd);
}

void IRRegCache::MapInIn(MIPSGPReg rs, MIPSGPReg rt) {
	Dirty(rs);
	Dirty(rt);
}

void IRRegCache::MapDirty(MIPSGPReg rd) {
	Dirty(rd);
}

void IRRegCache::MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs) {
	Dirty(rd);
	Dirty(rs);
}

void IRRegCache::MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt) {
	Dirty(rd);
	Dirty(rs);
	Dirty(rt);
}

void IRRegCache::Start(IRWriter *ir) {
	memset(&reg_, 0, sizeof(reg_));
	reg_[0].isImm = true;
	ir_ = ir;
}

void IRRegCache::FlushAll() {

}
