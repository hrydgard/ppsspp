#pragma once

#include <vector>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

// Basic IR
//
// This IR refers implicitly to the MIPS register set and is simple to interpret.
// To do real compiler things with it and do full-function compilation, it probably
// needs to be lifted to a higher IR first, before being lowered onto each target.
// But this gets rid of a lot of MIPS idiosyncrasies that makes it tricky, like
// delay slots, and is very suitable for translation into other IRs. Can of course
// even be directly JIT-ed, but the gains will probably be tiny over our older direct
// MIPS->target JITs.

enum class IROp : u8 {
	SetConst,
	FSetConst,

	Mov,

	Add,
	Sub,
	Neg,
	Not,

	And,
	Or,
	Xor,

	AddConst,
	SubConst,

	AndConst,
	OrConst,
	XorConst,

	Shl,
	Shr,
	Sar,
	Ror,

	// The shift is stored directly, not in the const table, so Imm instead of Const
	ShlImm,
	ShrImm,
	SarImm,
	RorImm,
		
	Slt,
	SltConst,
	SltU,
	SltUConst,

	Clz,

	// Conditional moves
	MovZ,
	MovNZ,

	Max,
	Min,

	// Byte swaps. All CPUs have native ones so worth keeping.
	BSwap16,  // Swaps both the high and low byte pairs.
	BSwap32,

	// Weird Hi/Lo semantics preserved. Too annoying to do something more generic.
	MtLo,
	MtHi,
	MfLo,
	MfHi,
	Mult,
	MultU,
	Madd,
	MaddU,
	Msub,
	MsubU,
	Div,
	DivU,
	
	// These take a constant from the pool as an offset.
	// Loads from a constant address can be represented by using r0.
	Load8,
	Load8Ext,
	Load16,
	Load16Ext,
	Load32,
	LoadFloat,

	Store8,
	Store16,
	Store32,
	StoreFloat,

	Ext8to32,
	Ext16to32,

	FAdd,
	FSub,
	FMul,
	FDiv,

	FMov,
	FSqrt,
	FNeg,
	FAbs,

	FRound,
	FTrunc,
	FCeil,
	FFloor,

	FCvtWS,
	FCvtSW,

	FMovFromGPR,
	FMovToGPR,

	FpCondToReg,
	VfpuCtrlToReg,

	ZeroFpCond,
	FCmpUnordered,
	FCmpEqual,
	FCmpEqualUnordered,
	FCmpLessOrdered,
	FCmpLessUnordered,
	FCmpLessEqualOrdered,
	FCmpLessEqualUnordered,

	// Rounding Mode
	RestoreRoundingMode,
	ApplyRoundingMode,
	UpdateRoundingMode,

	SetCtrlVFPU,

	// Fake/System instructions
	Interpret,

	// Emit this before you exits. Semantic is to set the downcount
	// that will be used at the actual exit.
	Downcount,  // src1 + (src2<<8)

	// End-of-basic-block.
	ExitToConst,   // 0, const, downcount
	ExitToReg,
	ExitToConstIfEq,  // const, reg1, reg2
	ExitToConstIfNeq, // const, reg1, reg2
	ExitToConstIfGtZ,  // const, reg1, 0
	ExitToConstIfGeZ,  // const, reg1, 0
	ExitToConstIfLtZ,  // const, reg1, 0
	ExitToConstIfLeZ,  // const, reg1, 0

	ExitToConstIfFpTrue,
	ExitToConstIfFpFalse,

	Syscall,
	SetPC,  // hack to make syscall returns work
	SetPCConst,  // hack to make replacement know PC
	CallReplacement,
	Break,
};

enum IRComparison {
	Greater,
	GreaterEqual,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	Bad,
};

// Hm, unused
inline IRComparison Invert(IRComparison comp) {
	switch (comp) {
	case IRComparison::Equal: return IRComparison::NotEqual;
	case IRComparison::NotEqual: return IRComparison::Equal;
	case IRComparison::Greater: return IRComparison::LessEqual;
	case IRComparison::GreaterEqual: return IRComparison::Less;
	case IRComparison::Less: return IRComparison::GreaterEqual;
	case IRComparison::LessEqual: return IRComparison::Greater;
	default:
		return IRComparison::Bad;
	}
}

inline IROp ComparisonToExit(IRComparison comp) {
	switch (comp) {
	case IRComparison::Equal: return IROp::ExitToConstIfEq;
	case IRComparison::NotEqual: return IROp::ExitToConstIfNeq;
	case IRComparison::Greater: return IROp::ExitToConstIfGtZ;
	case IRComparison::GreaterEqual: return IROp::ExitToConstIfGeZ;
	case IRComparison::Less: return IROp::ExitToConstIfLtZ;
	case IRComparison::LessEqual: return IROp::ExitToConstIfLeZ;
	default:
		return IROp::Break;
	}
}

enum {
	IRTEMP_0 = 192,
	IRTEMP_1,
	IRTEMP_LHS,  // Reserved for use in branches
	IRTEMP_RHS,  // Reserved for use in branches

	// Hacky way to get to other state
	IRREG_LO = 226,  // offset of lo in MIPSState / 4
	IRREG_HI = 227,
	IRREG_FCR31 = 228,
	IRREG_FPCOND = 229
};

enum class IRParam {
	Ignore = '_',
	UImm8 = 'U',
	Const = 'C',
	GPR = 'G',
	FPR = 'F',
	VPR = 'V',
	VCtrl = 'T',
};

struct IRMeta {
	IROp op;
	const char *name;
	const char types[4];  // GGG  
	u32 flags;
};

// 32 bits.
struct IRInst {
	IROp op;
	union {
		u8 dest;
		u8 src3;
	};
	u8 src1;
	u8 src2;
};

// Returns the new PC.
u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count);

// Each IR block gets a constant pool.
class IRWriter {
public:
	void Write(IROp op, u8 dst = 0, u8 src1 = 0, u8 src2 = 0);
	void Write(IRInst inst) {
		insts_.push_back(inst);
	}
	void WriteSetConstant(u8 dst, u32 value);

	int AddConstant(u32 value);
	int AddConstantFloat(float value);

	void Clear() {
		insts_.clear();
		constPool_.clear();
	}

	void Simplify();

	const std::vector<IRInst> &GetInstructions() const { return insts_; }
	const std::vector<u32> &GetConstants() const { return constPool_; }

private:
	std::vector<IRInst> insts_;
	std::vector<u32> constPool_;
};

const IRMeta *GetIRMeta(IROp op);
void DisassembleIR(char *buf, size_t bufsize, IRInst inst, const u32 *constPool);
void InitIR();
