#include "Common/CommonFuncs.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/MIPSDebugInterface.h"

// Legend
// ======================
//  _ = ignore
//  G = GPR register
//  C = 32-bit constant from array
//  I = immediate value from instruction
//  F = FPR register, single
//  V = FPR register, Vec4. Reg number always divisible by 4.
//  2 = FPR register, Vec2 (uncommon)
//  v = Vec4Init constant, chosen by immediate
//  s = Shuffle immediate (4 2-bit fields, choosing a xyzw shuffle)

static const IRMeta irMeta[] = {
	{ IROp::Nop, "Nop", "" },
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
	{ IROp::MovZ, "MovZ", "GGG", IRFLAG_SRC3DST },
	{ IROp::MovNZ, "MovNZ", "GGG", IRFLAG_SRC3DST },
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
	{ IROp::ReverseBits, "ReverseBits", "GG" },
	{ IROp::Load8, "Load8", "GGC" },
	{ IROp::Load8Ext, "Load8", "GGC" },
	{ IROp::Load16, "Load16", "GGC" },
	{ IROp::Load16Ext, "Load16Ext", "GGC" },
	{ IROp::Load32, "Load32", "GGC" },
	{ IROp::Load32Left, "Load32Left", "GGC", IRFLAG_SRC3DST },
	{ IROp::Load32Right, "Load32Right", "GGC", IRFLAG_SRC3DST },
	{ IROp::Load32Linked, "Load32Linked", "GGC" },
	{ IROp::LoadFloat, "LoadFloat", "FGC" },
	{ IROp::LoadVec4, "LoadVec4", "VGC" },
	{ IROp::Store8, "Store8", "GGC", IRFLAG_SRC3 },
	{ IROp::Store16, "Store16", "GGC", IRFLAG_SRC3 },
	{ IROp::Store32, "Store32", "GGC", IRFLAG_SRC3 },
	{ IROp::Store32Left, "Store32Left", "GGC", IRFLAG_SRC3 },
	{ IROp::Store32Right, "Store32Right", "GGC", IRFLAG_SRC3 },
	{ IROp::Store32Conditional, "Store32Conditional", "GGC", IRFLAG_SRC3DST },
	{ IROp::StoreFloat, "StoreFloat", "FGC", IRFLAG_SRC3 },
	{ IROp::StoreVec4, "StoreVec4", "VGC", IRFLAG_SRC3 },
	{ IROp::FAdd, "FAdd", "FFF" },
	{ IROp::FSub, "FSub", "FFF" },
	{ IROp::FMul, "FMul", "FFF" },
	{ IROp::FDiv, "FDiv", "FFF" },
	{ IROp::FMin, "FMin", "FFF" },
	{ IROp::FMax, "FMax", "FFF" },
	{ IROp::FMov, "FMov", "FF" },
	{ IROp::FSqrt, "FSqrt", "FF" },
	{ IROp::FSin, "FSin", "FF" },
	{ IROp::FCos, "FCos", "FF" },
	{ IROp::FSqrt, "FSqrt", "FF" },
	{ IROp::FRSqrt, "FRSqrt", "FF" },
	{ IROp::FRecip, "FRecip", "FF" },
	{ IROp::FAsin, "FAsin", "FF" },
	{ IROp::FNeg, "FNeg", "FF" },
	{ IROp::FSign, "FSign", "FF" },
	{ IROp::FAbs, "FAbs", "FF" },
	{ IROp::FRound, "FRound", "FF" },
	{ IROp::FTrunc, "FTrunc", "FF" },
	{ IROp::FCeil, "FCeil", "FF" },
	{ IROp::FFloor, "FFloor", "FF" },
	{ IROp::FCvtWS, "FCvtWS", "FF" },
	{ IROp::FCvtSW, "FCvtSW", "FF" },
	{ IROp::FCvtScaledWS, "FCvtScaledWS", "FFI" },
	{ IROp::FCvtScaledSW, "FCvtScaledSW", "FFI" },
	{ IROp::FCmp, "FCmp", "mFF" },
	{ IROp::FSat0_1, "FSat(0 - 1)", "FF" },
	{ IROp::FSatMinus1_1, "FSat(-1 - 1)", "FF" },
	{ IROp::FMovFromGPR, "FMovFromGPR", "FG" },
	{ IROp::FMovToGPR, "FMovToGPR", "GF" },
	{ IROp::FpCondFromReg, "FpCondFromReg", "_G" },
	{ IROp::FpCondToReg, "FpCondToReg", "G" },
	{ IROp::FpCtrlFromReg, "FpCtrlFromReg", "_G" },
	{ IROp::FpCtrlToReg, "FpCtrlToReg", "G" },
	{ IROp::VfpuCtrlToReg, "VfpuCtrlToReg", "GT" },
	{ IROp::SetCtrlVFPU, "SetCtrlVFPU", "TC" },
	{ IROp::SetCtrlVFPUReg, "SetCtrlVFPUReg", "TG" },
	{ IROp::SetCtrlVFPUFReg, "SetCtrlVFPUFReg", "TF" },
	{ IROp::FCmovVfpuCC, "FCmovVfpuCC", "FFI", IRFLAG_SRC3DST },
	{ IROp::FCmpVfpuBit, "FCmpVfpuBit", "IFF" },
	{ IROp::FCmpVfpuAggregate, "FCmpVfpuAggregate", "I" },
	{ IROp::Vec4Init, "Vec4Init", "Vv" },
	{ IROp::Vec4Shuffle, "Vec4Shuffle", "VVs" },
	{ IROp::Vec4Blend, "Vec4Blend", "VVVC" },
	{ IROp::Vec4Mov, "Vec4Mov", "VV" },
	{ IROp::Vec4Add, "Vec4Add", "VVV" },
	{ IROp::Vec4Sub, "Vec4Sub", "VVV" },
	{ IROp::Vec4Div, "Vec4Div", "VVV" },
	{ IROp::Vec4Mul, "Vec4Mul", "VVV" },
	{ IROp::Vec4Scale, "Vec4Scale", "VVF" },
	{ IROp::Vec4Dot, "Vec4Dot", "FVV" },
	{ IROp::Vec4Neg, "Vec4Neg", "VV" },
	{ IROp::Vec4Abs, "Vec4Abs", "VV" },

		// Pack/Unpack
	{ IROp::Vec2Unpack16To31, "Vec2Unpack16To31", "2F" },  // Note that the result is shifted down by 1, hence 31
	{ IROp::Vec2Unpack16To32, "Vec2Unpack16To32", "2F" },
	{ IROp::Vec4Unpack8To32, "Vec4Unpack8To32", "VF" },
	{ IROp::Vec4DuplicateUpperBitsAndShift1, "Vec4DuplicateUpperBitsAndShift1", "VV" },

	{ IROp::Vec4ClampToZero, "Vec4ClampToZero", "VV" },
	{ IROp::Vec2ClampToZero, "Vec2ClampToZero", "22" },
	{ IROp::Vec4Pack32To8, "Vec4Pack32To8", "FV" },
	{ IROp::Vec4Pack31To8, "Vec4Pack31To8", "FV" },
	{ IROp::Vec2Pack32To16, "Vec2Pack32To16", "F2" },
	{ IROp::Vec2Pack31To16, "Vec2Pack31To16", "F2" },

	{ IROp::Interpret, "Interpret", "_C", IRFLAG_BARRIER },
	{ IROp::Downcount, "Downcount", "_C" },
	{ IROp::ExitToPC, "ExitToPC", "", IRFLAG_EXIT },
	{ IROp::ExitToConst, "Exit", "C", IRFLAG_EXIT },
	{ IROp::ExitToConstIfEq, "ExitIfEq", "CGG", IRFLAG_EXIT },
	{ IROp::ExitToConstIfNeq, "ExitIfNeq", "CGG", IRFLAG_EXIT },
	{ IROp::ExitToConstIfGtZ, "ExitIfGtZ", "CG", IRFLAG_EXIT },
	{ IROp::ExitToConstIfGeZ, "ExitIfGeZ", "CG", IRFLAG_EXIT },
	{ IROp::ExitToConstIfLeZ, "ExitIfLeZ", "CG", IRFLAG_EXIT },
	{ IROp::ExitToConstIfLtZ, "ExitIfLtZ", "CG", IRFLAG_EXIT },
	{ IROp::ExitToReg, "ExitToReg", "_G", IRFLAG_EXIT },
	{ IROp::Syscall, "Syscall", "_C", IRFLAG_EXIT },
	{ IROp::Break, "Break", "", IRFLAG_EXIT },
	{ IROp::SetPC, "SetPC", "_G" },
	{ IROp::SetPCConst, "SetPC", "_C" },
	{ IROp::CallReplacement, "CallRepl", "GC", IRFLAG_BARRIER },
	{ IROp::Breakpoint, "Breakpoint", "_C", IRFLAG_BARRIER },
	{ IROp::MemoryCheck, "MemoryCheck", "IGC", IRFLAG_BARRIER },

	{ IROp::ValidateAddress8, "ValidAddr8", "_GC", IRFLAG_BARRIER },
	{ IROp::ValidateAddress16, "ValidAddr16", "_GC", IRFLAG_BARRIER },
	{ IROp::ValidateAddress32, "ValidAddr32", "_GC", IRFLAG_BARRIER },
	{ IROp::ValidateAddress128, "ValidAddr128", "_GC", IRFLAG_BARRIER },

	{ IROp::RestoreRoundingMode, "RestoreRoundingMode", "" },
	{ IROp::ApplyRoundingMode, "ApplyRoundingMode", "" },
	{ IROp::UpdateRoundingMode, "UpdateRoundingMode", "" },
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
	inst.constant = nextConst_;
	insts_.push_back(inst);

	nextConst_ = 0;
}

