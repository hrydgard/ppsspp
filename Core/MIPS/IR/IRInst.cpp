#include "Core/MIPS/IR/IRInst.h"
#include "Core/MemMap.h"

IRMeta meta[] = {
	{ IROp::SetConst, "SetConst", "GC" },
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
	{ IROp::Slt, "Slt","GGC" },
	{ IROp::SltConst, "SltConst","GGC" },
	{ IROp::SltU, "SltU", "GGC" },
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
	{ IROp::Syscall, "Syscall", "_C"},
	{ IROp::SetPC, "SetPC", "_C"},
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
		case IROp::Neg:
			mips->r[inst->dest] = -(s32)mips->r[inst->src1];
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
			mips->r[inst->dest] = mips->r[inst->src1] << inst->src2;
			break;
		case IROp::ShrImm:
			mips->r[inst->dest] = mips->r[inst->src1] >> inst->src2;
			break;
		case IROp::SarImm:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> inst->src2;
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
			return constPool[inst->src1];

		case IROp::ExitToReg:
			return mips->r[inst->src1];

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

		case IROp::SetPC:
			return mips->pc = mips->r[inst->src1];

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
	// TODO: Check for the fixed ones first.
	Write(IROp::SetConstImm, AddConstant(value));
}

int IRWriter::AddConstant(u32 value) {
	for (size_t i = 0; i < constPool_.size(); i++) {
		if (constPool_[i] == value)
			return i;
	}
	constPool_.push_back(value);
	return (int)constPool_.size() - 1;
}

int IRWriter::AddConstantFloat(float value) {
	u32 val;
	memcpy(&val, &value, 4);
	return AddConstant(val);
}

void DisassembleParam(char *buf, int bufSize, u8 param, char type, const u32 *constPool) {
	switch (type) {
	case 'G':
		snprintf(buf, bufSize, "r%d", param);
		break;
	case 'F':
		snprintf(buf, bufSize, "r%d", param);
		break;
	case 'C':
		snprintf(buf, bufSize, "%08x", constPool[param]);
		break;
	default:
		snprintf(buf, bufSize, "?");
		break;
	}
}

void DisassembleIR(char *buf, size_t bufsize, IRInst inst, const u32 *constPool) {
	const IRMeta *meta = metaIndex[(int)inst.op];
	char bufDst[16];
	char bufSrc1[16];
	char bufSrc2[16];
	DisassembleParam(bufDst, sizeof(bufDst) - 2, inst.dest, meta->types[0], constPool);
	DisassembleParam(bufSrc1, sizeof(bufSrc1) - 2, inst.dest, meta->types[1], constPool);
	DisassembleParam(bufSrc2, sizeof(bufSrc2), inst.dest, meta->types[2], constPool);
	if (meta->types[1]) {
		strcat(bufDst, ", ");
	}
	if (meta->types[2]) {
		strcat(bufSrc1, ", ");
	}
	snprintf(buf, bufsize, "%s %s%s%s", meta->name, bufDst, bufSrc1, bufSrc2);
}