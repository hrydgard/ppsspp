#include <algorithm>
#include <cstring>
#include <utility>

#include "Common/Log.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRRegCache.h"

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
	case IROp::ReverseBits: return ReverseBits32(a);
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

bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out, const IROptions &opts) {
	if (c == 1) {
		return passes[0](in, out, opts);
	}

	bool logBlocks = false;

	IRWriter temp[2];
	const IRWriter *nextIn = &in;
	IRWriter *nextOut = &temp[1];
	for (size_t i = 0; i < c - 1; ++i) {
		if (passes[i](*nextIn, *nextOut, opts)) {
			logBlocks = true;
		}

		temp[0] = std::move(temp[1]);
		nextIn = &temp[0];
	}

	if (passes[c - 1](*nextIn, out, opts)) {
		logBlocks = true;
	}

	return logBlocks;
}

bool OptimizeFPMoves(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	bool logBlocks = false;
	IRInst prev{ IROp::Nop };
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		switch (inst.op) {
		case IROp::FMovFromGPR:
			//FMovToGPR a0, f12
			//FMovFromGPR f14, a0
			// to
			//FMovToGPR a0, f12
			//FMov f14, f12
			if (prev.op == IROp::FMovToGPR && prev.dest == inst.src1) {
				inst.op = IROp::FMov;
				inst.src1 = prev.src1;
				out.Write(inst);
			} else {
				out.Write(inst);
			}
			break;

		// This will need to scan forward or keep track of more information to be useful.
		// Just doing one isn't.
		/*
		case IROp::LoadVec4:
			// AddConst a0, sp, 0x30
			// LoadVec4 v16, a0, 0x0
			// to
			// AddConst a0, sp, 0x30
			// LoadVec4 v16, sp, 0x30 
			if (prev.op == IROp::AddConst && prev.dest == inst.src1 && prev.dest != prev.src1 && prev.src1 == MIPS_REG_SP) {
				inst.constant += prev.constant;
				inst.src1 = prev.src1;
				logBlocks = 1;
			} else {
				goto doDefault;
			}
			out.Write(inst);
			break;
		*/
		default:
			out.Write(inst);
			break;
		}
		prev = inst;
	}
	return logBlocks;
}

// Might be useful later on x86.
bool ThreeOpToTwoOp(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	bool logBlocks = false;
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		const IRMeta *meta = GetIRMeta(inst.op);
		switch (inst.op) {
		case IROp::Sub:
		case IROp::Slt:
		case IROp::SltU:
		case IROp::Add:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
			if (inst.src1 != inst.dest && inst.src2 != inst.dest) {
				out.Write(IROp::Mov, inst.dest, inst.src1);
				out.Write(inst.op, inst.dest, inst.dest, inst.src2);
			} else {
				out.Write(inst);
			}
			break;
		case IROp::FMul:
		case IROp::FAdd:
			if (inst.src1 != inst.dest && inst.src2 != inst.dest) {
				out.Write(IROp::FMov, inst.dest, inst.src1);
				out.Write(inst.op, inst.dest, inst.dest, inst.src2);
			} else {
				out.Write(inst);
			}
			break;

		case IROp::Vec4Add:
		case IROp::Vec4Sub:
		case IROp::Vec4Mul:
		case IROp::Vec4Div:
			if (inst.src1 != inst.dest && inst.src2 != inst.dest) {
				out.Write(IROp::Vec4Mov, inst.dest, inst.src1);
				out.Write(inst.op, inst.dest, inst.dest, inst.src2);
			} else {
				out.Write(inst);
			}
			break;

		default:
			out.Write(inst);
			break;
		}
	}
	return logBlocks;
}

