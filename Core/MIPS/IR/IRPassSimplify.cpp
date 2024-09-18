#include <algorithm>
#include <cstring>
#include <utility>

#include "Common/BitSet.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRRegCache.h"

// #define CONDITIONAL_DISABLE { for (IRInst inst : in.GetInstructions()) { out.Write(inst); } return false; }
#define CONDITIONAL_DISABLE
#define DISABLE { for (IRInst inst : in.GetInstructions()) { out.Write(inst); } return false; }

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
		_assert_msg_(false, "Unable to evaluate two op %d", (int)op);
		return -1;
	}
}

u32 Evaluate(u32 a, IROp op) {
	switch (op) {
	case IROp::Not: return ~a;
	case IROp::Neg: return -(s32)a;
	case IROp::BSwap16: return ((a & 0xFF00FF00) >> 8) | ((a & 0x00FF00FF) << 8);
	case IROp::BSwap32: return swap32(a);
	case IROp::Ext8to32: return SignExtend8ToU32(a);
	case IROp::Ext16to32: return SignExtend16ToU32(a);
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
		_assert_msg_(false, "Unable to evaluate one op %d", (int)op);
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
		_assert_msg_(false, "Invalid ArithToArithConst for op %d", (int)op);
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
		_assert_msg_(false, "Invalid ShiftToShiftImm for op %d", (int)op);
		return (IROp)-1;
	}
}

bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out, const IROptions &opts) {
	out.Reserve(in.GetInstructions().size());

	if (c == 1) {
		return passes[0](in, out, opts);
	}

	bool logBlocks = false;

	IRWriter temp[2];
	const IRWriter *nextIn = &in;
	IRWriter *nextOut = &temp[1];
	temp[1].Reserve(nextIn->GetInstructions().size());
	for (size_t i = 0; i < c - 1; ++i) {
		if (passes[i](*nextIn, *nextOut, opts)) {
			logBlocks = true;
		}

		temp[0] = std::move(temp[1]);
		nextIn = &temp[0];

		temp[1].Clear();
		temp[1].Reserve(nextIn->GetInstructions().size());
	}

	out.Reserve(nextIn->GetInstructions().size());
	if (passes[c - 1](*nextIn, out, opts)) {
		logBlocks = true;
	}

	return logBlocks;
}

