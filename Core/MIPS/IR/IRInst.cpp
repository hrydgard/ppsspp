#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"

IRMeta meta[] = {
	{ IROp::SetConst, "SetConst", "GC_" },
	{ IROp::Mov, "Mov", "GG" },
	{ IROp::Add, "Add", "GGG" },
	{ IROp::Sub, "Sub", "GGG" },
	{ IROp::Neg, "Neg", "GG" },
	{ IROp::Not, "Not", "GG" },
	{ IROp::And, "And", "GGG" },
	{ IROp::Or, "Or", "GGG" },
	{ IROp::Xor, "Xor", "GGG" },
	{ IROp::AddConst, "AddConst", "GGC" },
	{ IROp::SubConst, "SubConst", "GGC" },
	{ IROp::AndConst, "AndConst", "GGC" },
	{ IROp::OrConst, "OrConst", "GGC" },
	{ IROp::XorConst, "XorConst", "GGC" },
	{ IROp::Shl, "Shl", "GGG" },
	{ IROp::Shr, "Shr", "GGG" },
	{ IROp::Sar, "Sar", "GGG" },
	{ IROp::Ror, "Ror", "GGG" },
	{ IROp::ShlImm, "ShlImm", "GGI" },
	{ IROp::ShrImm, "ShrImm", "GGI" },
	{ IROp::SarImm, "SarImm", "GGI" },
	{ IROp::RorImm, "RorImm", "GGI" },
	{ IROp::Slt, "Slt", "GGG" },
	{ IROp::SltConst, "SltConst", "GGC" },
	{ IROp::SltU, "SltU", "GGG" },
	{ IROp::SltUConst, "SltUConst", "GGC" },
	{ IROp::Clz, "Clz", "GG" },
	{ IROp::MovZ, "MovZ", "GGG" },
	{ IROp::MovNZ, "MovNZ", "GGG" },
	{ IROp::Max, "Max", "GGG" },
	{ IROp::Min, "Min", "GGG" },
	{ IROp::BSwap16, "BSwap16", "GG" },
	{ IROp::BSwap32, "BSwap32", "GG" },
	{ IROp::Mul, "Mul", "_GG" },
	{ IROp::Ext8to32, "Ext8to32", "GG" },
	{ IROp::Ext16to32, "Ext16to32", "GG" },
	{ IROp::Load8, "Load8", "GGC" },
	{ IROp::Load8Ext, "Load8", "GGC" },
	{ IROp::Load16, "Load16", "GGC" },
	{ IROp::Load16Ext, "Load16Ext", "GGC" },
	{ IROp::Load32, "Load32", "GGC" },
	{ IROp::Store8, "Store8", "GGC" },
	{ IROp::Store16, "Store16", "GGC" },
	{ IROp::Store32, "Store32", "GGC" },
	{ IROp::FAdd, "FAdd", "FFF" },
	{ IROp::FSub, "FSub", "FFF" },
	{ IROp::FMul, "FMul", "FFF" },
	{ IROp::FDiv, "FDiv", "FFF" },
	{ IROp::FMov, "FMov", "FF" },
	{ IROp::FSqrt, "FSqrt", "FF" },
	{ IROp::FNeg, "FNeg", "FF" },
	{ IROp::FAbs, "FAbs", "FF" },
	{ IROp::FRound, "FRound", "FF" },
	{ IROp::FTrunc, "FTrunc", "FF" },
	{ IROp::FCeil, "FCeil", "FF" },
	{ IROp::FFloor, "FFloor", "FF" },
	{ IROp::FCvtWS, "FCvtWS", "FF" },
	{ IROp::FCvtSW, "FCvtSW", "FF" },
	{ IROp::FMovFromGPR, "FMovFromGPR", "FG" },
	{ IROp::FMovToGPR, "FMovToGPR", "GF" },
	{ IROp::FpCondToReg, "FpCondToReg", "G" },
	{ IROp::SetCtrlVFPU, "SetCtrlVFPU", "T" },
	{ IROp::Interpret, "Interpret", "_C" },
	{ IROp::Downcount, "Downcount", "_II" },
	{ IROp::ExitToConst, "Exit", "C" },
	{ IROp::ExitToConstIfEq, "ExitIfEq", "CGG" },
	{ IROp::ExitToConstIfNeq, "ExitIfNeq", "CGG" },
	{ IROp::ExitToConstIfGtZ, "ExitIfGtZ", "CG" },
	{ IROp::ExitToConstIfGeZ, "ExitIfGeZ", "CG" },
	{ IROp::ExitToConstIfLeZ, "ExitIfLeZ", "CG" },
	{ IROp::ExitToConstIfLtZ, "ExitIfLtZ", "CG" },
	{ IROp::ExitToReg, "ExitToReg", "G" },
	{ IROp::Syscall, "Syscall", "_C"},
	{ IROp::SetPC, "SetPC", "_G"},
};

const IRMeta *metaIndex[256];

void InitIR() {
	for (size_t i = 0; i < ARRAY_SIZE(meta); i++) {
		metaIndex[(int)meta[i].op] = &meta[i];
	}
}