bool RemoveLoadStoreLeftRight(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; ++i) {
		const IRInst &inst = in.GetInstructions()[i];

		// TODO: Reorder or look ahead to combine?

		auto nextOp = [&]() -> const IRInst &{
			return in.GetInstructions()[i + 1];
		};

		auto combineOpposite = [&](IROp matchOp, int matchOff, IROp replaceOp, int replaceOff) {
			if (!opts.unalignedLoadStore || i + 1 >= n)
				return false;
			const IRInst &next = nextOp();
			if (next.op != matchOp || next.dest != inst.dest || next.src1 != inst.src1)
				return false;
			if (inst.constant + matchOff != next.constant)
				return false;

			// Write out one unaligned op.
			out.Write(replaceOp, inst.dest, inst.src1, out.AddConstant(inst.constant + replaceOff));
			// Skip the next one, replaced.
			i++;
			return true;
		};

		auto addCommonProlog = [&]() {
			// IRTEMP_LR_ADDR = rs + imm
			out.Write(IROp::AddConst, IRTEMP_LR_ADDR, inst.src1, out.AddConstant(inst.constant));
			// IRTEMP_LR_SHIFT = (addr & 3) * 8
			out.Write(IROp::AndConst, IRTEMP_LR_SHIFT, IRTEMP_LR_ADDR, out.AddConstant(3));
			out.Write(IROp::ShlImm, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, 3);
			// IRTEMP_LR_ADDR = addr & 0xfffffffc (for stores, later)
			out.Write(IROp::AndConst, IRTEMP_LR_ADDR, IRTEMP_LR_ADDR, out.AddConstant(0xFFFFFFFC));
			// IRTEMP_LR_VALUE = RAM(IRTEMP_LR_ADDR)
			out.Write(IROp::Load32, IRTEMP_LR_VALUE, IRTEMP_LR_ADDR, out.AddConstant(0));
		};
		auto addCommonStore = [&](int off = 0) {
			// RAM(IRTEMP_LR_ADDR) = IRTEMP_LR_VALUE
			out.Write(IROp::Store32, IRTEMP_LR_VALUE, IRTEMP_LR_ADDR, out.AddConstant(off));
		};

		switch (inst.op) {
		case IROp::Load32Left:
			if (!combineOpposite(IROp::Load32Right, -3, IROp::Load32, -3)) {
				addCommonProlog();
				// dest &= (0x00ffffff >> shift)
				// Alternatively, could shift to a wall and back (but would require two shifts each way.)
				out.WriteSetConstant(IRTEMP_LR_MASK, 0x00ffffff);
				out.Write(IROp::Shr, IRTEMP_LR_MASK, IRTEMP_LR_MASK, IRTEMP_LR_SHIFT);
				out.Write(IROp::And, inst.dest, inst.dest, IRTEMP_LR_MASK);
				// IRTEMP_LR_SHIFT = 24 - shift
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				// IRTEMP_LR_VALUE <<= (24 - shift)
				out.Write(IROp::Shl, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_SHIFT);
				// dest |= IRTEMP_LR_VALUE
				out.Write(IROp::Or, inst.dest, inst.dest, IRTEMP_LR_VALUE);

				bool src1Dirty = inst.dest == inst.src1;
				while (i + 1 < n && !src1Dirty && nextOp().op == inst.op && nextOp().src1 == inst.src1 && (nextOp().constant & 3) == (inst.constant & 3)) {
					// IRTEMP_LR_VALUE = RAM(IRTEMP_LR_ADDR + offsetDelta)
					out.Write(IROp::Load32, IRTEMP_LR_VALUE, IRTEMP_LR_ADDR, out.AddConstant(nextOp().constant - inst.constant));

					// dest &= IRTEMP_LR_MASK
					out.Write(IROp::And, nextOp().dest, nextOp().dest, IRTEMP_LR_MASK);
					// IRTEMP_LR_VALUE <<= (24 - shift)
					out.Write(IROp::Shl, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_SHIFT);
					// dest |= IRTEMP_LR_VALUE
					out.Write(IROp::Or, nextOp().dest, nextOp().dest, IRTEMP_LR_VALUE);

					src1Dirty = nextOp().dest == inst.src1;
					++i;
				}
			}
			break;

		case IROp::Load32Right:
			if (!combineOpposite(IROp::Load32Left, 3, IROp::Load32, 0)) {
				addCommonProlog();
				// IRTEMP_LR_VALUE >>= shift
				out.Write(IROp::Shr, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_SHIFT);
				// IRTEMP_LR_SHIFT = 24 - shift
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				// dest &= (0xffffff00 << (24 - shift))
				// Alternatively, could shift to a wall and back (but would require two shifts each way.)
				out.WriteSetConstant(IRTEMP_LR_MASK, 0xffffff00);
				out.Write(IROp::Shl, IRTEMP_LR_MASK, IRTEMP_LR_MASK, IRTEMP_LR_SHIFT);
				out.Write(IROp::And, inst.dest, inst.dest, IRTEMP_LR_MASK);
				// dest |= IRTEMP_LR_VALUE
				out.Write(IROp::Or, inst.dest, inst.dest, IRTEMP_LR_VALUE);

				// Building display lists sometimes involves a bunch of lwr in a row.
				// We can generate more optimal code by combining.
				bool shiftNeedsReverse = true;
				bool src1Dirty = inst.dest == inst.src1;
				while (i + 1 < n && !src1Dirty && nextOp().op == inst.op && nextOp().src1 == inst.src1 && (nextOp().constant & 3) == (inst.constant & 3)) {
					// IRTEMP_LR_VALUE = RAM(IRTEMP_LR_ADDR + offsetDelta)
					out.Write(IROp::Load32, IRTEMP_LR_VALUE, IRTEMP_LR_ADDR, out.AddConstant(nextOp().constant - inst.constant));

					if (shiftNeedsReverse) {
						// IRTEMP_LR_SHIFT = shift again
						out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
						out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
						shiftNeedsReverse = false;
					}
					// IRTEMP_LR_VALUE >>= IRTEMP_LR_SHIFT
					out.Write(IROp::Shr, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_SHIFT);
					// dest &= IRTEMP_LR_MASK
					out.Write(IROp::And, nextOp().dest, nextOp().dest, IRTEMP_LR_MASK);
					// dest |= IRTEMP_LR_VALUE
					out.Write(IROp::Or, nextOp().dest, nextOp().dest, IRTEMP_LR_VALUE);

					src1Dirty = nextOp().dest == inst.src1;
					++i;
				}
			}
			break;

		case IROp::Store32Left:
			if (!combineOpposite(IROp::Store32Right, -3, IROp::Store32, -3)) {
				addCommonProlog();
				// IRTEMP_LR_VALUE &= 0xffffff00 << shift
				out.WriteSetConstant(IRTEMP_LR_MASK, 0xffffff00);
				out.Write(IROp::Shl, IRTEMP_LR_MASK, IRTEMP_LR_MASK, IRTEMP_LR_SHIFT);
				out.Write(IROp::And, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_MASK);
				// IRTEMP_LR_SHIFT = 24 - shift
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				// IRTEMP_LR_VALUE |= src3 >> (24 - shift)
				out.Write(IROp::Shr, IRTEMP_LR_MASK, inst.src3, IRTEMP_LR_SHIFT);
				out.Write(IROp::Or, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_MASK);
				addCommonStore(0);
			}
			break;

		case IROp::Store32Right:
			if (!combineOpposite(IROp::Store32Left, 3, IROp::Store32, 0)) {
				addCommonProlog();
				// IRTEMP_LR_VALUE &= 0x00ffffff << (24 - shift)
				out.WriteSetConstant(IRTEMP_LR_MASK, 0x00ffffff);
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				out.Write(IROp::Shr, IRTEMP_LR_MASK, IRTEMP_LR_MASK, IRTEMP_LR_SHIFT);
				out.Write(IROp::And, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_MASK);
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				// IRTEMP_LR_VALUE |= src3 << shift
				out.Write(IROp::Shl, IRTEMP_LR_MASK, inst.src3, IRTEMP_LR_SHIFT);
				out.Write(IROp::Or, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_MASK);
				addCommonStore(0);
			}
			break;

		default:
			out.Write(inst);
			break;
		}
	}

	return logBlocks;
}