bool OptimizeFPMoves(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;

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
				// Skip it entirely if it's just a copy to and back.
				if (inst.dest != inst.src1)
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
	CONDITIONAL_DISABLE;

	bool logBlocks = false;
	for (int i = 0; i < (int)in.GetInstructions().size(); i++) {
		IRInst inst = in.GetInstructions()[i];
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
	CONDITIONAL_DISABLE;

	bool logBlocks = false;

	bool letThroughHalves = false;
	if (opts.optimizeForInterpreter) {
		// If we're using the interpreter, which can handle these instructions directly,
		// don't break "half" instructions up.
		// Of course, we still want to combine if possible.
		letThroughHalves = true;
	}

	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; ++i) {
		const IRInst &inst = in.GetInstructions()[i];

		// TODO: Reorder or look ahead to combine?

		auto nextOp = [&]() -> const IRInst &{
			return in.GetInstructions()[i + 1];
		};

		auto combineOpposite = [&](IROp matchOp, int matchOff, IROp replaceOp, int replaceOff) {
			if (i + 1 >= n)
				return false;
			const IRInst &next = nextOp();
			if (next.op != matchOp || next.dest != inst.dest || next.src1 != inst.src1)
				return false;
			if (inst.constant + matchOff != next.constant)
				return false;

			if (opts.unalignedLoadStore) {
				// Write out one unaligned op.
				out.Write(replaceOp, inst.dest, inst.src1, out.AddConstant(inst.constant + replaceOff));
			} else if (replaceOp == IROp::Load32) {
				// We can still combine to a simpler set of two loads.
				// We start by isolating the address and shift amount.

				// IRTEMP_LR_ADDR = rs + imm
				out.Write(IROp::AddConst, IRTEMP_LR_ADDR, inst.src1, out.AddConstant(inst.constant + replaceOff));
				// IRTEMP_LR_SHIFT = (addr & 3) * 8
				out.Write(IROp::AndConst, IRTEMP_LR_SHIFT, IRTEMP_LR_ADDR, out.AddConstant(3));
				out.Write(IROp::ShlImm, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, 3);
				// IRTEMP_LR_ADDR = addr & 0xfffffffc
				out.Write(IROp::AndConst, IRTEMP_LR_ADDR, IRTEMP_LR_ADDR, out.AddConstant(0xFFFFFFFC));
				// IRTEMP_LR_VALUE = low_word, dest = high_word
				out.Write(IROp::Load32, inst.dest, IRTEMP_LR_ADDR, out.AddConstant(0));
				out.Write(IROp::Load32, IRTEMP_LR_VALUE, IRTEMP_LR_ADDR, out.AddConstant(4));

				// Now we just need to adjust and combine dest and IRTEMP_LR_VALUE.
				// inst.dest >>= shift (putting its bits in the right spot.)
				out.Write(IROp::Shr, inst.dest, inst.dest, IRTEMP_LR_SHIFT);
				// We can't shift by 32, so we compromise by shifting twice.
				out.Write(IROp::ShlImm, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, 8);
				// IRTEMP_LR_SHIFT = 24 - shift
				out.Write(IROp::Neg, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT);
				out.Write(IROp::AddConst, IRTEMP_LR_SHIFT, IRTEMP_LR_SHIFT, out.AddConstant(24));
				// IRTEMP_LR_VALUE <<= (24 - shift)
				out.Write(IROp::Shl, IRTEMP_LR_VALUE, IRTEMP_LR_VALUE, IRTEMP_LR_SHIFT);

				// At this point the values are aligned, and we just merge.
				out.Write(IROp::Or, inst.dest, inst.dest, IRTEMP_LR_VALUE);
			} else {
				return false;
			}
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
				if (letThroughHalves) {
					out.Write(inst);
					break;
				}

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
				if (letThroughHalves) {
					out.Write(inst);
					break;
				}
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
				if (letThroughHalves) {
					out.Write(inst);
					break;
				}
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
				if (letThroughHalves) {
					out.Write(inst);
					break;
				}
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
	CONDITIONAL_DISABLE;
	IRImmRegCache gpr(&out);

	bool logBlocks = false;
	bool skipNextExitToConst = false;
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
			if (gpr.IsImm(inst.src1) && gpr.GetImm(inst.src1) == 0 && !gpr.IsImm(inst.src2)) {
				// Morph into a Neg.
				gpr.MapDirtyIn(inst.dest, inst.src2);
				out.Write(IROp::Neg, inst.dest, inst.src2);
				break;
			} else if (inst.src1 == inst.src2) {
				// Seen sometimes, yet another way of producing zero.
				gpr.SetImm(inst.dest, 0);
				break;
			}
#if  __cplusplus >= 201703 || _MSC_VER > 1910
			[[fallthrough]];
#endif
		case IROp::Slt:
		case IROp::SltU:
			symmetric = false;
#if  __cplusplus >= 201703 || _MSC_VER > 1910
			[[fallthrough]];
#endif
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
				if (imm2 == 0 && (inst.op == IROp::Add || inst.op == IROp::Sub || inst.op == IROp::Or || inst.op == IROp::Xor)) {
					// Add / Sub / Or / Xor with zero is just a Mov.  Add / Or are most common.
					if (inst.dest != inst.src1)
						out.Write(IROp::Mov, inst.dest, inst.src1);
				} else {
					out.Write(ArithToArithConst(inst.op), inst.dest, inst.src1, out.AddConstant(imm2));
				}
			} else if (symmetric && gpr.IsImm(inst.src1)) {
				const u32 imm1 = gpr.GetImm(inst.src1);
				gpr.MapDirtyIn(inst.dest, inst.src2);
				if (imm1 == 0 && (inst.op == IROp::Add || inst.op == IROp::Or || inst.op == IROp::Xor)) {
					// Add / Or / Xor with zero is just a Mov.
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
			} else if (inst.constant == 0 && (inst.op == IROp::AddConst || inst.op == IROp::SubConst || inst.op == IROp::OrConst || inst.op == IROp::XorConst)) {
				// Convert an Add/Sub/Or/Xor with a constant zero to a Mov (just like with reg zero.)
				gpr.MapDirtyIn(inst.dest, inst.src1);
				if (inst.dest != inst.src1)
					out.Write(IROp::Mov, inst.dest, inst.src1);
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
		case IROp::Store32Conditional:
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
		case IROp::Load32Linked:
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

		case IROp::ValidateAddress8:
		case IROp::ValidateAddress16:
		case IROp::ValidateAddress32:
		case IROp::ValidateAddress128:
			if (gpr.IsImm(inst.src1)) {
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + inst.constant));
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
		case IROp::FCvtScaledWS:
		case IROp::FCvtScaledSW:
		case IROp::FSin:
		case IROp::FCos:
		case IROp::FSqrt:
		case IROp::FRSqrt:
		case IROp::FRecip:
		case IROp::FAsin:
			out.Write(inst);
			break;

		case IROp::SetCtrlVFPU:
			gpr.MapDirty(IRREG_VFPU_CTRL_BASE + inst.dest);
			goto doDefault;

		case IROp::SetCtrlVFPUReg:
			if (gpr.IsImm(inst.src1)) {
				out.Write(IROp::SetCtrlVFPU, inst.dest, out.AddConstant(gpr.GetImm(inst.src1)));
			} else {
				gpr.MapDirtyIn(IRREG_VFPU_CTRL_BASE + inst.dest, inst.src1);
				out.Write(inst);
			}
			break;

		case IROp::SetCtrlVFPUFReg:
			gpr.MapDirty(IRREG_VFPU_CTRL_BASE + inst.dest);
			goto doDefault;

		case IROp::FCvtWS:
			// TODO: Actually, this should just use the currently set rounding mode.
			// Move up with FCvtSW when that's implemented.
			gpr.MapIn(IRREG_FCR31);
			out.Write(inst);
			break;

		case IROp::FpCondFromReg:
			gpr.MapDirtyIn(IRREG_FPCOND, inst.src1);
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
		case IROp::FpCtrlFromReg:
			gpr.MapDirtyIn(IRREG_FCR31, inst.src1);
			gpr.MapDirty(IRREG_FPCOND);
			goto doDefault;
		case IROp::FpCtrlToReg:
			gpr.MapDirtyInIn(inst.dest, IRREG_FPCOND, IRREG_FCR31);
			goto doDefault;

		case IROp::Vec4Init:
		case IROp::Vec4Mov:
		case IROp::Vec4Add:
		case IROp::Vec4Sub:
		case IROp::Vec4Mul:
		case IROp::Vec4Div:
		case IROp::Vec4Dot:
		case IROp::Vec4Scale:
		case IROp::Vec4Shuffle:
		case IROp::Vec4Blend:
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

		case IROp::FCmpVfpuBit:
			gpr.MapDirty(IRREG_VFPU_CC);
			goto doDefault;

		case IROp::FCmovVfpuCC:
			gpr.MapIn(IRREG_VFPU_CC);
			goto doDefault;

		case IROp::FCmpVfpuAggregate:
			gpr.MapDirtyIn(IRREG_VFPU_CC, IRREG_VFPU_CC);
			goto doDefault;

		case IROp::ExitToConstIfEq:
		case IROp::ExitToConstIfNeq:
			if (gpr.IsImm(inst.src1) && gpr.IsImm(inst.src2)) {
				bool passed = false;
				switch (inst.op) {
				case IROp::ExitToConstIfEq: passed = gpr.GetImm(inst.src1) == gpr.GetImm(inst.src2); break;
				case IROp::ExitToConstIfNeq: passed = gpr.GetImm(inst.src1) != gpr.GetImm(inst.src2); break;
				default: _assert_(false); break;
				}

				// This is a bit common for the first cycle of loops.
				// Reduce bloat by skipping on fail, and const exit on pass.
				if (passed) {
					gpr.FlushAll();
					out.Write(IROp::ExitToConst, out.AddConstant(inst.constant));
					skipNextExitToConst = true;
				}
				break;
			}
			gpr.FlushAll();
			goto doDefault;

		case IROp::ExitToConstIfGtZ:
		case IROp::ExitToConstIfGeZ:
		case IROp::ExitToConstIfLtZ:
		case IROp::ExitToConstIfLeZ:
			if (gpr.IsImm(inst.src1)) {
				bool passed = false;
				switch (inst.op) {
				case IROp::ExitToConstIfGtZ: passed = (s32)gpr.GetImm(inst.src1) > 0; break;
				case IROp::ExitToConstIfGeZ: passed = (s32)gpr.GetImm(inst.src1) >= 0; break;
				case IROp::ExitToConstIfLtZ: passed = (s32)gpr.GetImm(inst.src1) < 0; break;
				case IROp::ExitToConstIfLeZ: passed = (s32)gpr.GetImm(inst.src1) <= 0; break;
				default: _assert_(false); break;
				}

				if (passed) {
					gpr.FlushAll();
					out.Write(IROp::ExitToConst, out.AddConstant(inst.constant));
					skipNextExitToConst = true;
				}
				break;
			}
			gpr.FlushAll();
			goto doDefault;

		case IROp::ExitToConst:
			if (skipNextExitToConst) {
				skipNextExitToConst = false;
				break;
			}
			gpr.FlushAll();
			goto doDefault;

		case IROp::ExitToReg:
			if (gpr.IsImm(inst.src1)) {
				// This happens sometimes near loops.
				// Prefer ExitToConst to allow block linking.
				u32 dest = gpr.GetImm(inst.src1);
				gpr.FlushAll();
				out.Write(IROp::ExitToConst, out.AddConstant(dest));
				break;
			}
			gpr.FlushAll();
			goto doDefault;

		case IROp::CallReplacement:
		case IROp::Break:
		case IROp::Syscall:
		case IROp::Interpret:
		case IROp::ExitToConstIfFpFalse:
		case IROp::ExitToConstIfFpTrue:
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
	gpr.FlushAll();
	return logBlocks;
}

IRInstMeta IRReplaceSrcGPR(const IRInstMeta &inst, int fromReg, int toReg) {
	IRInstMeta newInst = inst;

	if (inst.m.types[1] == 'G' && inst.src1 == fromReg) {
		newInst.src1 = toReg;
	}
	if (inst.m.types[2] == 'G' && inst.src2 == fromReg) {
		newInst.src2 = toReg;
	}
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && inst.m.types[0] == 'G' && inst.src3 == fromReg) {
		newInst.src3 = toReg;
	}
	return newInst;
}

IRInstMeta IRReplaceDestGPR(const IRInstMeta &inst, int fromReg, int toReg) {
	IRInstMeta newInst = inst;

	if ((inst.m.flags & IRFLAG_SRC3) == 0 && inst.m.types[0] == 'G' && inst.dest == fromReg) {
		newInst.dest = toReg;
	}
	return newInst;
}

bool IRMutatesDestGPR(const IRInstMeta &inst, int reg) {
	return (inst.m.flags & IRFLAG_SRC3DST) != 0 && inst.m.types[0] == 'G' && inst.src3 == reg;
}

bool PurgeTemps(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	std::vector<IRInstMeta> insts;
	insts.reserve(in.GetInstructions().size());

	// We track writes both to rename regs and to purge dead stores.
	struct Check {
		Check(int r, int i, bool rbx) : reg(r), index(i), readByExit(rbx) {
		}

		// Register this instruction wrote to.
		int reg;
		// Only other than -1 when it's a Mov, equivalent reg at this point.
		int srcReg = -1;
		// Index into insts for this op.
		int index;
		// Whether the dest reg is read by any Exit.
		bool readByExit;
		int8_t fplen = 0;
	};
	std::vector<Check> checks;
	checks.reserve(insts.size() / 2);

	// This tracks the last index at which each reg was modified.
	int lastWrittenTo[256];
	int lastReadFrom[256];
	memset(lastWrittenTo, -1, sizeof(lastWrittenTo));
	memset(lastReadFrom, -1, sizeof(lastReadFrom));

	auto readsFromFPRCheck = [](IRInstMeta &inst, Check &check, bool *directly) {
		if (check.reg < 32)
			return false;

		bool result = false;
		*directly = true;
		for (int i = 0; i < 4; ++i) {
			bool laneDirectly;
			if (check.fplen >= i + 1 && IRReadsFromFPR(inst, check.reg - 32 + i, &laneDirectly)) {
				result = true;
				if (!laneDirectly) {
					*directly = false;
					break;
				}
			}
		}
		return result;
	};

	bool logBlocks = false;
	size_t firstCheck = 0;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInstMeta inst = GetIRMeta(in.GetInstructions()[i]);

		// It helps to skip through rechecking ones we already discarded.
		for (size_t ch = firstCheck; ch < checks.size(); ++ch) {
			Check &check = checks[ch];
			if (check.reg != 0) {
				firstCheck = ch;
				break;
			}
		}

		// Check if we can optimize by running through all the writes we've previously found.
		for (size_t ch = firstCheck; ch < checks.size(); ++ch) {
			Check &check = checks[ch];
			if (check.reg == 0) {
				// This means we already optimized this or a later inst depends on it.
				continue;
			}

			bool readsDirectly;
			if (IRReadsFromGPR(inst, check.reg, &readsDirectly)) {
				// If this reads from the reg, we either depend on it or we can fold or swap.
				// That's determined below.

				// If this reads and writes the reg (e.g. MovZ, Load32Left), we can't just swap.
				bool mutatesReg = IRMutatesDestGPR(inst, check.reg);
				// If this doesn't directly read (i.e. Interpret), we can't swap.
				bool cannotReplace = !readsDirectly;
				if (!mutatesReg && !cannotReplace && check.srcReg >= 0 && lastWrittenTo[check.srcReg] < check.index) {
					// Replace with the srcReg instead.  This happens with non-nice delay slots.
					// We're changing "Mov A, B; Add C, C, A" to "Mov A, B; Add C, C, B" here.
					// srcReg should only be set when it was a Mov.
					inst = IRReplaceSrcGPR(inst, check.reg, check.srcReg);

					// If the Mov modified the same reg as this instruction, we can't optimize from it anymore.
					if (inst.dest == check.reg) {
						check.reg = 0;
						// We can also optimize it out since we've essentially moved now.
						insts[check.index].op = IROp::Mov;
						insts[check.index].dest = 0;
						insts[check.index].src1 = 0;
					}
				} else if (!IRMutatesDestGPR(insts[check.index], check.reg) && inst.op == IROp::Mov && i == check.index + 1) {
					// As long as the previous inst wasn't modifying its dest reg, and this is a Mov, we can swap.
					// We're changing "Add A, B, C; Mov B, A" to "Add B, B, C; Mov A, B" here.

					// This happens with lwl/lwr temps.  Replace the original dest.
					insts[check.index] = IRReplaceDestGPR(insts[check.index], check.reg, inst.dest);
					lastWrittenTo[inst.dest] = check.index;
					// If it's being read from (by inst now), we can't optimize out.
					check.reg = 0;
					// Update the read by exit flag to match the new reg.
					check.readByExit = inst.dest < IRTEMP_0 || inst.dest > IRTEMP_LR_SHIFT;
					// And swap the args for this mov, since we changed the other dest.  We'll optimize this out later.
					std::swap(inst.dest, inst.src1);
				} else {
					// Legitimately read from, so we can't optimize out.
					// Unless this is an exit and a temp not read directly by the exit.
					if ((inst.m.flags & IRFLAG_EXIT) == 0 || check.readByExit || readsDirectly)
						check.reg = 0;
				}
			} else if (check.fplen >= 1 && readsFromFPRCheck(inst, check, &readsDirectly)) {
				// If one or the other is a Vec, they must match.
				bool lenMismatch = false;

				auto checkMismatch = [&check, &lenMismatch](IRReg src, char type) {
					int srclen = 1;
					if (type == 'V')
						srclen = 4;
					else if (type == '2')
						srclen = 2;
					else if (type != 'F')
						return;

					if (src + 32 + srclen > check.reg && src + 32 < check.reg + check.fplen) {
						if (src + 32 != check.reg || srclen != check.fplen)
							lenMismatch = true;
					}
				};

				checkMismatch(inst.src1, inst.m.types[1]);
				checkMismatch(inst.src2, inst.m.types[2]);
				if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0)
					checkMismatch(inst.src3, inst.m.types[3]);

				bool cannotReplace = !readsDirectly || lenMismatch;
				if (!cannotReplace && check.srcReg >= 32 && lastWrittenTo[check.srcReg] < check.index) {
					// This is probably not worth doing unless we can get rid of a temp.
					if (!check.readByExit) {
						if (insts[check.index].dest == inst.src1)
							inst.src1 = check.srcReg - 32;
						else if (insts[check.index].dest == inst.src2)
							inst.src2 = check.srcReg - 32;
						else
							_assert_msg_(false, "Unexpected src3 read of FPR");

						// Check if we've clobbered it entirely.
						if (inst.dest == check.reg) {
							check.reg = 0;
							insts[check.index].op = IROp::Mov;
							insts[check.index].dest = 0;
							insts[check.index].src1 = 0;
						}
					} else {
						// Let's not bother.
						check.reg = 0;
					}
				} else if ((inst.op == IROp::FMov || inst.op == IROp::Vec4Mov) && !lenMismatch) {
					// A swap could be profitable if this is a temp, and maybe in other cases.
					// These can happen a lot from mask regs, etc.
					// But make sure no other changes happened between.
					bool destNotChanged = true;
					for (int j = 0; j < check.fplen; ++j)
						destNotChanged = destNotChanged && lastWrittenTo[inst.dest + 32 + j] < check.index;

					bool destNotRead = true;
					for (int j = 0; j < check.fplen; ++j)
						destNotRead = destNotRead && lastReadFrom[inst.dest + 32 + j] <= check.index;

					if (!check.readByExit && destNotChanged && destNotRead) {
						_dbg_assert_(insts[check.index].dest == inst.src1);
						insts[check.index].dest = inst.dest;
						for (int j = 0; j < check.fplen; ++j)
							lastWrittenTo[inst.dest + 32 + j] = check.index;
						// If it's being read from (by inst now), we can't optimize out.
						check.reg = 0;
						// Swap the dest and src1 so we can optimize this out later, maybe.
						std::swap(inst.dest, inst.src1);
					} else {
						// Doesn't look like a good candidate.
						check.reg = 0;
					}
				} else {
					// Legitimately read from, so we can't optimize out.
					if ((inst.m.flags & IRFLAG_EXIT) == 0 || check.readByExit || readsDirectly)
						check.reg = 0;
				}
			} else if (check.readByExit && (inst.m.flags & IRFLAG_EXIT) != 0) {
				// This is an exit, and the reg is read by any exit.  Clear it.
				check.reg = 0;
			} else if (IRDestGPR(inst) == check.reg) {
				// Clobbered, we can optimize out.
				// This happens sometimes with temporaries used for constant addresses.
				insts[check.index].op = IROp::Mov;
				insts[check.index].dest = 0;
				insts[check.index].src1 = 0;
				check.reg = 0;
			} else if (IRWritesToFPR(inst, check.reg - 32) && check.fplen >= 1) {
				IRReg destFPRs[4];
				int numFPRs = IRDestFPRs(inst, destFPRs);

				if (numFPRs == check.fplen && inst.dest + 32 == check.reg) {
					// This means we've clobbered it, and with full overlap.
					// Sometimes this happens for non-temps, i.e. vmmov + vinit last row.
					insts[check.index].op = IROp::Mov;
					insts[check.index].dest = 0;
					insts[check.index].src1 = 0;
					check.reg = 0;
				} else {
					// Since there's an overlap, we simply cannot optimize.
					check.reg = 0;
				}
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
			// Check that it's not a barrier instruction (like CallReplacement). Don't want to even consider optimizing those.
			if (!(inst.m.flags & IRFLAG_BARRIER)) {
				// Unlike other registers, these don't need to persist between blocks.
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
			} else {
				lastWrittenTo[dest] = i;
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

		IRReg regs[16];
		int readGPRs = IRReadsFromGPRs(inst, regs);
		if (readGPRs == -1) {
			for (int j = 0; j < 256; ++j)
				lastReadFrom[j] = i;
		} else {
			for (int j = 0; j < readGPRs; ++j)
				lastReadFrom[regs[j]] = i;
		}

		int readFPRs = IRReadsFromFPRs(inst, regs);
		if (readFPRs == -1) {
			for (int j = 0; j < 256; ++j)
				lastReadFrom[j] = i;
		} else {
			for (int j = 0; j < readFPRs; ++j)
				lastReadFrom[regs[j] + 32] = i;
		}

		int destFPRs = IRDestFPRs(inst, regs);
		for (int j = 0; j < destFPRs; ++j)
			lastWrittenTo[regs[j] + 32] = i;

		dest = destFPRs > 0 ? regs[0] + 32 : -1;
		if (dest >= 32 && dest < IRTEMP_0) {
			// Standard FPU or VFPU reg.
			Check check(dest, i, true);
			check.fplen = (int8_t)destFPRs;
			checks.push_back(check);
		} else if (dest >= IRVTEMP_PFX_S + 32 && dest < IRVTEMP_PFX_S + 32 + 16) {
			// These are temporary regs and not read by exits.
			Check check(dest, i, false);
			check.fplen = (int8_t)destFPRs;
			if (inst.op == IROp::FMov || inst.op == IROp::Vec4Mov) {
				check.srcReg = inst.src1 + 32;
			}
			checks.push_back(check);
		} else if (dest != -1) {
			_assert_msg_(false, "Unexpected FPR output %d", dest);
		}

		insts.push_back(inst);
	}

	// Since we're done with the instructions, all remaining can be nuked.
	for (Check &check : checks) {
		if (!check.readByExit && check.reg > 0) {
			insts[check.index].op = IROp::Mov;
			insts[check.index].dest = 0;
			insts[check.index].src1 = 0;
		}
	}

	for (const IRInstMeta &inst : insts) {
		// Simply skip any Mov 0, 0 instructions, since that's how we nuke one.
		if (inst.op != IROp::Mov || inst.dest != 0 || inst.src1 != 0) {
			out.Write(inst.i);
		}
	}

	return logBlocks;
}

bool ReduceLoads(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	// This tells us to skip an AND op that has been optimized out.
	// Maybe we could skip multiple, but that'd slow things down and is pretty uncommon.
	int nextSkip = -1;

	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];

		if (inst.op == IROp::Load32 || inst.op == IROp::Load16 || inst.op == IROp::Load16Ext) {
			int dest = IRDestGPR(GetIRMeta(inst));
			for (int j = i + 1; j < n; j++) {
				const IRInstMeta laterInst = GetIRMeta(in.GetInstructions()[j]);

				if ((laterInst.m.flags & (IRFLAG_EXIT | IRFLAG_BARRIER)) != 0) {
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
	CONDITIONAL_DISABLE;

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
		case IROp::FpCondFromReg:
			if (inst.src1 && otherRegs[inst.src1] != RegState::CHANGED)
				otherRegs[inst.src1] = RegState::READ;
			otherQueue.push_back(inst);
			queuing = true;
			break;

		case IROp::Nop:
		case IROp::Downcount:
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
	CONDITIONAL_DISABLE;

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

struct IRMemoryOpInfo {
	int size;
	bool isWrite;
	bool isWordLR;
};

static IRMemoryOpInfo IROpMemoryAccessSize(IROp op) {
	// Assumes all take src1 + constant.
	switch (op) {
	case IROp::Load8:
	case IROp::Load8Ext:
	case IROp::Store8:
		return { 1, op == IROp::Store8 };

	case IROp::Load16:
	case IROp::Load16Ext:
	case IROp::Store16:
		return { 2, op == IROp::Store16 };

	case IROp::Load32:
	case IROp::Load32Linked:
	case IROp::LoadFloat:
	case IROp::Store32:
	case IROp::Store32Conditional:
	case IROp::StoreFloat:
		return { 4, op == IROp::Store32 || op == IROp::Store32Conditional || op == IROp::StoreFloat };

	case IROp::LoadVec4:
	case IROp::StoreVec4:
		return { 16, op == IROp::StoreVec4 };

	case IROp::Load32Left:
	case IROp::Load32Right:
	case IROp::Store32Left:
	case IROp::Store32Right:
		// This explicitly does not require alignment, so validate as an 8-bit operation.
		return { 1, op == IROp::Store32Left || op == IROp::Store32Right, true };

	default:
		return { 0 };
	}
}

bool ApplyMemoryValidation(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	if (g_Config.bFastMemory)
		DISABLE;

	int spLower = 0;
	int spUpper = -1;
	bool spWrite = false;
	bool spModified = false;
	for (IRInst inst : in.GetInstructions()) {
		IRMemoryOpInfo info = IROpMemoryAccessSize(inst.op);
		// Note: we only combine word aligned accesses.
		if (info.size != 0 && inst.src1 == MIPS_REG_SP && info.size == 4) {
			if (spModified) {
				// No good, it was modified and then we did more accesses.  Can't combine.
				spUpper = -1;
				break;
			}
			if ((int)inst.constant < 0 || (int)inst.constant >= 0x4000) {
				// Let's assume this might cross boundaries or something.  Uncommon.
				spUpper = -1;
				break;
			}

			spLower = std::min(spLower, (int)inst.constant);
			spUpper = std::max(spUpper, (int)inst.constant + info.size);
			spWrite = spWrite || info.isWrite;
		}

		const IRMeta *m = GetIRMeta(inst.op);
		if (m->types[0] == 'G' && (m->flags & IRFLAG_SRC3) == 0 && inst.dest == MIPS_REG_SP) {
			// We only care if it changes after we start combining.
			spModified = spUpper != -1;
		}
	}

	bool skipSP = spUpper != -1;
	bool flushedSP = false;

	std::map<uint64_t, uint8_t> checks;
	const auto addValidate = [&](IROp validate, uint8_t sz, const IRInst &inst, bool isStore) {
		if (inst.src1 == MIPS_REG_SP && skipSP && validate == IROp::ValidateAddress32) {
			if (!flushedSP) {
				out.Write(IROp::ValidateAddress32, 0, MIPS_REG_SP, spWrite ? 1U : 0U, spLower);
				if (spUpper > spLower + 4)
					out.Write(IROp::ValidateAddress32, 0, MIPS_REG_SP, spWrite ? 1U : 0U, spUpper - 4);
				flushedSP = true;
			}
			return;
		}

		uint64_t key = ((uint64_t)inst.src1 << 32) | inst.constant;
		auto it = checks.find(key);
		if (it == checks.end() || it->second < sz) {
			out.Write(validate, 0, inst.src1, isStore ? 1U : 0U, inst.constant);
			checks[key] = sz;
		}
	};

	bool logBlocks = false;
	for (IRInst inst : in.GetInstructions()) {
		IRMemoryOpInfo info = IROpMemoryAccessSize(inst.op);
		IROp validateOp = IROp::Nop;
		switch (info.size) {
		case 1: validateOp = IROp::ValidateAddress8; break;
		case 2: validateOp = IROp::ValidateAddress16; break;
		case 4: validateOp = IROp::ValidateAddress32; break;
		case 16: validateOp = IROp::ValidateAddress128; break;
		case 0: break;
		default: _assert_msg_(false, "Unexpected memory access size");
		}

		if (validateOp != IROp::Nop) {
			addValidate(validateOp, info.size, inst, info.isWrite);
		}

		const IRMeta *m = GetIRMeta(inst.op);
		if (m->types[0] == 'G' && (m->flags & IRFLAG_SRC3) == 0) {
			uint64_t key = (uint64_t)inst.dest << 32;
			// Wipe out all the already done checks since this was modified.
			checks.erase(checks.lower_bound(key), checks.upper_bound(key | 0xFFFFFFFFULL));
		}

		// Always write out the original.  We're only adding.
		out.Write(inst);
	}
	return logBlocks;
}

bool ReduceVec4Flush(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	// Only do this when using a SIMD backend.
	if (!opts.preferVec4) {
		DISABLE;
	}

	bool isVec4[256]{};
	bool isUsed[256]{};
	bool isVec4Dirty[256]{};
	auto updateVec4 = [&](char type, IRReg r) {
		bool downgraded = false;
		switch (type) {
		case 'F':
			downgraded = isVec4[r & ~3];
			isVec4[r & ~3] = false;
			isUsed[r] = true;
			break;

		case 'V':
			_dbg_assert_((r & 3) == 0);
			isVec4[r] = true;
			for (int i = 0; i < 4; ++i)
				isUsed[r + i] = true;
			break;

		case '2':
			downgraded = isVec4[r & ~3];
			isVec4[r & ~3] = false;
			for (int i = 0; i < 2; ++i)
				isUsed[r + i] = true;
			break;

		default:
			break;
		}

		return downgraded;
	};
	auto updateVec4Dest = [&](char type, IRReg r, uint32_t flags) {
		if ((flags & IRFLAG_SRC3) == 0) {
			switch (type) {
			case 'F':
				isVec4Dirty[r & ~3] = false;
				break;

			case 'V':
				_dbg_assert_((r & 3) == 0);
				isVec4Dirty[r] = true;
				break;

			case '2':
				isVec4Dirty[r & ~3] = false;
				break;

			default:
				break;
			}
		}
		return updateVec4(type, r);
	};

	// Checks overlap from r1 to other params.
	auto overlapped = [](IRReg r1, int l1, IRReg r2, int l2, IRReg r3 = IRREG_INVALID, int l3 = 0) {
		if (r1 < r2 + l2 && r1 + l1 > r2)
			return true;
		if (r1 < r3 + l3 && r1 + l1 > r3)
			return true;
		return false;
	};

	bool logBlocks = false;
	int inCount = (int)in.GetInstructions().size();
	for (int i = 0; i < inCount; ++i) {
		IRInst inst = in.GetInstructions()[i];
		const IRMeta *m = GetIRMeta(inst.op);

		if ((m->flags & (IRFLAG_EXIT | IRFLAG_BARRIER)) != 0) {
			memset(isVec4, 0, sizeof(isVec4));
			out.Write(inst);
			continue;
		}

		IRReg temp = IRREG_INVALID;
		auto findAvailTempVec4 = [&]() {
			// If it's not used yet in this block, we can use it.
			// Note: even if the instruction uses it to write, that should be fine.
			for (IRReg r = IRVTEMP_PFX_S; r < IRVTEMP_0 + 4; r += 4) {
				if (isUsed[r])
					continue;

				bool usable = true;
				for (int j = 1; j < 4; ++j)
					usable = usable && !isUsed[r + j];

				if (usable) {
					temp = r;
					// We don't update isUsed because our temporary doesn't need to last.
					return true;
				}
			}

			return false;
		};

		auto usedLaterAsVec4 = [&](IRReg r) {
			for (int j = i + 1; j < inCount; ++j) {
				IRInst inst = in.GetInstructions()[j];
				const IRMeta *m = GetIRMeta(inst.op);
				if (m->types[0] == 'V' && inst.dest == r)
					return true;
				if (m->types[1] == 'V' && inst.src1 == r)
					return true;
				if (m->types[2] == 'V' && inst.src2 == r)
					return true;
			}
			return false;
		};

		bool skip = false;
		switch (inst.op) {
		case IROp::SetConstF:
			if (isVec4[inst.dest & ~3] && findAvailTempVec4()) {
				// Check if we're setting multiple in a row, this is a bit common.
				u8 blendMask = 1 << (inst.dest & 3);
				while (i + 1 < inCount) {
					IRInst next = in.GetInstructions()[i + 1];
					if (next.op != IROp::SetConstF || (next.dest & ~3) != (inst.dest & ~3))
						break;
					if (next.constant != inst.constant)
						break;

					blendMask |= 1 << (next.dest & 3);
					i++;
				}

				if (inst.constant == 0) {
					out.Write(IROp::Vec4Init, temp, (int)Vec4Init::AllZERO);
				} else if (inst.constant == 0x3F800000) {
					out.Write(IROp::Vec4Init, temp, (int)Vec4Init::AllONE);
				} else if (inst.constant == 0xBF800000) {
					out.Write(IROp::Vec4Init, temp, (int)Vec4Init::AllMinusONE);
				} else {
					out.Write(IROp::SetConstF, temp, out.AddConstant(inst.constant));
					out.Write(IROp::Vec4Shuffle, temp, temp, 0);
				}
				out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, blendMask);
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			}
			break;

		case IROp::FMovFromGPR:
			if (isVec4[inst.dest & ~3] && findAvailTempVec4()) {
				u8 blendMask = 1 << (inst.dest & 3);
				out.Write(IROp::FMovFromGPR, temp, inst.src1);
				out.Write(IROp::Vec4Shuffle, temp, temp, 0);
				out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, blendMask);
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			}
			break;

		case IROp::LoadFloat:
			if (isVec4[inst.dest & ~3] && isVec4Dirty[inst.dest & ~3] && usedLaterAsVec4(inst.dest & ~3) && findAvailTempVec4()) {
				u8 blendMask = 1 << (inst.dest & 3);
				out.Write(inst.op, temp, inst.src1, inst.src2, inst.constant);
				out.Write(IROp::Vec4Shuffle, temp, temp, 0);
				out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, blendMask);
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			}
			break;

		case IROp::StoreFloat:
			if (isVec4[inst.src3 & ~3] && isVec4Dirty[inst.src3 & ~3] && usedLaterAsVec4(inst.src3 & ~3) && findAvailTempVec4()) {
				out.Write(IROp::FMov, temp, inst.src3, 0);
				out.Write(inst.op, temp, inst.src1, inst.src2, inst.constant);
				continue;
			}
			break;

		case IROp::FMov:
			if (isVec4[inst.dest & ~3] && (inst.dest & ~3) == (inst.src1 & ~3)) {
				// Oh, actually a shuffle?
				uint8_t shuffle = (uint8_t)VFPU_SWIZZLE(0, 1, 2, 3);
				uint8_t destShift = (inst.dest & 3) * 2;
				shuffle = (shuffle & ~(3 << destShift)) | ((inst.src1 & 3) << destShift);
				out.Write(IROp::Vec4Shuffle, inst.dest & ~3, inst.dest & ~3, shuffle);
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			} else if (isVec4[inst.dest & ~3] && (inst.dest & 3) == (inst.src1 & 3)) {
				// We can turn this directly into a blend, since it's the same lane.
				out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, inst.src1 & ~3, 1 << (inst.dest & 3));
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			} else if (isVec4[inst.dest & ~3] && isVec4[inst.src1 & ~3] && findAvailTempVec4()) {
				// For this, we'll need a temporary to move to the right lane.
				int lane = inst.src1 & 3;
				uint8_t shuffle = (uint8_t)VFPU_SWIZZLE(lane, lane, lane, lane);
				out.Write(IROp::Vec4Shuffle, temp, inst.src1 & ~3, shuffle);
				out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, 1 << (inst.dest & 3));
				isVec4Dirty[inst.dest & ~3] = true;
				continue;
			}
			break;

		case IROp::FAdd:
		case IROp::FSub:
		case IROp::FMul:
		case IROp::FDiv:
			if (isVec4[inst.dest & ~3] && isVec4Dirty[inst.dest & ~3] && usedLaterAsVec4(inst.dest & ~3)) {
				if (!overlapped(inst.dest & ~3, 4, inst.src1, 1, inst.src2, 1) && findAvailTempVec4()) {
					u8 blendMask = 1 << (inst.dest & 3);
					out.Write(inst.op, temp, inst.src1, inst.src2);
					out.Write(IROp::Vec4Shuffle, temp, temp, 0);
					out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, blendMask);
					updateVec4('F', inst.src1);
					updateVec4('F', inst.src2);
					isVec4Dirty[inst.dest & ~3] = true;
					continue;
				}
			}
			break;

		case IROp::Vec4Dot:
			if (overlapped(inst.dest, 1, inst.src1, 4, inst.src2, 4) && findAvailTempVec4()) {
				out.Write(inst.op, temp, inst.src1, inst.src2, inst.constant);
				if (usedLaterAsVec4(inst.dest & ~3)) {
					// Broadcast to other lanes if needed.
					if ((inst.dest & 3) != 0)
						out.Write(IROp::Vec4Shuffle, temp, temp, 0);
					out.Write(IROp::Vec4Blend, inst.dest & ~3, inst.dest & ~3, temp, 1 << (inst.dest & 3));
					// It's overlapped, so it'll get marked as Vec4 and used anyway.
					isVec4Dirty[inst.dest & ~3] = true;
					inst.dest = IRREG_INVALID;
				} else {
					out.Write(IROp::FMov, inst.dest, temp);
				}
				skip = true;
			}
			break;

		case IROp::Vec4Scale:
			if (overlapped(inst.src2, 1, inst.src1, 4, inst.dest, 4) && findAvailTempVec4()) {
				out.Write(IROp::FMov, temp, inst.src2);
				out.Write(inst.op, inst.dest, inst.src1, temp, inst.constant);
				skip = true;
				inst.src2 = IRREG_INVALID;
			} else if (isVec4[inst.src2 & 3] && usedLaterAsVec4(inst.src2 & ~3) && findAvailTempVec4()) {
				out.Write(IROp::FMov, temp, inst.src2);
				out.Write(inst.op, inst.dest, inst.src1, temp, inst.constant);
				skip = true;
				inst.src2 = IRREG_INVALID;
			}
			break;

		default:
			break;
		}

		bool downgrade = false;
		if (inst.src1 != IRREG_INVALID && updateVec4(m->types[1], inst.src1))
			downgrade = true;
		if (inst.src2 != IRREG_INVALID && updateVec4(m->types[2], inst.src2))
			downgrade = true;
		if (inst.dest != IRREG_INVALID && updateVec4Dest(m->types[0], inst.dest, m->flags))
			downgrade = true;

		if (downgrade) {
			//WARN_LOG(Log::JIT, "Vec4 downgrade by: %s", m->name);
		}

		if (!skip)
			out.Write(inst);
	}
	return logBlocks;
}

