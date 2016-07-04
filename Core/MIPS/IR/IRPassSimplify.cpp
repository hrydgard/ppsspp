#include <algorithm>
#include <cstring>
#include <utility>

#include "Common/Log.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRRegCache.h"

void WriteInstWithConstants(const IRWriter &in, IRWriter &out, const u32 *constants, IRInst inst) {
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

bool OptimizeFPMoves(const IRWriter &in, IRWriter &out) {
	const u32 *constants = !in.GetConstants().empty() ? &in.GetConstants()[0] : nullptr;
	bool logBlocks = false;
	IRInst prev;
	prev.op = IROp::Nop;
	prev.dest = 0;
	prev.src1 = 0;
	prev.src2 = 0;
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
				inst.src2 = out.AddConstant(constants[prev.src2] + constants[inst.src2]);
				inst.src1 = prev.src1;
				logBlocks = 1;
			} else {
				goto doDefault;
			}
			out.Write(inst);
			break;
		*/
		default:
			WriteInstWithConstants(in, out, constants, inst);
			break;
		}
		prev = inst;
	}
	return logBlocks;
}

// Might be useful later on x86.
bool ThreeOpToTwoOp(const IRWriter &in, IRWriter &out) {
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
	// Can reuse the old constants array - not touching constants in this pass.
	for (u32 value : in.GetConstants()) {
		out.AddConstant(value);
	}
	return logBlocks;
}

bool PropagateConstants(const IRWriter &in, IRWriter &out) {
	IRRegCache gpr(&out);

	const u32 *constants = !in.GetConstants().empty() ? &in.GetConstants()[0] : nullptr;
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
			if (gpr.IsImm(inst.src1) && inst.src1 != inst.dest) {
				gpr.MapIn(inst.dest);
				out.Write(inst.op, inst.dest, 0, out.AddConstant(gpr.GetImm(inst.src1) + constants[inst.src2]));
			} else {
				gpr.MapInIn(inst.dest, inst.src1);
				goto doDefault;
			}
			break;
		case IROp::StoreFloat:
		case IROp::StoreVec4:
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
		case IROp::LoadVec4:
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
		doDefaultAndFlush:
			gpr.FlushAll();
		doDefault:
			WriteInstWithConstants(in, out, constants, inst);
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

int IRDestGPR(const IRInst &inst) {
	const IRMeta *m = GetIRMeta(inst.op);

	if ((m->flags & IRFLAG_SRC3) == 0 && m->types[0] == 'G') {
		return inst.dest;
	}
	return -1;
}

bool PurgeTemps(const IRWriter &in, IRWriter &out) {
	std::vector<IRInst> insts;
	insts.reserve(in.GetInstructions().size());

	struct Check {
		Check(int r, int i, bool rbx) : reg(r), index(i), readByExit(rbx) {
		}

		int reg;
		int index;
		bool readByExit;
	};
	std::vector<Check> checks;

	bool logBlocks = false;
	for (int i = 0, n = (int)in.GetInstructions().size(); i < n; i++) {
		const IRInst &inst = in.GetInstructions()[i];
		const IRMeta *m = GetIRMeta(inst.op);

		for (Check &check : checks) {
			if (check.reg == 0) {
				continue;
			}

			if (IRReadsFromGPR(inst, check.reg)) {
				// Read from, so we can't optimize out.
				check.reg = 0;
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
		case IRTEMP_LHS:
		case IRTEMP_RHS:
			// Unlike other ops, these don't need to persist between blocks.
			// So we consider them not read unless proven read.
			checks.push_back(Check(dest, i, false));
			break;

		default:
			if (dest > IRTEMP_RHS) {
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

	for (u32 value : in.GetConstants()) {
		out.AddConstant(value);
	}
	for (const IRInst &inst : insts) {
		if (inst.op != IROp::Mov || inst.dest != 0 || inst.src1 != 0) {
			out.Write(inst);
		}
	}

	return logBlocks;
}

bool ReduceLoads(const IRWriter &in, IRWriter &out) {
	for (u32 value : in.GetConstants()) {
		out.AddConstant(value);
	}

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
						const u32 mask = in.GetConstants()[laterInst.src2];
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

static std::vector<IRInst> ReorderLoadStoreOps(std::vector<IRInst> &ops, const u32 *consts) {
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
			modifiesReg = true;
			if (ops[i].src1 == ops[i].dest) {
				// Can't ever reorder these, since it changes.
				continue;
			}
			break;

		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
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
				return consts[a.src2] < consts[b.src2];
			});
		}
	}

	return ops;
}

bool ReorderLoadStore(const IRWriter &in, IRWriter &out) {
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
		std::vector<IRInst> loadStoreSorted = ReorderLoadStoreOps(loadStoreQueue, &in.GetConstants()[0]);
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

	// Can reuse the old constants array - not touching constants in this pass.
	for (u32 value : in.GetConstants()) {
		out.AddConstant(value);
	}
	return logBlocks;
}

bool MergeLoadStore(const IRWriter &in, IRWriter &out) {
	bool logBlocks = false;

	auto opsCompatible = [&](const IRInst &a, const IRInst &b, int dist) {
		if (a.op != b.op || a.src1 != b.src1) {
			// Not similar enough at all.
			return false;
		}
		u32 off1 = in.GetConstants()[a.src2];
		u32 off2 = in.GetConstants()[b.src2];
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
			// Warning: this may generate unaligned stores.
			if (c == 2 || c == 3) {
				inst.op = IROp::Store16;
				out.Write(inst);
				prev = inst;
				// Skip the next one.
				++i;
				continue;
			}
			if (c == 4) {
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
			// Warning: this may generate unaligned stores.
			if (c == 2) {
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

	// Can reuse the old constants array - not touching constants in this pass.
	for (u32 value : in.GetConstants()) {
		out.AddConstant(value);
	}
	return logBlocks;
}