bool PropagateConstants(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	IRRegCache gpr(&out);

	bool logBlocks = false;
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		bool symmetric = true;
		switch (inst.op) {
		case IROp::SetConst:
			gpr.SetImm(inst.dest, inst.constant);
			break;
		case IROp::SetConstF:
			goto doDefault;

		case IROp::Sub:
		case IROp::Slt:
		case IROp::SltU:
			symmetric = false;  // fallthrough
		case IROp::Add:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
			// Regularize, for the add/or check below.
			if (symmetric && inst.src2 == inst.dest && inst.src1 != inst.src2) {
				std::swap(inst.src1, inst.src2);
			}
			if (gpr.IsImm(inst.src1) && gpr.IsImm(inst.src2)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), gpr.GetImm(inst.src2), inst.op));
			} else if (inst.op == IROp::And && gpr.IsImm(inst.src1) && gpr.GetImm(inst.src1) == 0) {
				gpr.SetImm(inst.dest, 0);
			} else if (inst.op == IROp::And && gpr.IsImm(inst.src2) && gpr.GetImm(inst.src2) == 0) {
				gpr.SetImm(inst.dest, 0);
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
		case IROp::ReverseBits:
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
			// And 0 is otherwise set to 0.  Happens when optimizing lwl.
			if (inst.op == IROp::AndConst && inst.constant == 0) {
				gpr.SetImm(inst.dest, 0);
			} else if (gpr.IsImm(inst.src1)) {
				gpr.SetImm(inst.dest, Evaluate(gpr.GetImm(inst.src1), inst.constant, inst.op));
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
		case IROp::Madd:
		case IROp::MaddU:
		case IROp::Msub:
		case IROp::MsubU:
		case IROp::Div:
		case IROp::DivU:
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

		case IROp::FMovToGPR:
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
		case IROp::Store32Left:
		case IROp::Store32Right:
			if (gpr.IsImm(inst.src1) && inst.src1 != inst.dest) {
				gpr.MapIn(inst.dest);
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
			} else {
				gpr.MapInIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;
		case IROp::StoreFloat:
		case IROp::StoreVec4:
			if (gpr.IsImm(inst.src1)) {
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
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
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;
		case IROp::LoadFloat:
		case IROp::LoadVec4:
			if (gpr.IsImm(inst.src1)) {
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
			} else {
				gpr.MapIn(inst.src1);
				goto doDefault;
			}
			break;
		case IROp::Load32Left:
		case IROp::Load32Right:
			if (gpr.IsImm(inst.src1)) {
				gpr.MapIn(inst.dest);
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
			} else {
				gpr.MapInIn(inst.dest, inst.src1);
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
			// Regularize, to help x86 backends (add.s r0, r1, r0 -> add.s r0, r0, r1)
			if (inst.src2 == inst.dest && inst.src1 != inst.src2)
				std::swap(inst.src1, inst.src2);
			out.Write(inst);
			break;

		case IROp::FSub:
		case IROp::FDiv:
		case IROp::FNeg:
		case IROp::FAbs:
		case IROp::FMov:
		case IROp::FRound:
		case IROp::FTrunc:
		case IROp::FCeil:
		case IROp::FFloor:
		case IROp::FCvtSW:
		case IROp::FSin:
		case IROp::FCos:
		case IROp::FSqrt:
		case IROp::FRSqrt:
		case IROp::FRecip:
		case IROp::FAsin:
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

		case IROp::Vec4Init:
		case IROp::Vec4Mov:
		case IROp::Vec4Add:
		case IROp::Vec4Sub:
		case IROp::Vec4Mul:
		case IROp::Vec4Div:
		case IROp::Vec4Dot:
		case IROp::Vec4Scale:
		case IROp::Vec4Shuffle:
		case IROp::Vec4Neg:
		case IROp::Vec4Abs:
		case IROp::Vec4Pack31To8:
		case IROp::Vec4Pack32To8:
		case IROp::Vec2Pack32To16:
		case IROp::Vec4Unpack8To32:
		case IROp::Vec2Unpack16To32:
		case IROp::Vec4DuplicateUpperBitsAndShift1:
		case IROp::Vec2ClampToZero:
		case IROp::Vec4ClampToZero:
			out.Write(inst);
			break;

		case IROp::ZeroFpCond:
		case IROp::FCmp:
			gpr.MapDirty(IRREG_FPCOND);
			goto doDefault;

		case IROp::RestoreRoundingMode:
		case IROp::ApplyRoundingMode:
		case IROp::UpdateRoundingMode:
			goto doDefault;

		case IROp::VfpuCtrlToReg:
			gpr.MapDirtyIn(inst.dest, IRREG_VFPU_CTRL_BASE + inst.src1);
			goto doDefault;

		case IROp::CallReplacement:
		case IROp::Break:
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
		case IROp::Breakpoint:
		case IROp::MemoryCheck:
		default:
		{
			gpr.FlushAll();
		doDefault:
			out.Write(inst);
			break;
		}
		}
	}
	return logBlocks;
}

bool IRReadsFromGPR(const IRInst &inst, int reg) {
	const IRMeta *m = GetIRMeta(inst.op);

	if (m->types[1] == 'G' && inst.src1 == reg) {
		return true;
	}
	if (m->types[2] == 'G' && inst.src2 == reg) {
		return true;
	}
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && m->types[0] == 'G' && inst.src3 == reg) {
		return true;
	}
	if (inst.op == IROp::Interpret || inst.op == IROp::CallReplacement) {
		return true;
	}
	return false;
}

IRInst IRReplaceSrcGPR(const IRInst &inst, int fromReg, int toReg) {
	IRInst newInst = inst;
	const IRMeta *m = GetIRMeta(inst.op);

	if (m->types[1] == 'G' && inst.src1 == fromReg) {
		newInst.src1 = toReg;
	}
	if (m->types[2] == 'G' && inst.src2 == fromReg) {
		newInst.src2 = toReg;
	}
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && m->types[0] == 'G' && inst.src3 == fromReg) {
		newInst.src3 = toReg;
	}
	return newInst;
}

