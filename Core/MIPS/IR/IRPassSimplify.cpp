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
	case IROp::Shr: case IROp::ShrImm: return a >> b;
	case IROp::Sar: case IROp::SarImm: return (s32)a >> b;
	case IROp::Ror: case IROp::RorImm: return (a >> b) | (a << (32 - b));
	case IROp::Shl: case IROp::ShlImm: return a << b;
	case IROp::Slt: case IROp::SltConst: return ((s32)a < (s32)b);
	case IROp::SltU: case IROp::SltUConst: return (a < b);
	default:
		return -1;
	}
}

u32 Evaluate(u32 a, IROp op) {
	switch (op) {
	case IROp::Not: return ~a;
	case IROp::Neg: return -(s32)a;
	case IROp::BSwap16: return ((a & 0xFF00FF00) >> 8) | ((a & 0x00FF00FF) << 8);
	case IROp::BSwap32: return swap32(a);
	case IROp::Ext8to32: return (u32)(s32)(s8)(u8)a;
	case IROp::Ext16to32: return (u32)(s32)(s16)(u16)a;
	case IROp::Clz: {
		int x = 31;
		int count = 0;
		while (x >= 0 && !(a & (1 << x))) {
			count++;
			x--;
		}
		return count;
	}
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
	case IROp::Slt: return IROp::SltConst;
	case IROp::SltU: return IROp::SltUConst;
	default:
		return (IROp)-1;
	}
}

IROp ShiftToShiftImm(IROp op) {
	switch (op) {
	case IROp::Shl: return IROp::ShlImm;
	case IROp::Shr: return IROp::ShrImm;
	case IROp::Ror: return IROp::RorImm;
	case IROp::Sar: return IROp::SarImm;
	default:
		return (IROp)-1;
	}
}

bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out) {
	if (c == 1) {
		return passes[0](in, out);
	}

	bool logBlocks = false;

	IRWriter temp[2];
	const IRWriter *nextIn = &in;
	IRWriter *nextOut = &temp[1];
	for (size_t i = 0; i < c - 1; ++i) {
		if (passes[i](*nextIn, *nextOut)) {
			logBlocks = true;
		}

		temp[0] = std::move(temp[1]);
		nextIn = &temp[0];
	}

	if (passes[c - 1](*nextIn, out)) {
		logBlocks = true;
	}

	return logBlocks;
}

