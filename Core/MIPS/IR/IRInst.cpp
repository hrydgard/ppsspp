#include "Common/CommonFuncs.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/MIPSDebugInterface.h"

static const IRMeta irMeta[] = {
	{ IROp::SetConst, "SetConst", "GC" },
	{ IROp::SetConstF, "SetConstF", "FC" },
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
	{ IROp::Mult, "Mult", "_GG" },
	{ IROp::MultU, "MultU", "_GG" },
	{ IROp::Madd, "Madd", "_GG" },
	{ IROp::MaddU, "MaddU", "_GG" },
	{ IROp::Msub, "Msub", "_GG" },
	{ IROp::MsubU, "MsubU", "_GG" },
	{ IROp::Div, "Div", "_GG" },
	{ IROp::DivU, "DivU", "_GG" },
	{ IROp::MtLo, "MtLo", "_G" },
	{ IROp::MtHi, "MtHi", "_G" },
	{ IROp::MfLo, "MfLo", "G" },
	{ IROp::MfHi, "MfHi", "G" },
	{ IROp::Ext8to32, "Ext8to32", "GG" },
	{ IROp::Ext16to32, "Ext16to32", "GG" },
	{ IROp::Load8, "Load8", "GGC" },
	{ IROp::Load8Ext, "Load8", "GGC" },
	{ IROp::Load16, "Load16", "GGC" },
	{ IROp::Load16Ext, "Load16Ext", "GGC" },
	{ IROp::Load32, "Load32", "GGC" },
	{ IROp::LoadFloat, "LoadFloat", "FGC" },
	{ IROp::LoadVec4, "LoadVec4", "FGC" },
	{ IROp::Store8, "Store8", "GGC" },
	{ IROp::Store16, "Store16", "GGC" },
	{ IROp::Store32, "Store32", "GGC" },
	{ IROp::StoreFloat, "StoreFloat", "FGC" },
	{ IROp::StoreVec4, "StoreVec4", "FGC" },
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
	{ IROp::InitVec4, "InitVec4", "Fv"},
	{ IROp::FpCondToReg, "FpCondToReg", "G" },
	{ IROp::VfpuCtrlToReg, "VfpuCtrlToReg", "GI" },
	{ IROp::SetCtrlVFPU, "SetCtrlVFPU", "TC" },

	{ IROp::FSin, "FSin", "FF" },
	{ IROp::FCos, "FCos", "FF" },
	{ IROp::FSqrt, "FSqrt", "FF" },
	{ IROp::FRSqrt, "FRSqrt", "FF" },
	{ IROp::FRecip, "FRecip", "FF" },
	{ IROp::FAsin, "FAsin", "FF" },

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
	{ IROp::Syscall, "Syscall", "_C" },
	{ IROp::Break, "Break", ""},
	{ IROp::SetPC, "SetPC", "_G" },
	{ IROp::SetPCConst, "SetPC", "_C" },
	{ IROp::CallReplacement, "CallRepl", "_C"},
};

const IRMeta *metaIndex[256];

void InitIR() {
	for (size_t i = 0; i < ARRAY_SIZE(irMeta); i++) {
		metaIndex[(int)irMeta[i].op] = &irMeta[i];
	}
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
	if (constPool_.size() > 255) {
		// Cannot have more than 256 constants in a block!
		Crash();
	}
	return (int)constPool_.size() - 1;
}

int IRWriter::AddConstantFloat(float value) {
	u32 val;
	memcpy(&val, &value, 4);
	return AddConstant(val);
}

const char *GetGPRName(int r) {
	if (r < 32) {
		return currentDebugMIPS->GetRegName(0, r);
	}
	switch (r) {
	case IRTEMP_0: return "irtemp0";
	case IRTEMP_1: return "irtemp1";
	case IRTEMP_LHS: return "irtemp_lhs";
	case IRTEMP_RHS: return "irtemp_rhs";
	default: return "(unk)";
	}
}

void DisassembleParam(char *buf, int bufSize, u8 param, char type, const u32 *constPool) {
	static const char *vfpuCtrlNames[VFPU_CTRL_MAX] = {
		"SPFX",
		"TPFX",
		"DPFX",
		"CC",
		"INF4",
		"RSV5",
		"RSV6",
		"REV",
		"RCX0",
		"RCX1",
		"RCX2",
		"RCX3",
		"RCX4",
		"RCX5",
		"RCX6",
		"RCX7",
	};
	static const char *initVec4Names[8] = {
		"[0 0 0 0]",
		"[1 1 1 1]",
		"[-1 -1 -1 -1]",
		"[1 0 0 0]",
		"[0 1 0 0]",
		"[0 0 1 0]",
		"[0 0 0 1]",
	};

	switch (type) {
	case 'G':
		snprintf(buf, bufSize, "%s", GetGPRName(param));
		break;
	case 'F':
		if (param >= 32) {
			snprintf(buf, bufSize, "v%d", param - 32);
		} else {
			snprintf(buf, bufSize, "f%d", param);
		}
		break;
	case 'C':
		snprintf(buf, bufSize, "%08x", constPool[param]);
		break;
	case 'I':
		snprintf(buf, bufSize, "%02x", param);
		break;
	case 'T':
		snprintf(buf, bufSize, "%s", vfpuCtrlNames[param]);
		break;
	case 'v':
		snprintf(buf, bufSize, "%s", initVec4Names[param]);
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

const IRMeta *GetIRMeta(IROp op) {
	return metaIndex[(int)op];
}

void DisassembleIR(char *buf, size_t bufsize, IRInst inst, const u32 *constPool) {
	const IRMeta *meta = GetIRMeta(inst.op);
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