int IRDestGPR(const IRInst &inst) {
	const IRMeta *m = GetIRMeta(inst.op);

	if ((m->flags & IRFLAG_SRC3) == 0 && m->types[0] == 'G') {
		return inst.dest;
	}
	return -1;
}

IRInst IRReplaceDestGPR(const IRInst &inst, int fromReg, int toReg) {
	IRInst newInst = inst;
	const IRMeta *m = GetIRMeta(inst.op);

	if ((m->flags & IRFLAG_SRC3) == 0 && m->types[0] == 'G' && inst.dest == fromReg) {
		newInst.dest = toReg;
	}
	return newInst;
}

bool IRMutatesDestGPR(const IRInst &inst, int reg) {
	const IRMeta *m = GetIRMeta(inst.op);
	return (m->flags & IRFLAG_SRC3DST) != 0 && m->types[0] == 'G' && inst.src3 == reg;
}

bool PurgeTemps(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	std::vector<IRInst> insts;
	insts.reserve(in.GetInstructions().size());

	struct Check {
		Check(int r, int i, bool rbx) : reg(r), index(i), readByExit(rbx) {
		}

		int reg;
		int srcReg = -1;
		int index;
		bool readByExit;
	};
	std::vector<Check> checks;
	int lastWrittenTo[256];
	memset(lastWrittenTo, -1, sizeof(lastWrittenTo));

	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];
		const IRMeta *m = GetIRMeta(inst.op);

		for (Check &check : checks) {
			if (check.reg == 0) {
				continue;
			}

			if (IRReadsFromGPR(inst, check.reg)) {
				// Read from, but was this just a copy?
				bool mutatesReg = IRMutatesDestGPR(inst, check.reg);
				bool cannotReplace = inst.op == IROp::Interpret || inst.op == IROp::CallReplacement;
				if (!mutatesReg && !cannotReplace && check.srcReg >= 0 && lastWrittenTo[check.srcReg] < check.index) {
					// Replace with the srcReg instead.  This happens with non-nice delay slots.
					inst = IRReplaceSrcGPR(inst, check.reg, check.srcReg);
				} else if (!IRMutatesDestGPR(insts[check.index], check.reg) && inst.op == IROp::Mov && i == check.index + 1) {
					// This happens with lwl/lwr temps.  Replace the original dest.
					insts[check.index] = IRReplaceDestGPR(insts[check.index], check.reg, inst.dest);
					lastWrittenTo[inst.dest] = check.index;
					check.reg = inst.dest;
					// And swap the args for this mov, since we changed the other dest.  We'll optimize this out later.
					std::swap(inst.dest, inst.src1);
				} else {
					// Legitimately read from, so we can't optimize out.
					check.reg = 0;
				}
			} else if (check.readByExit && (m->flags & IRFLAG_EXIT) != 0) {
				check.reg = 0;
			} else if (IRDestGPR(inst) == check.reg) {
				// Clobbered, we can optimize out.
				// This happens sometimes with temporaries used for constant addresses.
				insts[check.index].op = IROp::Mov;
				insts[check.index].dest = 0;
				insts[check.index].src1 = 0;
				check.reg = 0;
			}
		}

		int dest = IRDestGPR(inst);
		switch (dest) {
		case IRTEMP_0:
		case IRTEMP_1:
		case IRTEMP_2:
		case IRTEMP_3:
		case IRTEMP_LHS:
		case IRTEMP_RHS:
		case IRTEMP_LR_ADDR:
		case IRTEMP_LR_VALUE:
		case IRTEMP_LR_MASK:
		case IRTEMP_LR_SHIFT:
			// Unlike other ops, these don't need to persist between blocks.
			// So we consider them not read unless proven read.
			lastWrittenTo[dest] = i;
			// If this is a copy, we might be able to optimize out the copy.
			if (inst.op == IROp::Mov) {
				Check check(dest, i, false);
				check.srcReg = inst.src1;
				checks.push_back(check);
			} else {
				checks.push_back(Check(dest, i, false));
			}
			break;

		default:
			lastWrittenTo[dest] = i;
			if (dest > IRTEMP_LR_SHIFT) {
				// These might sometimes be implicitly read/written by other instructions.
				break;
			}
			checks.push_back(Check(dest, i, true));
			break;

		// Not a GPR output.
		case 0:
		case -1:
			break;
		}

		// TODO: VFPU temps?  Especially for masked dregs.

		insts.push_back(inst);
	}

	for (Check &check : checks) {
		if (!check.readByExit && check.reg > 0) {
			insts[check.index].op = IROp::Mov;
			insts[check.index].dest = 0;
			insts[check.index].src1 = 0;
		}
	}

	for (const IRInst &inst : insts) {
		if (inst.op != IROp::Mov || inst.dest != 0 || inst.src1 != 0) {
			out.Write(inst);
		}
	}

	return logBlocks;
}

