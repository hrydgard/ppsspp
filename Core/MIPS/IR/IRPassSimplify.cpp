#include "Core/MIPS/IR/IRPassSimplify.h"

void SimplifyInPlace(IRInst *inst, int count, const u32 *constPool) {
	for (int i = 0; i < count; i++) {
		switch (inst[i].op) {
		case IROp::AddConst:
			if (constPool[inst[i].src2] == 0)
				inst[i].op = IROp::Mov;
			else if (inst[i].src1 == 0) {
				inst[i].op = IROp::SetConst;
				inst[i].src1 = inst[i].src2;
			}
			break;
		default:
			break;
		}
	}
}