bool PropagateConstants(const IRWriter &in, IRWriter &out) {
	IRRegCache gpr(&out);

	const u32 *constants = in.GetConstants().data();
	bool logBlocks = false;
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		bool symmetric = true;
		if (out.GetConstants().size() > 128) {
			// Avoid causing a constant explosion.
			goto doDefaultAndFlush;
		}
		switch (inst.op) {
		case IROp::SetConst:
			gpr.SetImm(inst.dest, constants[inst.src1]);
			break;
		case IROp::SetConstF:
		case IROp::SetConstV:
			goto doDefault;

		case IROp::Sub:
		case IROp::Slt:
		case IROp::SltU:
			symmetric = false;  // fallthrough
		case IROp::Add:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
			if (gpr.IsImm(inst.src1) && gpr.IsImm(inst.src2)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), gpr.GetImm(inst.src2), inst.op));
			} else if (gpr.IsImm(inst.src2)) {
				const u32 imm2 = gpr.GetImm(inst.src2);
				gpr.MapDirtyIn(inst.dest, inst.src1);
				if (imm2 == 0 && (inst.op == IROp::Add || inst.op == IROp::Or)) {
					// Add / Or with zero is just a Mov.
					if (inst.dest != inst.src1)
						out.Write(IROp::Mov, inst.dest, inst.src1);
				} else {
					out.Write(ArithToArithConst(inst.op), inst.dest, inst.src1, out.AddConstant(imm2));
				}
			} else if (symmetric && gpr.IsImm(inst.src1)) {
				const u32 imm1 = gpr.GetImm(inst.src1);
				gpr.MapDirtyIn(inst.dest, inst.src2);
				if (imm1 == 0 && (inst.op == IROp::Add || inst.op == IROp::Or)) {
					// Add / Or with zero is just a Mov.
					if (inst.dest != inst.src2)
						out.Write(IROp::Mov, inst.dest, inst.src2);
				} else {
					out.Write(ArithToArithConst(inst.op), inst.dest, inst.src2, out.AddConstant(imm1));
				}
			} else {
				gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
				goto doDefault;
			}
			break;

		case IROp::Neg:
		case IROp::Not:
		case IROp::BSwap16:
		case IROp::BSwap32:
		case IROp::Ext8to32:
		case IROp::Ext16to32:
		case IROp::Clz:
			if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), inst.op));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::AddConst:
		case IROp::SubConst:
		case IROp::AndConst:
		case IROp::OrConst:
		case IROp::XorConst:
		case IROp::SltConst:
		case IROp::SltUConst:
			if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), constants[inst.src2], inst.op));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Shl:
		case IROp::Shr:
		case IROp::Ror:
		case IROp::Sar:
			if (gpr.IsImm(inst.src1) && gpr.IsImm(inst.src2)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), gpr.GetImm(inst.src2), inst.op));
			} else if (gpr.IsImm(inst.src2)) {
				const u8 sa = gpr.GetImm(inst.src2) & 31;
				gpr.MapDirtyIn(inst.dest, inst.src1);
				if (sa == 0) {
					if (inst.dest != inst.src1)
						out.Write(IROp::Mov, inst.dest, inst.src1);
				} else {
					out.Write(ShiftToShiftImm(inst.op), inst.dest, inst.src1, sa);
				}
			} else {
				gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
				goto doDefault;
			}
			break;

		case IROp::ShlImm:
		case IROp::ShrImm:
		case IROp::RorImm:
		case IROp::SarImm:
			if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), inst.src2, inst.op));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Mov:
			if (inst.dest == inst.src1) {
				// Nop
			} else if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, gpr.GetImm(inst.src1));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Mult:
		case IROp::MultU:
			gpr.MapInIn(inst.src1, inst.src2);
			goto doDefault;

		case IROp::MovZ:
		case IROp::MovNZ:
			gpr.MapInInIn(inst.dest, inst.src1, inst.src2);
			goto doDefault;

		case IROp::Min:
		case IROp::Max:
			gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
			goto doDefault;

		case IROp::FMovFromGPR:
			if (gpr.IsImm(inst.src1)) {
				out.Write(IROp::SetConstF, inst.dest, out.AddConstant(gpr.GetImm(inst.src1)));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;

		case IROp::VMovFromGPR:
			if (gpr.IsImm(inst.src1)) {
				out.Write(IROp::SetConstV, inst.dest, out.AddConstant(gpr.GetImm(inst.src1)));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;

		case IROp::FMovToGPR:
			gpr.MapDirty(inst.dest);
			goto doDefault;

		case IROp::VMovToGPR:
			gpr.MapDirty(inst.dest);
			goto doDefault;

		case IROp::MfHi:
		case IROp::MfLo:
			gpr.MapDirty(inst.dest);
			goto doDefault;

		case IROp::MtHi:
		case IROp::MtLo:
			gpr.MapIn(inst.src1);
			goto doDefault;

		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
			if (gpr.IsImm(inst.src1) && inst.src1 != inst.dest) {
				gpr.MapIn(inst.dest);
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + constants[inst.src2]));
			} else {
				gpr.MapInIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;
		case IROp::StoreFloat:
		case IROp::StoreFloatV:
			if (gpr.IsImm(inst.src1)) {
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + constants[inst.src2]));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Load8:
		case IROp::Load8Ext:
		case IROp::Load16:
		case IROp::Load16Ext:
		case IROp::Load32:
			if (gpr.IsImm(inst.src1) && inst.src1 != inst.dest) {
				gpr.MapDirty(inst.dest);
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + constants[inst.src2]));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;
		case IROp::LoadFloat:
		case IROp::LoadFloatV:
			if (gpr.IsImm(inst.src1)) {
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + constants[inst.src2]));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;

		case IROp::Downcount:
		case IROp::SetPCConst:
			goto doDefault;

		case IROp::SetPC:
			if (gpr.IsImm(inst.src1)) {
				out.Write(IROp::SetPCConst, out.AddConstant(gpr.GetImm(inst.src1)));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;

		// FP-only instructions don't need to flush immediates.
		case IROp::FAdd:
		case IROp::FMul:
		case IROp::FDiv:
		case IROp::FSub:
		case IROp::FNeg:
		case IROp::FAbs:
		case IROp::FSqrt:
		case IROp::FMov:
		case IROp::FRound:
		case IROp::FTrunc:
		case IROp::FCeil:
		case IROp::FFloor:
		case IROp::FCvtSW:
			out.Write(inst);
			break;

		case IROp::SetCtrlVFPU:
			goto doDefault;

		case IROp::FCvtWS:
			// TODO: Actually, this should just use the currently set rounding mode.
			// Move up with FCvtSW when that's implemented.
			gpr.MapIn(IRREG_FCR31);
			out.Write(inst);
			break;

		case IROp::FpCondToReg:
			if (gpr.IsImm(IRREG_FPCOND)) {
				gpr.SetImm(inst.dest, gpr.GetImm(IRREG_FPCOND));
			} else {
				gpr.MapDirtyIn(inst.dest, IRREG_FPCOND);
				out.Write(inst);
			}
			break;

		case IROp::ZeroFpCond:
		case IROp::FCmpUnordered:
		case IROp::FCmpEqual:
		case IROp::FCmpEqualUnordered:
		case IROp::FCmpLessOrdered:
		case IROp::FCmpLessUnordered:
		case IROp::FCmpLessEqualOrdered:
		case IROp::FCmpLessEqualUnordered:
			gpr.MapDirty(IRREG_FPCOND);
			goto doDefault;

		case IROp::RestoreRoundingMode:
		case IROp::ApplyRoundingMode:
		case IROp::UpdateRoundingMode:
			goto doDefault;

		case IROp::VfpuCtrlToReg:
			gpr.MapDirtyIn(inst.dest, IRREG_VPFU_CTRL_BASE + inst.src1);
			goto doDefault;

		case IROp::Syscall:
		case IROp::Interpret:
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
		doDefaultAndFlush:
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
	return logBlocks;
}