bool ReduceLoads(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	// This tells us to skip an AND op that has been optimized out.
	// Maybe we could skip multiple, but that'd slow things down and is pretty uncommon.
	int nextSkip = -1;

	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];

		if (inst.op == IROp::Load32 || inst.op == IROp::Load16 || inst.op == IROp::Load16Ext) {
			int dest = IRDestGPR(inst);
			for (int j = i + 1; j < n; j++) {
				const IRInst &laterInst = in.GetInstructions()[j];
				const IRMeta *m = GetIRMeta(laterInst.op);

				if ((m->flags & IRFLAG_EXIT) != 0) {
					// Exit, so we can't do the optimization.
					break;
				}
				if (IRReadsFromGPR(laterInst, dest)) {
					if (IRDestGPR(laterInst) == dest && laterInst.op == IROp::AndConst) {
						const u32 mask = laterInst.constant;
						// Here we are, maybe we can reduce the load size based on the mask.
						if ((mask & 0xffffff00) == 0) {
							inst.op = IROp::Load8;
							if (mask == 0xff) {
								nextSkip = j;
							}
						} else if ((mask & 0xffff0000) == 0 && inst.op == IROp::Load32) {
							inst.op = IROp::Load16;
							if (mask == 0xffff) {
								nextSkip = j;
							}
						}
					}
					// If it was read, we can't do the optimization.
					break;
				}
				if (IRDestGPR(laterInst) == dest) {
					// Someone else wrote, so we can't do the optimization.
					break;
				}
			}
		}

		if (i != nextSkip) {
			out.Write(inst);
		}
	}

	return logBlocks;
}

