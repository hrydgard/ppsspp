#include "Core/MIPS/IR/IRPassSimplify.h"

void SimplifyInPlace(IRInst *inst, int count, const u32 *constPool) {
	for (int i = 0; i < count; i++) {
		switch (inst[i].op) {
		case IROp::AddConst:
			if (constPool[inst[i].src2] == 0)
				inst[i].op = IROp::Mov;
			break;
		default:
			break;
		}
	}
}