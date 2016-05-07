#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRRegCache.h"

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


u32 Evaluate(u32 a, u32 b, IROp op) {
	switch (op) {
	case IROp::Add: case IROp::AddConst: return a + b;
	case IROp::Sub: case IROp::SubConst: return a - b;
	case IROp::And: case IROp::AndConst: return a & b;
	case IROp::Or: case IROp::OrConst: return a | b;
	case IROp::Xor: case IROp::XorConst: return a ^ b;
	default:
		return -1;
	}
}

IROp ArithToArithConst(IROp op) {
	switch (op) {
	case IROp::Add: return IROp::AddConst;
	case IROp::Sub: return IROp::SubConst;
	case IROp::And: return IROp::AndConst;
	case IROp::Or: return IROp::OrConst;
	case IROp::Xor: return IROp::XorConst;
	default:
		return (IROp)-1;
	}
}


void PropagateConstants(const IRWriter &in, IRWriter &out) {
	IRRegCache gpr(&out);

	const u32 *constants = in.GetConstants().data();
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		bool symmetric = true;
		switch (inst.op) {
		case IROp::SetConst:
			gpr.SetImm((MIPSGPReg)inst.dest, constants[inst.src1]);
			break;

		case IROp::Sub:
			symmetric = false;  // fallthrough
		case IROp::Add:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
			if (gpr.IsImm(inst.src1) && gpr.IsImm(inst.src2)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), gpr.GetImm(inst.src2), inst.op));
			} else if (gpr.IsImm(inst.src2) && inst.src1 != inst.src2 && inst.dest != inst.src2) {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				if (gpr.GetImm(inst.src2) == 0 && (inst.op == IROp::Add || inst.op == IROp::Or)) {
					out.Write(IROp::Mov, inst.dest, inst.src1);
				} else {
					out.Write(ArithToArithConst(inst.op), inst.dest, inst.src1, out.AddConstant(gpr.GetImm(inst.src2)));
				}
			} else if (gpr.IsImm(inst.src1) && inst.src1 != inst.src2 && inst.dest != inst.src2 && symmetric) {
				gpr.MapDirtyIn(inst.dest, inst.src2);
				out.Write(ArithToArithConst(inst.op), inst.dest, inst.src2, out.AddConstant(gpr.GetImm(inst.src1)));
			} else {
				gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
				goto doDefault;
			}
			break;
		
		case IROp::AddConst:
		case IROp::SubConst:
		case IROp::AndConst:
		case IROp::OrConst:
		case IROp::XorConst:
			if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), constants[inst.src2], inst.op));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Mov:
			if (inst.src1 == inst.src2) {
				// Nop
			} else if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, gpr.GetImm(inst.src1));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
			// Just pass through, no excessive flushing
			gpr.MapInIn(inst.dest, inst.src1);
			goto doDefault;

		case IROp::Load8:
		case IROp::Load8Ext:
		case IROp::Load16:
		case IROp::Load16Ext:
		case IROp::Load32:
			gpr.MapDirtyIn(inst.dest, inst.src1);
			goto doDefault;

		case IROp::ExitToConst:
		case IROp::ExitToReg:
		case IROp::ExitToConstIfEq:
		case IROp::ExitToConstIfNeq:
		case IROp::ExitToConstIfFpFalse:
		case IROp::ExitToConstIfFpTrue:
		case IROp::ExitToConstIfGeZ:
		case IROp::ExitToConstIfGtZ:
		case IROp::ExitToConstIfLeZ:
		case IROp::ExitToConstIfLtZ:
		default:
		{
			gpr.FlushAll();
		doDefault:
			// Remap constants to the new reality
			const IRMeta *m = GetIRMeta(inst.op);
			switch (m->types[0]) {
			case 'C':
				inst.dest = out.AddConstant(constants[inst.dest]);
				break;
			}
			switch (m->types[1]) {
			case 'C':
				inst.src1 = out.AddConstant(constants[inst.src1]);
				break;
			}
			switch (m->types[2]) {
			case 'C':
				inst.src2 = out.AddConstant(constants[inst.src2]);
				break;
			}
			out.Write(inst);
			break;
		}
		}
	}
}