static std::vector<IRInst> ReorderLoadStoreOps(std::vector<IRInst> &ops) {
	if (ops.size() < 2) {
		return ops;
	}

	bool modifiedRegs[256] = {};

	for (size_t i = 0, n = ops.size(); i < n - 1; ++i) {
		bool modifiesReg = false;
		bool usesFloatReg = false;
		switch (ops[i].op) {
		case IROp::Load8:
		case IROp::Load8Ext:
		case IROp::Load16:
		case IROp::Load16Ext:
		case IROp::Load32:
		case IROp::Load32Left:
		case IROp::Load32Right:
			modifiesReg = true;
			if (ops[i].src1 == ops[i].dest) {
				// Can't ever reorder these, since it changes.
				continue;
			}
			break;

		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
		case IROp::Store32Left:
		case IROp::Store32Right:
			break;

		case IROp::LoadFloat:
		case IROp::LoadVec4:
			usesFloatReg = true;
			modifiesReg = true;
			break;

		case IROp::StoreFloat:
		case IROp::StoreVec4:
			usesFloatReg = true;
			break;

		default:
			continue;
		}

		memset(modifiedRegs, 0, sizeof(modifiedRegs));
		size_t start = i;
		size_t j;
		for (j = i; j < n; ++j) {
			if (ops[start].op != ops[j].op || ops[start].src1 != ops[j].src1) {
				// Incompatible ops, so let's not reorder.
				break;
			}
			if (modifiedRegs[ops[j].dest] || (!usesFloatReg && modifiedRegs[ops[j].src1])) {
				// Can't reorder, this reg was modified.
				break;
			}
			if (modifiesReg) {
				// Modifies itself, can't reorder this.
				if (!usesFloatReg && ops[j].dest == ops[j].src1) {
					break;
				}
				modifiedRegs[ops[j].dest] = true;
			}

			// Keep going, these operations are compatible.
		}

		// Everything up to (but not including) j will be sorted, so skip them.
		i = j - 1;
		size_t end = j;
		if (start + 1 < end) {
			std::stable_sort(ops.begin() + start, ops.begin() + end, [&](const IRInst &a, const IRInst &b) {
				return a.constant < b.constant;
			});
		}
	}

	return ops;
}

