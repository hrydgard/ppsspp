#pragma once

#include <vector>
#include <utility>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

#ifdef __SYMBIAN32__
// Seems std::move() doesn't exist, so assuming it can't do moves at all.
namespace std {
	template <typename T>
	const T &move(const T &x) {
		return x;
	}
};
#endif

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
	Nop,

	SetConst,
	SetConstF,

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
	LoadVec4,

	Store8,
	Store16,
	Store32,
	StoreFloat,
	StoreVec4,

	Ext8to32,
	Ext16to32,
	ReverseBits,

	FAdd,
	FSub,
	FMul,
	FDiv,
	FMin,
	FMax,

	FMov,
	FSqrt,
	FNeg,
	FAbs,
	FSign,

	FRound,
	FTrunc,
	FCeil,
	FFloor,

	FCvtWS,
	FCvtSW,

	FMovFromGPR,
	FMovToGPR,

	FSat0_1,
	FSatMinus1_1,

	FpCondToReg,
	VfpuCtrlToReg,

	ZeroFpCond,
	FCmp,

	FCmovVfpuCC,
	FCmpVfpuBit,
	FCmpVfpuAggregate,

	// Rounding Mode
	RestoreRoundingMode,
	ApplyRoundingMode,
	UpdateRoundingMode,

	SetCtrlVFPU,
	SetCtrlVFPUReg,
	SetCtrlVFPUFReg,

	// 4-wide instructions to assist SIMD.
	// Can of course add a pass to break them up if a target does not
	// support SIMD.
	Vec4Init,
	Vec4Shuffle,
	Vec4Mov,
	Vec4Add,
	Vec4Sub,
	Vec4Mul,
	Vec4Div,
	Vec4Scale,
	Vec4Dot,
	Vec4Neg,
	Vec4Abs,

	// vx2i
	Vec2Unpack16To31,  // Note that the result is shifted down by 1, hence 31
	Vec2Unpack16To32,
	Vec4Unpack8To32,
	Vec4DuplicateUpperBitsAndShift1,  // Bizarro vuc2i behaviour, in an instruction. Split?
	Vec4ClampToZero,
	Vec2ClampToZero,
	Vec4Pack31To8,
	Vec4Pack32To8,
	Vec2Pack31To16,
	Vec2Pack32To16,

	// Slow special functions. Used on singles.
	FSin,
	FCos,
	FRSqrt,
	FRecip,
	FAsin,

	// Fake/System instructions
	Interpret,

	// Emit this before you exit. Semantic is to set the downcount
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
	ExitToPC,  // Used after a syscall to give us a way to do things before returning.

	Syscall,
	SetPC,  // hack to make syscall returns work
	SetPCConst,  // hack to make replacement know PC
	CallReplacement,
	Break,
	Breakpoint,
	MemoryCheck,
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

// Some common vec4 constants.
enum class Vec4Init {
	AllZERO,
	AllONE,
	AllMinusONE,
	Set_1000,
	Set_0100,
	Set_0010,
	Set_0001,
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

enum IRFpCompareMode {
	False = 0,
	NotEqualUnordered,
	EqualOrdered, // eq,  seq (equal, ordered)
	EqualUnordered, // ueq, ngl (equal, unordered)
	LessOrdered, // olt, lt (less than, ordered)
	LessUnordered, // ult, nge (less than, unordered)
	LessEqualOrdered, // ole, le (less equal, ordered)
	LessEqualUnordered, // ule, ngt (less equal, unordered)
};

enum {
	IRTEMP_0 = 192,
	IRTEMP_1,
	IRTEMP_LHS,  // Reserved for use in branches
	IRTEMP_RHS,  // Reserved for use in branches

	IRVTEMP_PFX_S = 224 - 32,  // Relative to the FP regs
	IRVTEMP_PFX_T = 228 - 32,
	IRVTEMP_PFX_D = 232 - 32,
	IRVTEMP_0 = 236 - 32,

	// 16 float temps for vector S and T prefixes and things like that.
	// IRVTEMP_0 = 208 - 64,  // -64 to be relative to v[0]

	// Hacky way to get to other state
	IRREG_VFPU_CTRL_BASE = 208,
	IRREG_VFPU_CC = 211,
	IRREG_LO = 242,  // offset of lo in MIPSState / 4
	IRREG_HI = 243,
	IRREG_FCR31 = 244,
	IRREG_FPCOND = 245,
};

enum IRFlags {
	// Uses src3, not dest.
	IRFLAG_SRC3 = 0x0001,
	// Uses src3 AND dest (i.e. mutates dest.)
	IRFLAG_SRC3DST = 0x0002,
	// Exit instruction (maybe conditional.)
	IRFLAG_EXIT = 0x0004,
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
	IRWriter &operator =(const IRWriter &w) {
		insts_ = w.insts_;
		constPool_ = w.constPool_;
		return *this;
	}
	IRWriter &operator =(IRWriter &&w) {
		insts_ = std::move(w.insts_);
		constPool_ = std::move(w.constPool_);
		return *this;
	}

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

	const std::vector<IRInst> &GetInstructions() const { return insts_; }
	const std::vector<u32> &GetConstants() const { return constPool_; }

private:
	std::vector<IRInst> insts_;
	std::vector<u32> constPool_;
};

const IRMeta *GetIRMeta(IROp op);
void DisassembleIR(char *buf, size_t bufsize, IRInst inst, const u32 *constPool);
void InitIR();