void IRWriter::WriteSetConstant(u8 dst, u32 value) {
	Write(IROp::SetConst, dst, AddConstant(value));
}

int IRWriter::AddConstant(u32 value) {
	nextConst_ = value;
	return 255;
}

int IRWriter::AddConstantFloat(float value) {
	u32 val;
	memcpy(&val, &value, 4);
	return AddConstant(val);
}

static std::string GetGPRName(int r) {
	if (r < 32) {
		return currentDebugMIPS->GetRegName(0, r);
	}
	switch (r) {
	case IRTEMP_0: return "irtemp0";
	case IRTEMP_1: return "irtemp1";
	case IRTEMP_2: return "irtemp2";
	case IRTEMP_3: return "irtemp3";
	case IRTEMP_LHS: return "irtemp_lhs";
	case IRTEMP_RHS: return "irtemp_rhs";
	case IRTEMP_LR_ADDR: return "irtemp_addr";
	case IRTEMP_LR_VALUE: return "irtemp_value";
	case IRTEMP_LR_MASK: return "irtemp_mask";
	case IRTEMP_LR_SHIFT: return "irtemp_shift";
	default: return "(unk)";
	}
}

void DisassembleParam(char *buf, int bufSize, u8 param, char type, u32 constant) {
	static const char * const vfpuCtrlNames[VFPU_CTRL_MAX] = {
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
	static const char * const initVec4Names[8] = {
		"[0 0 0 0]",
		"[1 1 1 1]",
		"[-1 -1 -1 -1]",
		"[1 0 0 0]",
		"[0 1 0 0]",
		"[0 0 1 0]",
		"[0 0 0 1]",
	};
	static const char * const xyzw = "xyzw";

	switch (type) {
	case 'G':
		snprintf(buf, bufSize, "%s", GetGPRName(param).c_str());
		break;
	case 'F':
		if (param >= 32) {
			snprintf(buf, bufSize, "vf%d", param - 32);
		} else {
			snprintf(buf, bufSize, "f%d", param);
		}
		break;
	case 'V':
		if (param >= 32) {
			snprintf(buf, bufSize, "vf%d..vf%d", param - 32, param - 32 + 3);
		} else {
			snprintf(buf, bufSize, "f%d..f%d", param, param + 3);
		}
		break;
	case '2':
		if (param >= 32) {
			snprintf(buf, bufSize, "vf%d,vf%d", param - 32, param - 32 + 1);
		} else {
			snprintf(buf, bufSize, "f%d,f%d", param, param + 1);
		}
		break;
	case 'C':
		snprintf(buf, bufSize, "%08x", constant);
		break;
	case 'I':
		snprintf(buf, bufSize, "%02x", param);
		break;
	case 'm':
		snprintf(buf, bufSize, "%d", param);
		break;
	case 'T':
		snprintf(buf, bufSize, "%s", vfpuCtrlNames[param]);
		break;
	case 'v':
		snprintf(buf, bufSize, "%s", initVec4Names[param]);
		break;
	case 's':
		snprintf(buf, bufSize, "%c%c%c%c", xyzw[param & 3], xyzw[(param >> 2) & 3], xyzw[(param >> 4) & 3], xyzw[(param >> 6) & 3]);
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

void DisassembleIR(char *buf, size_t bufsize, IRInst inst) {
	const IRMeta *meta = GetIRMeta(inst.op);
	if (!meta) {
		snprintf(buf, bufsize, "Unknown %d", (int)inst.op);
		return;
	}
	char bufDst[16];
	char bufSrc1[16];
	char bufSrc2[16];
	// Only really used for constant.
	char bufSrc3[16];
	DisassembleParam(bufDst, sizeof(bufDst) - 2, inst.dest, meta->types[0], inst.constant);
	DisassembleParam(bufSrc1, sizeof(bufSrc1) - 2, inst.src1, meta->types[1], inst.constant);
	DisassembleParam(bufSrc2, sizeof(bufSrc2), inst.src2, meta->types[2], inst.constant);
	DisassembleParam(bufSrc3, sizeof(bufSrc3), inst.src3, meta->types[3], inst.constant);
	if (meta->types[1] && meta->types[0] != '_') {
		strcat(bufDst, ", ");
	}
	if (meta->types[2] && meta->types[1] != '_') {
		strcat(bufSrc1, ", ");
	}
	if (meta->types[3] && meta->types[2] != '_') {
		strcat(bufSrc2, ", ");
	}
	snprintf(buf, bufsize, "%s %s%s%s%s", meta->name, bufDst, bufSrc1, bufSrc2, bufSrc3);
}