bool ReorderLoadStore(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	bool logBlocks = false;

	enum class RegState : u8 {
		UNUSED = 0,
		READ = 1,
		CHANGED = 2,
	};

	bool queuing = false;
	std::vector<IRInst> loadStoreQueue;
	std::vector<IRInst> otherQueue;
	RegState otherRegs[256] = {};

	auto flushQueue = [&]() {
		if (!queuing) {
			return;
		}

		std::vector<IRInst> loadStoreUnsorted = loadStoreQueue;
		std::vector<IRInst> loadStoreSorted = ReorderLoadStoreOps(loadStoreQueue);
		if (memcmp(&loadStoreSorted[0], &loadStoreUnsorted[0], sizeof(IRInst) * loadStoreSorted.size()) != 0) {
			logBlocks = true;
		}

		queuing = false;
		for (IRInst queued : loadStoreSorted) {
			out.Write(queued);
		}
		for (IRInst queued : otherQueue) {
			out.Write(queued);
		}
		loadStoreQueue.clear();
		otherQueue.clear();
		memset(otherRegs, 0, sizeof(otherRegs));
	};

	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
		switch (inst.op) {
		case IROp::Load8:
		case IROp::Load8Ext:
		case IROp::Load16:
		case IROp::Load16Ext:
		case IROp::Load32:
		case IROp::Load32Left:
		case IROp::Load32Right:
			// To move a load up, its dest can't be changed by things we move down.
			if (otherRegs[inst.dest] != RegState::UNUSED || otherRegs[inst.src1] == RegState::CHANGED) {
				flushQueue();
			}

			queuing = true;
			loadStoreQueue.push_back(inst);
			break;

		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
		case IROp::Store32Left:
		case IROp::Store32Right:
			// A store can move above even if it's read, as long as it's not changed by the other ops.
			if (otherRegs[inst.src3] == RegState::CHANGED || otherRegs[inst.src1] == RegState::CHANGED) {
				flushQueue();
			}

			queuing = true;
			loadStoreQueue.push_back(inst);
			break;

		case IROp::LoadVec4:
		case IROp::LoadFloat:
		case IROp::StoreVec4:
		case IROp::StoreFloat:
			// Floats can always move as long as their address is safe.
			if (otherRegs[inst.src1] == RegState::CHANGED) {
				flushQueue();
			}

			queuing = true;
			loadStoreQueue.push_back(inst);
			break;

		case IROp::Sub:
		case IROp::Slt:
		case IROp::SltU:
		case IROp::Add:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
		case IROp::Shl:
		case IROp::Shr:
		case IROp::Ror:
		case IROp::Sar:
		case IROp::MovZ:
		case IROp::MovNZ:
		case IROp::Max:
		case IROp::Min:
			// We'll try to move this downward.
			otherRegs[inst.dest] = RegState::CHANGED;
			if (inst.src1 && otherRegs[inst.src1] != RegState::CHANGED)
				otherRegs[inst.src1] = RegState::READ;
			if (inst.src2 && otherRegs[inst.src2] != RegState::CHANGED)
				otherRegs[inst.src2] = RegState::READ;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::Neg:
		case IROp::Not:
		case IROp::BSwap16:
		case IROp::BSwap32:
		case IROp::Ext8to32:
		case IROp::Ext16to32:
		case IROp::ReverseBits:
		case IROp::Clz:
		case IROp::AddConst:
		case IROp::SubConst:
		case IROp::AndConst:
		case IROp::OrConst:
		case IROp::XorConst:
		case IROp::SltConst:
		case IROp::SltUConst:
		case IROp::ShlImm:
		case IROp::ShrImm:
		case IROp::RorImm:
		case IROp::SarImm:
		case IROp::Mov:
			// We'll try to move this downward.
			otherRegs[inst.dest] = RegState::CHANGED;
			if (inst.src1 && otherRegs[inst.src1] != RegState::CHANGED)
				otherRegs[inst.src1] = RegState::READ;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::SetConst:
			// We'll try to move this downward.
			otherRegs[inst.dest] = RegState::CHANGED;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::Mult:
		case IROp::MultU:
		case IROp::Madd:
		case IROp::MaddU:
		case IROp::Msub:
		case IROp::MsubU:
		case IROp::Div:
		case IROp::DivU:
			if (inst.src1 && otherRegs[inst.src1] != RegState::CHANGED)
				otherRegs[inst.src1] = RegState::READ;
			if (inst.src2 && otherRegs[inst.src2] != RegState::CHANGED)
				otherRegs[inst.src2] = RegState::READ;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::MfHi:
		case IROp::MfLo:
		case IROp::FpCondToReg:
			otherRegs[inst.dest] = RegState::CHANGED;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::MtHi:
		case IROp::MtLo:
			if (inst.src1 && otherRegs[inst.src1] != RegState::CHANGED)
				otherRegs[inst.src1] = RegState::READ;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::Nop:
		case IROp::Downcount:
		case IROp::ZeroFpCond:
			if (queuing) {
				// These are freebies.  Sometimes helps with delay slots.
				otherQueue.push_back(inst);
			} else {
				out.Write(inst);
			}
			break;

		default:
			flushQueue();
			out.Write(inst);
			break;
		}
	}
	return logBlocks;
}