u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count) {
	const IRInst *end = inst + count;
	while (inst != end) {
		switch (inst->op) {
		case IROp::SetConst:
			mips->r[inst->dest] = constPool[inst->src1];
			break;
		case IROp::Add:
			mips->r[inst->dest] = mips->r[inst->src1] + mips->r[inst->src2];
			break;
		case IROp::Sub:
			mips->r[inst->dest] = mips->r[inst->src1] - mips->r[inst->src2];
			break;
		case IROp::And:
			mips->r[inst->dest] = mips->r[inst->src1] & mips->r[inst->src2];
			break;
		case IROp::Or:
			mips->r[inst->dest] = mips->r[inst->src1] | mips->r[inst->src2];
			break;
		case IROp::Xor:
			mips->r[inst->dest] = mips->r[inst->src1] ^ mips->r[inst->src2];
			break;
		case IROp::Mov:
			mips->r[inst->dest] = mips->r[inst->src1];
			break;
		case IROp::AddConst:
			mips->r[inst->dest] = mips->r[inst->src1] + constPool[inst->src2];
			break;
		case IROp::SubConst:
			mips->r[inst->dest] = mips->r[inst->src1] - constPool[inst->src2];
			break;
		case IROp::AndConst:
			mips->r[inst->dest] = mips->r[inst->src1] & constPool[inst->src2];
			break;
		case IROp::OrConst:
			mips->r[inst->dest] = mips->r[inst->src1] | constPool[inst->src2];
			break;
		case IROp::XorConst:
			mips->r[inst->dest] = mips->r[inst->src1] ^ constPool[inst->src2];
			break;
		case IROp::Neg:
			mips->r[inst->dest] = -(s32)mips->r[inst->src1];
			break;
		case IROp::Not:
			mips->r[inst->dest] = ~mips->r[inst->src1];
			break;
		case IROp::Ext8to32:
			mips->r[inst->dest] = (s32)(s8)mips->r[inst->src1];
			break;
		case IROp::Ext16to32:
			mips->r[inst->dest] = (s32)(s16)mips->r[inst->src1];
			break;

		case IROp::Load8:
			mips->r[inst->dest] = Memory::ReadUnchecked_U8(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load8Ext:
			mips->r[inst->dest] = (s32)(s8)Memory::ReadUnchecked_U8(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load16:
			mips->r[inst->dest] = Memory::ReadUnchecked_U16(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load16Ext:
			mips->r[inst->dest] = (s32)(s16)Memory::ReadUnchecked_U16(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load32:
			mips->r[inst->dest] = Memory::ReadUnchecked_U32(mips->r[inst->src1] + constPool[inst->src2]);
			break;

		case IROp::Store8:
			Memory::WriteUnchecked_U8(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Store16:
			Memory::WriteUnchecked_U16(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Store32:
			Memory::WriteUnchecked_U32(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;

		case IROp::ShlImm:
			mips->r[inst->dest] = mips->r[inst->src1] << (int)inst->src2;
			break;
		case IROp::ShrImm:
			mips->r[inst->dest] = mips->r[inst->src1] >> (int)inst->src2;
			break;
		case IROp::SarImm:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (int)inst->src2;
			break;
		case IROp::RorImm:
		{
			u32 x = mips->r[inst->src1];
			int sa = inst->src2;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
		}
			break;

		case IROp::Shl:
			mips->r[inst->dest] = mips->r[inst->src1] << (mips->r[inst->src2] & 31);
			break;
		case IROp::Shr:
			mips->r[inst->dest] = mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			break;
		case IROp::Sar:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			break;
		case IROp::Ror:
		{
			u32 x = mips->r[inst->src1];
			int sa = mips->r[inst->src2] & 31;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
		}
		break;

		case IROp::Clz:
		{
			int x = 31;
			int count = 0;
			int value = mips->r[inst->src1];
			while (x >= 0 && !(value & (1 << x))) {
				count++;
				x--;
			}
			mips->r[inst->dest] = count;
			break;
		}

		case IROp::Slt:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2];
			break;

		case IROp::SltU:
			mips->r[inst->dest] = mips->r[inst->src1] < mips->r[inst->src2];
			break;

		case IROp::SltConst:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)constPool[inst->src2];
			break;

		case IROp::SltUConst:
			mips->r[inst->dest] = mips->r[inst->src1] < constPool[inst->src2];
			break;

		case IROp::MovZ:
			if (mips->r[inst->src1] == 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			break;
		case IROp::MovNZ:
			if (mips->r[inst->src1] != 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			break;

		case IROp::Max:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] > (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			break;
		case IROp::Min:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			break;

		case IROp::BSwap16:
		{
			u32 x = mips->r[inst->src1];
			mips->r[inst->dest] = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);
			break;
		}
		case IROp::BSwap32:
			mips->r[inst->dest] = swap32(mips->r[inst->src1]);
			break;

		case IROp::FAdd:
			mips->f[inst->dest] = mips->f[inst->src1] + mips->f[inst->src2];
			break;
		case IROp::FSub:
			mips->f[inst->dest] = mips->f[inst->src1] - mips->f[inst->src2];
			break;
		case IROp::FMul:
			mips->f[inst->dest] = mips->f[inst->src1] * mips->f[inst->src2];
			break;
		case IROp::FDiv:
			mips->f[inst->dest] = mips->f[inst->src1] / mips->f[inst->src2];
			break;

		case IROp::FMov:
			mips->f[inst->dest] = mips->f[inst->src1];
			break;
		case IROp::FAbs:
			mips->f[inst->dest] = fabsf(mips->f[inst->src1]);
			break;
		case IROp::FSqrt:
			mips->f[inst->dest] = sqrtf(mips->f[inst->src1]);
			break;
		case IROp::FNeg:
			mips->f[inst->dest] = -mips->f[inst->src1];
			break;
		case IROp::FpCondToReg:
			mips->r[inst->dest] = mips->fpcond;
			break;

		case IROp::ExitToConst:
			return constPool[inst->dest];

		case IROp::ExitToReg:
			return mips->r[inst->dest];

		case IROp::ExitToConstIfEq:
			if (mips->r[inst->src1] == mips->r[inst->src2])
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfNeq:
			if (mips->r[inst->src1] != mips->r[inst->src2])
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfGtZ:
			if ((s32)mips->r[inst->src1] > 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfGeZ:
			if ((s32)mips->r[inst->src1] >= 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfLtZ:
			if ((s32)mips->r[inst->src1] < 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfLeZ:
			if ((s32)mips->r[inst->src1] <= 0)
				return constPool[inst->dest];
			break;

		case IROp::Downcount:
			mips->downcount -= (inst->src1) | ((inst->src2) << 8);
			break;

		case IROp::SetPC:
			mips->pc = mips->r[inst->src1];
			break;

		case IROp::Syscall:
			// SetPC was executed before.
		{
			MIPSOpcode op(constPool[inst->src1]);
			CallSyscall(op);
			return mips->pc;
		}

		case IROp::Interpret:  // SLOW fallback. Can be made faster.
		{
			MIPSOpcode op(constPool[inst->src1]);
			MIPSInterpret(op);
			break;
		}

		default:
			Crash();
		}
		inst++;
	}

	// If we got here, the block was badly constructed.
	// Crash();
	return 0;
}

void IRWriter::Write(IROp op, u8 dst, u8 src1, u8 src2) {
	IRInst inst;
	inst.op = op;
	inst.dest = dst;
	inst.src1 = src1;
	inst.src2 = src2;
	insts_.push_back(inst);
}

void IRWriter::WriteSetConstant(u8 dst, u32 value) {
	Write(IROp::SetConst, dst, AddConstant(value));
}

int IRWriter::AddConstant(u32 value) {
	for (size_t i = 0; i < constPool_.size(); i++) {
		if (constPool_[i] == value)
			return (int)i;
	}
	constPool_.push_back(value);
	return (int)constPool_.size() - 1;
}

int IRWriter::AddConstantFloat(float value) {
	u32 val;
	memcpy(&val, &value, 4);
	return AddConstant(val);
}

void IRWriter::Simplify() {
	SimplifyInPlace(&insts_[0], insts_.size(), constPool_.data());
}

const char *GetGPRName(int r) {
	if (r < 32) {
		return currentDebugMIPS->GetRegName(0, r);
	}
	switch (r) {
	case IRTEMP_0: return "irtemp0";
	case IRTEMP_1: return "irtemp1";
	default: return "(unk)";
	}
}

void DisassembleParam(char *buf, int bufSize, u8 param, char type, const u32 *constPool) {
	switch (type) {
	case 'G':
		snprintf(buf, bufSize, "%s", GetGPRName(param));
		break;
	case 'F':
		snprintf(buf, bufSize, "r%d", param);
		break;
	case 'C':
		snprintf(buf, bufSize, "%08x", constPool[param]);
		break;
	case 'I':
		snprintf(buf, bufSize, "%02x", param);
		break;
	case '_':
	case '\0':
		buf[0] = 0;
		break;
	default:
		snprintf(buf, bufSize, "?");
		break;
	}
}

void DisassembleIR(char *buf, size_t bufsize, IRInst inst, const u32 *constPool) {
	const IRMeta *meta = metaIndex[(int)inst.op];
	if (!meta) {
		snprintf(buf, bufsize, "Unknown %d", (int)inst.op);
		return;
	}
	char bufDst[16];
	char bufSrc1[16];
	char bufSrc2[16];
	DisassembleParam(bufDst, sizeof(bufDst) - 2, inst.dest, meta->types[0], constPool);
	DisassembleParam(bufSrc1, sizeof(bufSrc1) - 2, inst.src1, meta->types[1], constPool);
	DisassembleParam(bufSrc2, sizeof(bufSrc2), inst.src2, meta->types[2], constPool);
	if (meta->types[1] && meta->types[0] != '_') {
		strcat(bufDst, ", ");
	}
	if (meta->types[2] && meta->types[1] != '_') {
		strcat(bufSrc1, ", ");
	}
	snprintf(buf, bufsize, "%s %s%s%s", meta->name, bufDst, bufSrc1, bufSrc2);
}