// This optimizes away redundant loads-after-stores, which are surprisingly not that uncommon.
bool OptimizeLoadsAfterStores(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	// This tells us to skip an AND op that has been optimized out.
	// Maybe we could skip multiple, but that'd slow things down and is pretty uncommon.
	int nextSkip = -1;

	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];

		// Just copy the last instruction.
		if (i == n - 1) {
			out.Write(inst);
			break;
		}

		out.Write(inst);

		IRInst next = in.GetInstructions()[i + 1];
		switch (inst.op) {
		case IROp::Store32:
			if (next.op == IROp::Load32 &&
				next.constant == inst.constant &&
				next.dest == inst.dest &&
				next.src1 == inst.src1) {
				// The upcoming load is completely redundant.
				// Skip it.
				i++;
			}
			break;
		case IROp::StoreVec4:
			if (next.op == IROp::LoadVec4 &&
				next.constant == inst.constant &&
				next.dest == inst.dest &&
				next.src1 == inst.src1) {
				// The upcoming load is completely redundant. These are common in Wipeout.
				// Skip it. NOTE: It looks like vector load/stores uses different register assignments, but there's a union between dest and src3.
				i++;
			}
			break;
		default:
			break;
		}
	}

	return logBlocks;
}

bool OptimizeForInterpreter(const IRWriter &in, IRWriter &out, const IROptions &opts) {
	CONDITIONAL_DISABLE;
	// This tells us to skip an AND op that has been optimized out.
	// Maybe we could skip multiple, but that'd slow things down and is pretty uncommon.
	int nextSkip = -1;

	bool logBlocks = false;
	// We also move the downcount to the top so the interpreter can assume that it's there.
	bool foundDowncount = false;
	out.Write(IROp::Downcount);

	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		IRInst inst = in.GetInstructions()[i];

		bool last = i == n - 1;

		// Specialize some instructions.
		switch (inst.op) {
		case IROp::Downcount:
			if (!foundDowncount) {
				// Move the value into the initial Downcount.
				foundDowncount = true;
				out.ReplaceConstant(0, inst.constant);
			} else {
				// Already had a downcount. Let's just re-emit it.
				out.Write(inst);
			}
			break;
		case IROp::AddConst:
			if (inst.src1 == inst.dest) {
				inst.op = IROp::OptAddConst;
			}
			out.Write(inst);
			break;
		case IROp::AndConst:
			if (inst.src1 == inst.dest) {
				inst.op = IROp::OptAndConst;
			}
			out.Write(inst);
			break;
		case IROp::OrConst:
			if (inst.src1 == inst.dest) {
				inst.op = IROp::OptOrConst;
			}
			out.Write(inst);
			break;
		case IROp::FMovToGPR:
			if (!last) {
				IRInst next = in.GetInstructions()[i + 1];
				if (next.op == IROp::ShrImm && next.src2 == 8 && next.src1 == next.dest && next.src1 == inst.dest) {
					// Heavily used when writing display lists.
					inst.op = IROp::OptFMovToGPRShr8;
					i++;  // Skip the next instruction.
				}
			}
			out.Write(inst);
			break;
		case IROp::FMovFromGPR:
			if (!last) {
				IRInst next = in.GetInstructions()[i + 1];
				if (next.op == IROp::FCvtSW && next.src1 == inst.dest && next.dest == inst.dest) {
					inst.op = IROp::OptFCvtSWFromGPR;
					i++;  // Skip the next
				}
			}
			out.Write(inst);
			break;
		default:
			out.Write(inst);
			break;
		}
	}

	return logBlocks;
}