bool MergeLoadStore(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	bool logBlocks = false;

	auto opsCompatible = [&](const IRInst &a, const IRInst &b, int dist) {
		if (a.op != b.op || a.src1 != b.src1) {
			// Not similar enough at all.
			return false;
		}
		u32 off1 = a.constant;
		u32 off2 = b.constant;
		if (off1 + dist != off2) {
			// Not immediately sequential.
			return false;
		}

		return true;
	};

	IRInst prev = { IROp::Nop };
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];
		int c = 0;
		switch (inst.op) {
		case IROp::Store8:
			for (c = 1; c < 4 && i + c < n; ++c) {
				const IRInst &nextInst = in.GetInstructions()[i + c];
				// TODO: Might be nice to check if this is an obvious constant.
				if (inst.src3 != nextInst.src3 || inst.src3 != 0) {
					break;
				}
				if (!opsCompatible(inst, nextInst, c)) {
					break;
				}
			}
			if ((c == 2 || c == 3) && opts.unalignedLoadStore) {
				inst.op = IROp::Store16;
				out.Write(inst);
				prev = inst;
				// Skip the next one (the 3rd will be separate.)
				++i;
				continue;
			}
			if (c == 4 && opts.unalignedLoadStore) {
				inst.op = IROp::Store32;
				out.Write(inst);
				prev = inst;
				// Skip all 4.
				i += 3;
				continue;
			}
			out.Write(inst);
			prev = inst;
			break;

		case IROp::Store16:
			for (c = 1; c < 2 && i + c < n; ++c) {
				const IRInst &nextInst = in.GetInstructions()[i + c];
				// TODO: Might be nice to check if this is an obvious constant.
				if (inst.src3 != nextInst.src3 || inst.src3 != 0) {
					break;
				}
				if (!opsCompatible(inst, nextInst, c * 2)) {
					break;
				}
			}
			if (c == 2 && opts.unalignedLoadStore) {
				inst.op = IROp::Store32;
				out.Write(inst);
				prev = inst;
				// Skip the next one.
				++i;
				continue;
			}
			out.Write(inst);
			prev = inst;
			break;

		case IROp::Load32:
			if (prev.src1 == inst.src1 && prev.src2 == inst.src2) {
				// A store and then an immediate load.  This is sadly common in minis.
				if (prev.op == IROp::Store32 && prev.src3 == inst.dest) {
					// Even the same reg, a volatile variable?  Skip it.
					continue;
				}

				// Store16 and Store8 in rare cases happen... could be made AndConst, but not worth the trouble.
				if (prev.op == IROp::Store32) {
					inst.op = IROp::Mov;
					inst.src1 = prev.src3;
					inst.src2 = 0;
				} else if (prev.op == IROp::StoreFloat) {
					inst.op = IROp::FMovToGPR;
					inst.src1 = prev.src3;
					inst.src2 = 0;
				}
				// The actual op is written below.
			}
			out.Write(inst);
			prev = inst;
			break;

		case IROp::LoadFloat:
			if (prev.src1 == inst.src1 && prev.src2 == inst.src2) {
				// A store and then an immediate load, of a float.
				if (prev.op == IROp::StoreFloat && prev.src3 == inst.dest) {
					// Volatile float, I suppose?
					continue;
				}

				if (prev.op == IROp::StoreFloat) {
					inst.op = IROp::FMov;
					inst.src1 = prev.src3;
					inst.src2 = 0;
				} else if (prev.op == IROp::Store32) {
					inst.op = IROp::FMovFromGPR;
					inst.src1 = prev.src3;
					inst.src2 = 0;
				}
				// The actual op is written below.
			}
			out.Write(inst);
			prev = inst;
			break;

		default:
			out.Write(inst);
			prev = inst;
			break;
		}
	}
	return logBlocks;
}
