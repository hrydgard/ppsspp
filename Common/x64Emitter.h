// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "ppsspp_config.h"

#include <cstddef>
#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/CodeBlock.h"

#if PPSSPP_ARCH(64BIT)
#define PTRBITS 64
#else
#define PTRBITS 32
#endif

namespace Gen
{

enum X64Reg : u32
{
	EAX = 0, EBX = 3, ECX = 1, EDX = 2,
	ESI = 6, EDI = 7, EBP = 5, ESP = 4,

	RAX = 0, RBX = 3, RCX = 1, RDX = 2,
	RSI = 6, RDI = 7, RBP = 5, RSP = 4,
	R8  = 8, R9  = 9, R10 = 10,R11 = 11,
	R12 = 12,R13 = 13,R14 = 14,R15 = 15,

	AL = 0, BL = 3, CL = 1, DL = 2,
	SIL = 6, DIL = 7, BPL = 5, SPL = 4,
	AH = 0x104, BH = 0x107, CH = 0x105, DH = 0x106,

	AX = 0, BX = 3, CX = 1, DX = 2,
	SI = 6, DI = 7, BP = 5, SP = 4,

	XMM0=0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
	XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15,

	YMM0=0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7,
	YMM8, YMM9, YMM10, YMM11, YMM12, YMM13, YMM14, YMM15,

	INVALID_REG = 0xFFFFFFFF
};

enum CCFlags
{
	CC_O   = 0,
	CC_NO  = 1,
	CC_B   = 2, CC_C   = 2, CC_NAE = 2,
	CC_NB  = 3, CC_NC  = 3, CC_AE  = 3,
	CC_Z   = 4, CC_E   = 4,
	CC_NZ  = 5, CC_NE  = 5,
	CC_BE  = 6, CC_NA  = 6,
	CC_NBE = 7, CC_A   = 7,
	CC_S   = 8,
	CC_NS  = 9,
	CC_P   = 0xA, CC_PE  = 0xA,
	CC_NP  = 0xB, CC_PO  = 0xB,
	CC_L   = 0xC, CC_NGE = 0xC,
	CC_NL  = 0xD, CC_GE  = 0xD,
	CC_LE  = 0xE, CC_NG  = 0xE,
	CC_NLE = 0xF, CC_G   = 0xF
};

enum
{
	NUMGPRs = 16,
	NUMXMMs = 16,
};

enum
{
	SCALE_NONE = 0,
	SCALE_1 = 1,
	SCALE_2 = 2,
	SCALE_4 = 4,
	SCALE_8 = 8,
	SCALE_ATREG = 16,
	//SCALE_NOBASE_1 is not supported and can be replaced with SCALE_ATREG
	SCALE_NOBASE_2 = 34,
	SCALE_NOBASE_4 = 36,
	SCALE_NOBASE_8 = 40,
	SCALE_RIP = 0xFF,
	SCALE_IMM8  = 0xF0,
	SCALE_IMM16 = 0xF1,
	SCALE_IMM32 = 0xF2,
	SCALE_IMM64 = 0xF3,
};

enum NormalOp {
	nrmADD,
	nrmADC,
	nrmSUB,
	nrmSBB,
	nrmAND,
	nrmOR ,
	nrmXOR,
	nrmMOV,
	nrmTEST,
	nrmCMP,
	nrmXCHG,
};

enum {
	CMP_EQ = 0,
	CMP_LT = 1,
	CMP_LE = 2,
	CMP_UNORD = 3,
	CMP_NEQ = 4,
	CMP_NLT = 5,
	CMP_NLE = 6,
	CMP_ORD = 7,
};

enum FloatOp {
	floatLD = 0,
	floatST = 2,
	floatSTP = 3,
	floatLD80 = 5,
	floatSTP80 = 7,

	floatINVALID = -1,
};

enum FloatRound {
	FROUND_NEAREST = 0,
	FROUND_FLOOR = 1,
	FROUND_CEIL = 2,
	FROUND_ZERO = 3,
	FROUND_MXCSR = 4,

	FROUND_RAISE_PRECISION = 0,
	FROUND_IGNORE_PRECISION = 8,
};

class XEmitter;

// RIP addressing does not benefit from micro op fusion on Core arch
struct OpArg
{
	OpArg() {}  // dummy op arg, used for storage
	OpArg(u64 _offset, int _scale, X64Reg rmReg = RAX, X64Reg scaledReg = RAX)
	{
		operandReg = 0;
		scale = (u8)_scale;
		offsetOrBaseReg = (u16)rmReg;
		indexReg = (u16)scaledReg;
		//if scale == 0 never mind offsetting
		offset = _offset;
	}
	bool operator==(const OpArg &b) const
	{
		return operandReg == b.operandReg && scale == b.scale && offsetOrBaseReg == b.offsetOrBaseReg &&
		       indexReg == b.indexReg && offset == b.offset;
	}
	void WriteRex(XEmitter *emit, int opBits, int bits, int customOp = -1) const;
	void WriteVex(XEmitter* emit, X64Reg regOp1, X64Reg regOp2, int L, int pp, int mmmmm, int W = 0) const;
	void WriteRest(XEmitter *emit, int extraBytes=0, X64Reg operandReg=INVALID_REG, bool warn_64bit_offset = true) const;
	void WriteSingleByteOp(XEmitter *emit, u8 op, X64Reg operandReg, int bits);
	// This one is public - must be written to
	u64 offset;  // use RIP-relative as much as possible - 64-bit immediates are not available.
	u16 operandReg;

	void WriteNormalOp(XEmitter *emit, bool toRM, NormalOp op, const OpArg &operand, int bits) const;
	bool IsImm() const {return scale == SCALE_IMM8 || scale == SCALE_IMM16 || scale == SCALE_IMM32 || scale == SCALE_IMM64;}
	bool IsSimpleReg() const {return scale == SCALE_NONE;}
	bool IsSimpleReg(X64Reg reg) const
	{
		if (!IsSimpleReg())
			return false;
		return GetSimpleReg() == reg;
	}
	bool IsIndexedReg(X64Reg reg) const {
		if (scale >= 1 && scale <= 4)
			return indexReg == reg;
		if (scale >= 32 && scale <= 40)
			return indexReg == reg;
		return false;
	}

	bool CanDoOpWith(const OpArg &other) const
	{
		if (IsSimpleReg()) return true;
		if (!IsSimpleReg() && !other.IsSimpleReg() && !other.IsImm()) return false;
		return true;
	}

	int GetImmBits() const
	{
		switch (scale)
		{
		case SCALE_IMM8: return 8;
		case SCALE_IMM16: return 16;
		case SCALE_IMM32: return 32;
		case SCALE_IMM64: return 64;
		default: return -1;
		}
	}

	void SetImmBits(int bits) {
		switch (bits)
		{
			case 8: scale = SCALE_IMM8; break;
			case 16: scale = SCALE_IMM16; break;
			case 32: scale = SCALE_IMM32; break;
			case 64: scale = SCALE_IMM64; break;
		}
	}

	X64Reg GetSimpleReg() const
	{
		if (scale == SCALE_NONE)
			return (X64Reg)offsetOrBaseReg;
		else
			return INVALID_REG;
	}

	u32 GetImmValue() const {
		return (u32)offset;
	}

	// For loops.
	void IncreaseOffset(int sz) {
		offset += sz;
	}

private:
	u8 scale;
	u16 offsetOrBaseReg;
	u16 indexReg;
};

inline OpArg M(const void *ptr) {return OpArg((u64)ptr, (int)SCALE_RIP);}
template <typename T>
inline OpArg M(const T *ptr)    {return OpArg((u64)(const void *)ptr, (int)SCALE_RIP);}
inline OpArg R(X64Reg value)    {return OpArg(0, SCALE_NONE, value);}
inline OpArg MatR(X64Reg value) {return OpArg(0, SCALE_ATREG, value);}

inline OpArg MDisp(X64Reg value, int offset)
{
	return OpArg((u32)offset, SCALE_ATREG, value);
}

inline OpArg MComplex(X64Reg base, X64Reg scaled, int scale, int offset)
{
	return OpArg(offset, scale, base, scaled);
}

inline OpArg MScaled(X64Reg scaled, int scale, int offset)
{
	if (scale == SCALE_1)
		return OpArg(offset, SCALE_ATREG, scaled);
	else
		return OpArg(offset, scale | 0x20, RAX, scaled);
}

inline OpArg MRegSum(X64Reg base, X64Reg offset)
{
	return MComplex(base, offset, 1, 0);
}

template <typename T>
inline bool Accessible(const T *t1, const T *t2) {
	ptrdiff_t diff = (const uint8_t *)t1 - (const uint8_t *)t2;
	return diff > -0x7FFFFFE0 && diff < 0x7FFFFFE0;
}

template <typename T>
inline OpArg MAccessibleDisp(X64Reg r, const T *tbase, const T *t) {
	_assert_(Accessible(tbase, t));
	ptrdiff_t diff = (const uint8_t *)t - (const uint8_t *)tbase;
	return MDisp(r, (int)diff);
}

inline OpArg Imm8 (u8 imm)  {return OpArg(imm, SCALE_IMM8);}
inline OpArg Imm16(u16 imm) {return OpArg(imm, SCALE_IMM16);} //rarely used
inline OpArg Imm32(u32 imm) {return OpArg(imm, SCALE_IMM32);}
inline OpArg Imm64(u64 imm) {return OpArg(imm, SCALE_IMM64);}

template<int N> OpArg ImmPtrTpl(const void *imm);
template<> inline OpArg ImmPtrTpl<8>(const void *imm) {return Imm64((u64)imm);}
template<> inline OpArg ImmPtrTpl<4>(const void *imm) {return Imm32((u32)(uintptr_t)imm);}
inline OpArg ImmPtr(const void* imm) {return ImmPtrTpl<sizeof(void*)>(imm);}

inline OpArg UImmAuto(u32 imm) {
	return OpArg(imm, imm >= 128 ? SCALE_IMM32 : SCALE_IMM8);
}
inline OpArg SImmAuto(s32 imm) {
	return OpArg(imm, (imm >= 128 || imm < -128) ? SCALE_IMM32 : SCALE_IMM8);
}

template<int N> u32 PtrOffsetTpl(const void* ptr, const void* base);
template<> inline u32 PtrOffsetTpl<8>(const void *ptr, const void* base) {
	s64 distance = (s64)ptr-(s64)base;
	if (distance >= 0x80000000LL ||
	    distance < -0x80000000LL)
	{
		_assert_msg_(false, "pointer offset out of range");
		return 0;
	}

	return (u32)distance;
}
template<> inline u32 PtrOffsetTpl<4>(const void *ptr, const void* base) {
	return (u32)(uintptr_t)ptr-(u32)(uintptr_t)base;
}
inline u32 PtrOffset(const void* ptr, const void* base) {
    return PtrOffsetTpl<sizeof(void*)>(ptr, base);
}

//usage: int a[]; ARRAY_OFFSET(a,10)
#define ARRAY_OFFSET(array,index) ((u32)((u64)&(array)[index]-(u64)&(array)[0]))
//usage: struct {int e;} s; STRUCT_OFFSET(s,e)
#define STRUCT_OFFSET(str,elem) ((u32)((u64)&(str).elem-(u64)&(str)))

struct FixupBranch
{
	u8 *ptr;
	int type; //0 = 8bit 1 = 32bit
};

enum SSECompare
{
	EQ = 0,
	LT,
	LE,
	UNORD,
	NEQ,
	NLT,
	NLE,
	ORD,
};

typedef const u8* JumpTarget;

class XEmitter
{
	friend struct OpArg;  // for Write8 etc
private:
	u8 *code;
	bool flags_locked;

	void CheckFlags();

	void Rex(int w, int r, int x, int b);
	void WriteSimple1Byte(int bits, u8 byte, X64Reg reg);
	void WriteSimple2Byte(int bits, u8 byte1, u8 byte2, X64Reg reg);
	void WriteMulDivType(int bits, OpArg src, int ext);
	void WriteBitSearchType(int bits, X64Reg dest, OpArg src, u8 byte2, bool rep = false);
	void WriteShift(int bits, OpArg dest, OpArg &shift, int ext);
	void WriteBitTest(int bits, OpArg &dest, OpArg &index, int ext);
	void WriteMXCSR(OpArg arg, int ext);
	void WriteSSEOp(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0);
	void WriteSSSE3Op(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0);
	void WriteSSE41Op(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0);
	void WriteAVXOp(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0, int W = 0);
	void WriteAVXOp(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0, int W = 0);
	// AVX for 128, AVX2 for 256.
	void WriteAVX12Op(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0, int W = 0);
	void WriteAVX12Op(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0, int W = 0);
	void WriteAVX2Op(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0, int W = 0);
	void WriteAVX2Op(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0, int W = 0);
	void WriteVEXOp(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0);
	void WriteBMI1Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0);
	void WriteBMI2Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes = 0);
	void WriteFloatLoadStore(int bits, FloatOp op, FloatOp op_80b, OpArg arg);
	void WriteNormalOp(XEmitter *emit, int bits, NormalOp op, const OpArg &a1, const OpArg &a2);

protected:
	inline void Write8(u8 value)   {*code++ = value;}
	inline void Write16(u16 value) {std::memcpy(code, &value, sizeof(u16)); code += 2;}
	inline void Write32(u32 value) {std::memcpy(code, &value, sizeof(u32)); code += 4;}
	inline void Write64(u64 value) {std::memcpy(code, &value, sizeof(u64)); code += 8;}

public:
	XEmitter() { code = nullptr; flags_locked = false; }
	XEmitter(u8 *code_ptr) { code = code_ptr; flags_locked = false; }
	virtual ~XEmitter() {}

	void WriteModRM(int mod, int rm, int reg);
	void WriteSIB(int scale, int index, int base);

	void SetCodePointer(u8 *ptr, u8 *writePtr);
	const u8 *GetCodePointer() const;

	void ReserveCodeSpace(int bytes);
	const u8 *AlignCode4();
	const u8 *AlignCode16();
	const u8 *AlignCodePage();

	// Nops until the code pointer is 16-byte aligned. Good for loops.
	const u8 *NopAlignCode16();

	u8 *GetWritableCodePtr();

	void LockFlags() { flags_locked = true; }
	void UnlockFlags() { flags_locked = false; }

	// Looking for one of these? It's BANNED!! Some instructions are slow on modern CPU
	// INC, DEC, LOOP, LOOPNE, LOOPE, ENTER, LEAVE, XCHG, XLAT, REP MOVSB/MOVSD, REP SCASD + other string instr.,
	// INC and DEC are slow on Intel Core, but not on AMD. They create a
	// false flag dependency because they only update a subset of the flags.
	// XCHG is SLOW and should be avoided.
	// Actually, REP MOVSB has gotten fast again on recent CPU generations, though it's not all that useful anyway.

	// Debug breakpoint
	void INT3();

	// Do nothing
	void NOP(size_t count = 1);

	// Save energy in wait-loops on P4 only. Probably not too useful.
	void PAUSE();

	// Flag control
	void STC();
	void CLC();
	void CMC();

	// These two can not be executed in 64-bit mode on early Intel 64-bit CPU:s, only on Core2 and AMD!
	void LAHF(); // 3 cycle vector path
	void SAHF(); // direct path fast

	// Stack control
	void PUSH(X64Reg reg);
	void POP(X64Reg reg);
	void PUSH(int bits, const OpArg &reg);
	void POP(int bits, const OpArg &reg);
	void PUSHF();
	void POPF();

	// Flow control
	void RET();
	void RET_FAST();
	void UD2();
	FixupBranch J(bool force5bytes = false);

	void JMP(const u8 * addr, bool force5Bytes = false);
	void JMP(OpArg arg);
	void JMPptr(const OpArg &arg);
	void JMPself(); //infinite loop!
#ifdef CALL
#undef CALL
#endif
	bool CanCALLDirect(const void *fnptr);
	void CALL(const void *fnptr);
	void CALLptr(OpArg arg);

	FixupBranch J_CC(CCFlags conditionCode, bool force5bytes = false);
	//void J_CC(CCFlags conditionCode, JumpTarget target);
	void J_CC(CCFlags conditionCode, const u8 * addr, bool force5Bytes = false);

	void SetJumpTarget(const FixupBranch &branch);

	void SETcc(CCFlags flag, OpArg dest);
	// Note: CMOV brings small if any benefit on current cpus.
	void CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag);

	// Fences
	void LFENCE();
	void MFENCE();
	void SFENCE();

	// Bit scan
	void BSF(int bits, X64Reg dest, OpArg src); //bottom bit to top bit
	void BSR(int bits, X64Reg dest, OpArg src); //top bit to bottom bit

	// Cache control
	enum PrefetchLevel
	{
		PF_NTA, //Non-temporal (data used once and only once)
		PF_T0,  //All cache levels
		PF_T1,  //Levels 2+ (aliased to T0 on AMD)
		PF_T2,  //Levels 3+ (aliased to T0 on AMD)
	};
	void PREFETCH(PrefetchLevel level, OpArg arg);
	void MOVNTI(int bits, OpArg dest, X64Reg src);
	void MOVNTDQ(OpArg arg, X64Reg regOp);
	void MOVNTPS(OpArg arg, X64Reg regOp);
	void MOVNTPD(OpArg arg, X64Reg regOp);

	// Multiplication / division
	void MUL(int bits, OpArg src); //UNSIGNED
	void IMUL(int bits, OpArg src); //SIGNED
	void IMUL(int bits, X64Reg regOp, OpArg src);
	void IMUL(int bits, X64Reg regOp, OpArg src, OpArg imm);
	void DIV(int bits, OpArg src);
	void IDIV(int bits, OpArg src);

	// Shift
	void ROL(int bits, OpArg dest, OpArg shift);
	void ROR(int bits, OpArg dest, OpArg shift);
	void RCL(int bits, OpArg dest, OpArg shift);
	void RCR(int bits, OpArg dest, OpArg shift);
	void SHL(int bits, OpArg dest, OpArg shift);
	void SHR(int bits, OpArg dest, OpArg shift);
	void SAR(int bits, OpArg dest, OpArg shift);

	// Bit Test
	void BT(int bits, OpArg dest, OpArg index);
	void BTS(int bits, OpArg dest, OpArg index);
	void BTR(int bits, OpArg dest, OpArg index);
	void BTC(int bits, OpArg dest, OpArg index);

	// Double-Precision Shift
	void SHRD(int bits, OpArg dest, OpArg src, OpArg shift);
	void SHLD(int bits, OpArg dest, OpArg src, OpArg shift);

	// Extend EAX into EDX in various ways
	void CWD(int bits = 16);
	inline void CDQ() {CWD(32);}
	inline void CQO() {CWD(64);}
	void CBW(int bits = 8);
	inline void CWDE() {CBW(16);}
	inline void CDQE() {CBW(32);}

	// Load effective address
	void LEA(int bits, X64Reg dest, OpArg src);

	// Integer arithmetic
	void NEG (int bits, OpArg src);
	void ADD (int bits, const OpArg &a1, const OpArg &a2);
	void ADC (int bits, const OpArg &a1, const OpArg &a2);
	void SUB (int bits, const OpArg &a1, const OpArg &a2);
	void SBB (int bits, const OpArg &a1, const OpArg &a2);
	void AND (int bits, const OpArg &a1, const OpArg &a2);
	void CMP (int bits, const OpArg &a1, const OpArg &a2);

	// Bit operations
	void NOT (int bits, OpArg src);
	void OR  (int bits, const OpArg &a1, const OpArg &a2);
	void XOR (int bits, const OpArg &a1, const OpArg &a2);
	void MOV (int bits, const OpArg &a1, const OpArg &a2);
	void TEST(int bits, const OpArg &a1, const OpArg &a2);

	// Are these useful at all? Consider removing.
	void XCHG(int bits, const OpArg &a1, const OpArg &a2);
	void XCHG_AHAL();

	// Byte swapping (32 and 64-bit only).
	void BSWAP(int bits, X64Reg reg);

	// Sign/zero extension
	void MOVSX(int dbits, int sbits, X64Reg dest, OpArg src); //automatically uses MOVSXD if necessary
	void MOVZX(int dbits, int sbits, X64Reg dest, OpArg src);

	// Available only on Atom or >= Haswell so far. Test with cpu_info.bMOVBE.
	void MOVBE(int dbits, const OpArg& dest, const OpArg& src);

	// Available only on AMD >= Phenom or Intel >= Haswell
	void LZCNT(int bits, X64Reg dest, OpArg src);
	// Note: this one is actually part of BMI1
	void TZCNT(int bits, X64Reg dest, OpArg src);

	// WARNING - These two take 11-13 cycles and are VectorPath! (AMD64)
	void STMXCSR(OpArg memloc);
	void LDMXCSR(OpArg memloc);

	// Prefixes
	void LOCK();
	void REP();
	void REPNE();
	void FSOverride();
	void GSOverride();

	// x87
	enum x87StatusWordBits {
		x87_InvalidOperation = 0x1,
		x87_DenormalizedOperand = 0x2,
		x87_DivisionByZero = 0x4,
		x87_Overflow = 0x8,
		x87_Underflow = 0x10,
		x87_Precision = 0x20,
		x87_StackFault = 0x40,
		x87_ErrorSummary = 0x80,
		x87_C0 = 0x100,
		x87_C1 = 0x200,
		x87_C2 = 0x400,
		x87_TopOfStack = 0x2000 | 0x1000 | 0x800,
		x87_C3 = 0x4000,
		x87_FPUBusy = 0x8000,
	};

	void FLD(int bits, OpArg src);
	void FST(int bits, OpArg dest);
	void FSTP(int bits, OpArg dest);
	void FNSTSW_AX();
	void FWAIT();

	// SSE/SSE2: Floating point arithmetic
	void ADDSS(X64Reg regOp, OpArg arg);
	void ADDSD(X64Reg regOp, OpArg arg);
	void SUBSS(X64Reg regOp, OpArg arg);
	void SUBSD(X64Reg regOp, OpArg arg);
	void MULSS(X64Reg regOp, OpArg arg);
	void MULSD(X64Reg regOp, OpArg arg);
	void DIVSS(X64Reg regOp, OpArg arg);
	void DIVSD(X64Reg regOp, OpArg arg);
	void MINSS(X64Reg regOp, OpArg arg);
	void MINSD(X64Reg regOp, OpArg arg);
	void MAXSS(X64Reg regOp, OpArg arg);
	void MAXSD(X64Reg regOp, OpArg arg);
	void SQRTSS(X64Reg regOp, OpArg arg);
	void SQRTSD(X64Reg regOp, OpArg arg);
	void RCPSS(X64Reg regOp, OpArg& arg);
	void RSQRTSS(X64Reg regOp, OpArg arg);

	// SSE/SSE2: Floating point bitwise (yes)
	void CMPSS(X64Reg regOp, OpArg arg, u8 compare);
	void CMPSD(X64Reg regOp, OpArg arg, u8 compare);

	inline void CMPEQSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_EQ); }
	inline void CMPLTSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_LT); }
	inline void CMPLESS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_LE); }
	inline void CMPUNORDSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_UNORD); }
	inline void CMPNEQSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_NEQ); }
	inline void CMPNLTSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_NLT); }
	inline void CMPORDSS(X64Reg regOp, OpArg arg) { CMPSS(regOp, arg, CMP_ORD); }

	// SSE/SSE2: Floating point packed arithmetic (x4 for float, x2 for double)
	void ADDPS(X64Reg regOp, OpArg arg);
	void ADDPD(X64Reg regOp, OpArg arg);
	void SUBPS(X64Reg regOp, OpArg arg);
	void SUBPD(X64Reg regOp, OpArg arg);
	void CMPPS(X64Reg regOp, OpArg arg, u8 compare);
	void CMPPD(X64Reg regOp, OpArg arg, u8 compare);
	void MULPS(X64Reg regOp, OpArg arg);
	void MULPD(X64Reg regOp, OpArg arg);
	void DIVPS(X64Reg regOp, OpArg arg);
	void DIVPD(X64Reg regOp, OpArg arg);
	void MINPS(X64Reg regOp, OpArg arg);
	void MINPD(X64Reg regOp, OpArg arg);
	void MAXPS(X64Reg regOp, OpArg arg);
	void MAXPD(X64Reg regOp, OpArg arg);
	void SQRTPS(X64Reg regOp, OpArg arg);
	void SQRTPD(X64Reg regOp, OpArg arg);
	void RCPPS(X64Reg regOp, OpArg& arg);
	void RSQRTPS(X64Reg regOp, OpArg arg);

	// SSE/SSE2: Floating point packed bitwise (x4 for float, x2 for double)
	void ANDPS(X64Reg regOp, OpArg arg);
	void ANDPD(X64Reg regOp, OpArg arg);
	void ANDNPS(X64Reg regOp, OpArg arg);
	void ANDNPD(X64Reg regOp, OpArg arg);
	void ORPS(X64Reg regOp, OpArg arg);
	void ORPD(X64Reg regOp, OpArg arg);
	void XORPS(X64Reg regOp, OpArg arg);
	void XORPD(X64Reg regOp, OpArg arg);

	// SSE/SSE2: Shuffle components. These are tricky - see Intel documentation.
	void SHUFPS(X64Reg regOp, OpArg arg, u8 shuffle);
	void SHUFPD(X64Reg regOp, OpArg arg, u8 shuffle);

	// SSE/SSE2: Useful alternative to shuffle in some cases.
	void MOVDDUP(X64Reg regOp, OpArg arg);

	// TODO: Actually implement
#if 0
	// SSE3: Horizontal operations in SIMD registers. Could be useful for various VFPU things like dot products...
	void ADDSUBPS(X64Reg dest, OpArg src);
	void ADDSUBPD(X64Reg dest, OpArg src);
	void HADDPD(X64Reg dest, OpArg src);
	void HSUBPS(X64Reg dest, OpArg src);
	void HSUBPD(X64Reg dest, OpArg src);

	// SSE4: Further horizontal operations - dot products. These are weirdly flexible, the arg contains both a read mask and a write "mask".
	void DPPD(X64Reg dest, OpArg src, u8 arg);
#endif

	// SSE4: Insert and extract for floats.
	// Note: insert from memory or an XMM.
	void INSERTPS(X64Reg dest, OpArg arg, u8 dstsubreg, u8 srcsubreg = 0, u8 zmask = 0);
	// Extract to memory or GPR.
	void EXTRACTPS(OpArg dest, X64Reg arg, u8 subreg);

	// SSE3: Horizontal operations in SIMD registers. Very slow! shufps-based code beats it handily on Ivy.
	void HADDPS(X64Reg dest, OpArg src);

	// SSE4: Further horizontal operations - dot products. These are weirdly flexible, the arg contains both a read mask and a write "mask".
	void DPPS(X64Reg dest, OpArg src, u8 arg);

	void UNPCKLPS(X64Reg dest, OpArg src);
	void UNPCKHPS(X64Reg dest, OpArg src);
	void UNPCKLPD(X64Reg dest, OpArg src);
	void UNPCKHPD(X64Reg dest, OpArg src);

	// SSE/SSE2: Compares.
	void COMISS(X64Reg regOp, OpArg arg);
	void COMISD(X64Reg regOp, OpArg arg);
	void UCOMISS(X64Reg regOp, OpArg arg);
	void UCOMISD(X64Reg regOp, OpArg arg);

	// SSE/SSE2: Moves. Use the right data type for your data, in most cases.
	void MOVAPS(X64Reg regOp, OpArg arg);
	void MOVAPD(X64Reg regOp, OpArg arg);
	void MOVAPS(OpArg arg, X64Reg regOp);
	void MOVAPD(OpArg arg, X64Reg regOp);

	void MOVUPS(X64Reg regOp, OpArg arg);
	void MOVUPD(X64Reg regOp, OpArg arg);
	void MOVUPS(OpArg arg, X64Reg regOp);
	void MOVUPD(OpArg arg, X64Reg regOp);

	void MOVDQA(X64Reg regOp, OpArg arg);
	void MOVDQA(OpArg arg, X64Reg regOp);
	void MOVDQU(X64Reg regOp, OpArg arg);
	void MOVDQU(OpArg arg, X64Reg regOp);

	void MOVSS(X64Reg regOp, OpArg arg);
	void MOVSD(X64Reg regOp, OpArg arg);
	void MOVSS(OpArg arg, X64Reg regOp);
	void MOVSD(OpArg arg, X64Reg regOp);

	void MOVLPS(X64Reg regOp, OpArg arg);
	void MOVLPD(X64Reg regOp, OpArg arg);
	void MOVLPS(OpArg arg, X64Reg regOp);
	void MOVLPD(OpArg arg, X64Reg regOp);

	void MOVHPS(X64Reg regOp, OpArg arg);
	void MOVHPD(X64Reg regOp, OpArg arg);
	void MOVHPS(OpArg arg, X64Reg regOp);
	void MOVHPD(OpArg arg, X64Reg regOp);

	void MOVHLPS(X64Reg regOp1, X64Reg regOp2);
	void MOVLHPS(X64Reg regOp1, X64Reg regOp2);

	void MOVD_xmm(X64Reg dest, const OpArg &arg);
	void MOVQ_xmm(X64Reg dest, OpArg arg);
	void MOVD_xmm(const OpArg &arg, X64Reg src);
	void MOVQ_xmm(OpArg arg, X64Reg src);

	// SSE3: Some additional moves.
	void MOVSHDUP(X64Reg regOp1, OpArg arg);
	void MOVSLDUP(X64Reg regOp1, OpArg arg);

	// SSE/SSE2: Generates a mask from the high bits of the components of the packed register in question.
	void MOVMSKPS(X64Reg dest, OpArg arg);
	void MOVMSKPD(X64Reg dest, OpArg arg);

	// SSE2: Selective byte store, mask in src register. EDI/RDI specifies store address. This is a weird one.
	void MASKMOVDQU(X64Reg dest, X64Reg src);
	void LDDQU(X64Reg dest, OpArg src);

	// SSE/SSE2: Data type conversions.
	void CVTPS2PD(X64Reg dest, OpArg src);
	void CVTPD2PS(X64Reg dest, OpArg src);
	void CVTSS2SD(X64Reg dest, OpArg src);
	void CVTSI2SS(X64Reg dest, OpArg src);
	void CVTSD2SS(X64Reg dest, OpArg src);
	void CVTSI2SD(X64Reg dest, OpArg src);
	void CVTDQ2PD(X64Reg regOp, OpArg arg);
	void CVTPD2DQ(X64Reg regOp, OpArg arg);
	void CVTDQ2PS(X64Reg regOp, OpArg arg);
	void CVTPS2DQ(X64Reg regOp, OpArg arg);

	void CVTTPS2DQ(X64Reg regOp, OpArg arg);
	void CVTTPD2DQ(X64Reg regOp, OpArg arg);

	// Destinations are X64 regs (rax, rbx, ...) for these instructions.
	void CVTSS2SI(X64Reg xregdest, OpArg src);
	void CVTSD2SI(X64Reg xregdest, OpArg src);
	void CVTTSS2SI(X64Reg xregdest, OpArg arg);
	void CVTTSD2SI(X64Reg xregdest, OpArg arg);

	// SSE2: Packed integer instructions
	void PACKSSDW(X64Reg dest, OpArg arg);
	void PACKSSWB(X64Reg dest, OpArg arg);
	void PACKUSDW(X64Reg dest, OpArg arg);
	void PACKUSWB(X64Reg dest, OpArg arg);

	void PUNPCKLBW(X64Reg dest, const OpArg &arg);
	void PUNPCKLWD(X64Reg dest, const OpArg &arg);
	void PUNPCKLDQ(X64Reg dest, const OpArg &arg);
	void PUNPCKLQDQ(X64Reg dest, const OpArg &arg);

	void PUNPCKHBW(X64Reg dest, const OpArg &arg);
	void PUNPCKHWD(X64Reg dest, const OpArg &arg);
	void PUNPCKHDQ(X64Reg dest, const OpArg &arg);
	void PUNPCKHQDQ(X64Reg dest, const OpArg &arg);

	void PTEST(X64Reg dest, OpArg arg);
	void PAND(X64Reg dest, OpArg arg);
	void PANDN(X64Reg dest, OpArg arg);
	void PXOR(X64Reg dest, OpArg arg);
	void POR(X64Reg dest, OpArg arg);

	void PADDB(X64Reg dest, OpArg arg);
	void PADDW(X64Reg dest, OpArg arg);
	void PADDD(X64Reg dest, OpArg arg);
	void PADDQ(X64Reg dest, OpArg arg);

	void PADDSB(X64Reg dest, OpArg arg);
	void PADDSW(X64Reg dest, OpArg arg);
	void PADDUSB(X64Reg dest, OpArg arg);
	void PADDUSW(X64Reg dest, OpArg arg);

	void PSUBB(X64Reg dest, OpArg arg);
	void PSUBW(X64Reg dest, OpArg arg);
	void PSUBD(X64Reg dest, OpArg arg);
	void PSUBQ(X64Reg dest, OpArg arg);

	void PSUBSB(X64Reg dest, OpArg arg);
	void PSUBSW(X64Reg dest, OpArg arg);
	void PSUBUSB(X64Reg dest, OpArg arg);
	void PSUBUSW(X64Reg dest, OpArg arg);

	void PAVGB(X64Reg dest, OpArg arg);
	void PAVGW(X64Reg dest, OpArg arg);

	void PCMPEQB(X64Reg dest, OpArg arg);
	void PCMPEQW(X64Reg dest, OpArg arg);
	void PCMPEQD(X64Reg dest, OpArg arg);

	void PCMPGTB(X64Reg dest, OpArg arg);
	void PCMPGTW(X64Reg dest, OpArg arg);
	void PCMPGTD(X64Reg dest, OpArg arg);

	void PEXTRW(X64Reg dest, X64Reg arg, u8 subreg);
	void PINSRW(X64Reg dest, OpArg arg, u8 subreg);
	// SSE4 inserts and extracts.
	void PEXTRB(OpArg dest, X64Reg arg, u8 subreg);
	void PEXTRW(OpArg dest, X64Reg arg, u8 subreg);
	void PEXTRD(OpArg dest, X64Reg arg, u8 subreg);
	void PEXTRQ(OpArg dest, X64Reg arg, u8 subreg);
	void PINSRB(X64Reg dest, OpArg arg, u8 subreg);
	void PINSRD(X64Reg dest, OpArg arg, u8 subreg);
	void PINSRQ(X64Reg dest, OpArg arg, u8 subreg);

	void PMADDWD(X64Reg dest, OpArg arg);
	void PMADDUBSW(X64Reg dest, OpArg arg);
	void PSADBW(X64Reg dest, OpArg arg);

	void PMAXSW(X64Reg dest, OpArg arg);
	void PMAXUB(X64Reg dest, OpArg arg);
	void PMINSW(X64Reg dest, OpArg arg);
	void PMINUB(X64Reg dest, OpArg arg);
	// SSE4: More MAX/MIN instructions.
	void PMINSB(X64Reg dest, OpArg arg);
	void PMINSD(X64Reg dest, OpArg arg);
	void PMINUW(X64Reg dest, OpArg arg);
	void PMINUD(X64Reg dest, OpArg arg);
	void PMAXSB(X64Reg dest, OpArg arg);
	void PMAXSD(X64Reg dest, OpArg arg);
	void PMAXUW(X64Reg dest, OpArg arg);
	void PMAXUD(X64Reg dest, OpArg arg);

	void PMOVMSKB(X64Reg dest, OpArg arg);
	void PSHUFD(X64Reg dest, OpArg arg, u8 shuffle);
	void PSHUFB(X64Reg dest, OpArg arg);

	void PSHUFLW(X64Reg dest, OpArg arg, u8 shuffle);
	void PSHUFHW(X64Reg dest, OpArg arg, u8 shuffle);

	void PSRLW(X64Reg dest, X64Reg reg, int shift);
	void PSRLW(X64Reg reg, int shift) { PSRLW(reg, reg, shift); }
	void PSRLD(X64Reg dest, X64Reg reg, int shift);
	void PSRLD(X64Reg reg, int shift) { PSRLD(reg, reg, shift); }
	void PSRLQ(X64Reg dest, X64Reg reg, int shift);
	void PSRLQ(X64Reg reg, int shift) { PSRLQ(reg, reg, shift); }
	void PSRLDQ(X64Reg dest, X64Reg reg, int shift);
	void PSRLDQ(X64Reg reg, int shift) { PSRLDQ(reg, reg, shift); }
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSRLW(X64Reg reg, OpArg arg);
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSRLD(X64Reg reg, OpArg arg);
	// Note: both values shifted by lowest 64-bit in XMM arg.
	void PSRLQ(X64Reg reg, OpArg arg);

	void PSLLW(X64Reg dest, X64Reg reg, int shift);
	void PSLLW(X64Reg reg, int shift) { PSLLW(reg, reg, shift); }
	void PSLLD(X64Reg dest, X64Reg reg, int shift);
	void PSLLD(X64Reg reg, int shift) { PSLLD(reg, reg, shift); }
	void PSLLQ(X64Reg dest, X64Reg reg, int shift);
	void PSLLQ(X64Reg reg, int shift) { PSLLQ(reg, reg, shift); }
	void PSLLDQ(X64Reg dest, X64Reg reg, int shift);
	void PSLLDQ(X64Reg reg, int shift) { PSLLDQ(reg, reg, shift); }
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSLLW(X64Reg reg, OpArg arg);
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSLLD(X64Reg reg, OpArg arg);
	// Note: both values shifted by lowest 64-bit in XMM arg.
	void PSLLQ(X64Reg reg, OpArg arg);

	void PSRAW(X64Reg dest, X64Reg reg, int shift);
	void PSRAW(X64Reg reg, int shift) { PSRAW(reg, reg, shift); }
	void PSRAD(X64Reg dest, X64Reg reg, int shift);
	void PSRAD(X64Reg reg, int shift) { PSRAD(reg, reg, shift); }
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSRAW(X64Reg reg, OpArg arg);
	// Note: all values shifted by lowest 64-bit in XMM arg.
	void PSRAD(X64Reg reg, OpArg arg);

	void PMULLW(X64Reg dest, const OpArg &arg);
	void PMULHW(X64Reg dest, const OpArg &arg);
	void PMULHUW(X64Reg dest, const OpArg &arg);
	void PMULUDQ(X64Reg dest, const OpArg &arg);

	// SSE4: integer multiply
	void PMULLD(X64Reg dest, const OpArg &arg);
	void PMULDQ(X64Reg dest, const OpArg &arg);

	// SSE4: data type conversions
	void PMOVSXBW(X64Reg dest, OpArg arg);
	void PMOVSXBD(X64Reg dest, OpArg arg);
	void PMOVSXBQ(X64Reg dest, OpArg arg);
	void PMOVSXWD(X64Reg dest, OpArg arg);
	void PMOVSXWQ(X64Reg dest, OpArg arg);
	void PMOVSXDQ(X64Reg dest, OpArg arg);
	void PMOVZXBW(X64Reg dest, OpArg arg);
	void PMOVZXBD(X64Reg dest, OpArg arg);
	void PMOVZXBQ(X64Reg dest, OpArg arg);
	void PMOVZXWD(X64Reg dest, OpArg arg);
	void PMOVZXWQ(X64Reg dest, OpArg arg);
	void PMOVZXDQ(X64Reg dest, OpArg arg);

	// SSE4: variable blend instructions (xmm0 implicit argument)
	void PBLENDVB(X64Reg dest, OpArg arg);
	void BLENDVPS(X64Reg dest, OpArg arg);
	void BLENDVPD(X64Reg dest, OpArg arg);

	// SSE4: constant blend instructions
	void PBLENDW(X64Reg dest, OpArg arg, u8 mask);
	void BLENDPS(X64Reg dest, OpArg arg, u8 mask);
	void BLENDPD(X64Reg dest, OpArg arg, u8 mask);

	// SSE4: rounding (see FloatRound for mode or use ROUNDNEARSS, etc. helpers.)
	void ROUNDSS(X64Reg dest, OpArg arg, u8 mode);
	void ROUNDSD(X64Reg dest, OpArg arg, u8 mode);
	void ROUNDPS(X64Reg dest, OpArg arg, u8 mode);
	void ROUNDPD(X64Reg dest, OpArg arg, u8 mode);

	inline void ROUNDNEARSS(X64Reg dest, OpArg arg) { ROUNDSS(dest, arg, FROUND_NEAREST); }
	inline void ROUNDFLOORSS(X64Reg dest, OpArg arg) { ROUNDSS(dest, arg, FROUND_FLOOR); }
	inline void ROUNDCEILSS(X64Reg dest, OpArg arg) { ROUNDSS(dest, arg, FROUND_CEIL); }
	inline void ROUNDZEROSS(X64Reg dest, OpArg arg) { ROUNDSS(dest, arg, FROUND_ZERO); }

	inline void ROUNDNEARSD(X64Reg dest, OpArg arg) { ROUNDSD(dest, arg, FROUND_NEAREST); }
	inline void ROUNDFLOORSD(X64Reg dest, OpArg arg) { ROUNDSD(dest, arg, FROUND_FLOOR); }
	inline void ROUNDCEILSD(X64Reg dest, OpArg arg) { ROUNDSD(dest, arg, FROUND_CEIL); }
	inline void ROUNDZEROSD(X64Reg dest, OpArg arg) { ROUNDSD(dest, arg, FROUND_ZERO); }

	inline void ROUNDNEARPS(X64Reg dest, OpArg arg) { ROUNDPS(dest, arg, FROUND_NEAREST); }
	inline void ROUNDFLOORPS(X64Reg dest, OpArg arg) { ROUNDPS(dest, arg, FROUND_FLOOR); }
	inline void ROUNDCEILPS(X64Reg dest, OpArg arg) { ROUNDPS(dest, arg, FROUND_CEIL); }
	inline void ROUNDZEROPS(X64Reg dest, OpArg arg) { ROUNDPS(dest, arg, FROUND_ZERO); }

	inline void ROUNDNEARPD(X64Reg dest, OpArg arg) { ROUNDPD(dest, arg, FROUND_NEAREST); }
	inline void ROUNDFLOORPD(X64Reg dest, OpArg arg) { ROUNDPD(dest, arg, FROUND_FLOOR); }
	inline void ROUNDCEILPD(X64Reg dest, OpArg arg) { ROUNDPD(dest, arg, FROUND_CEIL); }
	inline void ROUNDZEROPD(X64Reg dest, OpArg arg) { ROUNDPD(dest, arg, FROUND_ZERO); }

	// AVX
	void VADDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VADDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VADDSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VADDSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VADDSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VADDSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VCMPPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare);
	void VCMPPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare);
	void VCMPSS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare);
	void VCMPSD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare);
	void VCOMISS(X64Reg regOp1, OpArg arg);
	void VCOMISD(X64Reg regOp1, OpArg arg);
	void VDIVPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VDIVPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VDIVSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VDIVSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	// 256 is just two DPPS, not a single dot product.
	void VDPPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VDPPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VHADDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VHADDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VHSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VHSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMAXPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMAXPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMAXSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMAXSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMINPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMINPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMINSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMINSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMULPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMULPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMULSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMULSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VRCPPS(int bits, X64Reg regOp1, OpArg arg);
	void VRCPSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VRSQRTPS(int bits, X64Reg regOp1, OpArg arg);
	void VRSQRTSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSQRTPS(int bits, X64Reg regOp1, OpArg arg);
	void VSQRTPD(int bits, X64Reg regOp1, OpArg arg);
	void VSQRTSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSQRTSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSUBSS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VSUBSD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VUCOMISS(X64Reg regOp1, OpArg arg);
	void VUCOMISD(X64Reg regOp1, OpArg arg);

	void VBLENDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VBLENDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VBLENDVPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg mask);
	void VBLENDVPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg mask);
	void VCVTDQ2PS(int bits, X64Reg regOp1, OpArg arg);
	void VCVTDQ2PD(int bits, X64Reg regOp1, OpArg arg);
	void VCVTPS2DQ(int bits, X64Reg regOp1, OpArg arg);
	void VCVTPD2DQ(int bits, X64Reg regOp1, OpArg arg);
	void VCVTPS2PD(int bits, X64Reg regOp1, OpArg arg);
	void VCVTPD2PS(int bits, X64Reg regOp1, OpArg arg);
	void VCVTSS2SI(int bits, X64Reg regOp1, OpArg arg);
	void VCVTSS2SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VCVTSD2SI(int bits, X64Reg regOp1, OpArg arg);
	void VCVTSD2SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VCVTSI2SS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VCVTSI2SD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VCVTTPS2DQ(int bits, X64Reg regOp1, OpArg arg);
	void VCVTTPD2DQ(int bits, X64Reg regOp1, OpArg arg);
	void VCVTTSS2SI(int bits, X64Reg regOp1, OpArg arg);
	void VCVTTSD2SI(int bits, X64Reg regOp1, OpArg arg);
	// Can only extract from the low 128 bits.
	void VEXTRACTPS(OpArg arg, X64Reg regOp1, u8 subreg);
	// Can only insert into the low 128 bits, zeros upper bits.  Inserts from XMM.
	void VINSERTPS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 dstsubreg, u8 srcsubreg = 0, u8 zmask = 0);
	void VLDDQU(int bits, X64Reg regOp1, OpArg arg);
	void VMOVAPS(int bits, X64Reg regOp1, OpArg arg);
	void VMOVAPD(int bits, X64Reg regOp1, OpArg arg);
	void VMOVAPS(int bits, OpArg arg, X64Reg regOp1);
	void VMOVAPD(int bits, OpArg arg, X64Reg regOp1);
	void VMOVD(int bits, X64Reg regOp1, OpArg arg);
	void VMOVD(int bits, OpArg arg, X64Reg regOp1);
	void VMOVD(X64Reg regOp1, OpArg arg);
	void VMOVD(OpArg arg, X64Reg regOp1);
	void VMOVQ(X64Reg regOp1, OpArg arg);
	void VMOVQ(OpArg arg, X64Reg regOp1);
	void VMOVDDUP(int bits, X64Reg regOp1, OpArg arg);
	void VMOVDQA(int bits, X64Reg regOp1, OpArg arg);
	void VMOVDQA(int bits, OpArg arg, X64Reg regOp1);
	void VMOVDQU(int bits, X64Reg regOp1, OpArg arg);
	void VMOVDQU(int bits, OpArg arg, X64Reg regOp1);
	void VMOVHLPS(X64Reg regOp1, X64Reg regOp2, X64Reg arg);
	void VMOVLHPS(X64Reg regOp1, X64Reg regOp2, X64Reg arg);
	void VMOVHPS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VMOVHPS(OpArg arg, X64Reg regOp1);
	void VMOVHPD(X64Reg regOp2, X64Reg regOp1, OpArg arg);
	void VMOVHPD(OpArg arg, X64Reg regOp1);
	void VMOVLPS(X64Reg regOp2, X64Reg regOp1, OpArg arg);
	void VMOVLPS(OpArg arg, X64Reg regOp1);
	void VMOVLPD(X64Reg regOp2, X64Reg regOp1, OpArg arg);
	void VMOVLPD(OpArg arg, X64Reg regOp1);
	void VMOVMSKPS(int bits, X64Reg genReg, X64Reg xmmReg);
	void VMOVMSKPD(int bits, X64Reg genReg, X64Reg xmmReg);
	void VMOVNTDQ(int bits, OpArg arg, X64Reg regOp1);
	void VMOVNTPS(int bits, OpArg arg, X64Reg regOp1);
	void VMOVNTPD(int bits, OpArg arg, X64Reg regOp1);
	// Between XMM regs, zeros upper bits.
	void VMOVQ(X64Reg regOp1, X64Reg arg);
	void VMOVSS(X64Reg regOp1, X64Reg regOp2, X64Reg arg);
	void VMOVSS(OpArg arg, X64Reg regOp1);
	void VMOVSS(X64Reg regOp1, OpArg arg);
	void VMOVSD(X64Reg regOp1, X64Reg regOp2, X64Reg arg);
	void VMOVSD(OpArg arg, X64Reg regOp1);
	void VMOVSD(X64Reg regOp1, OpArg arg);
	void VMOVSHDUP(int bits, X64Reg regOp1, OpArg arg);
	void VMOVSLDUP(int bits, X64Reg regOp1, OpArg arg);
	void VMOVUPS(int bits, X64Reg regOp1, OpArg arg);
	void VMOVUPD(int bits, X64Reg regOp1, OpArg arg);
	void VMOVUPS(int bits, OpArg arg, X64Reg regOp1);
	void VMOVUPD(int bits, OpArg arg, X64Reg regOp1);
	void VROUNDPS(int bits, X64Reg dest, OpArg arg, u8 mode);
	void VROUNDPD(int bits, X64Reg dest, OpArg arg, u8 mode);
	void VROUNDSS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mode);
	void VROUNDSD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mode);
	void VSHUFPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shuffle);
	void VSHUFPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shuffle);
	void VUNPCKHPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VUNPCKHPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VUNPCKLPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VUNPCKLPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void VANDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VANDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VANDNPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VANDNPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VORPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VORPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VXORPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VXORPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void VPTEST(int bits, X64Reg regOp1, OpArg arg);

	void VMOVNTDQA(int bits, X64Reg regOp1, OpArg arg);
	void VPACKSSWB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPACKSSDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPACKUSWB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPACKUSDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	// 256 performs two PALIGNRs (high and low), not a single large one.
	void VPALIGNR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shift);
	void VPBLENDVB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg maskReg);
	void VPBLENDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VPEXTRB(OpArg arg, X64Reg regOp1, u8 subreg);
	void VPEXTRW(OpArg arg, X64Reg regOp1, u8 subreg);
	void VPEXTRD(OpArg arg, X64Reg regOp1, u8 subreg);
	void VPEXTRQ(OpArg arg, X64Reg regOp1, u8 subreg);
	void VPINSRB(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPINSRW(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPINSRD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPINSRQ(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPMOVMSKB(int bits, X64Reg genReg, X64Reg arg);
	void VPMOVSXBW(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVSXBD(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVSXBQ(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVSXWD(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVSXWQ(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVSXDQ(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXBW(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXBD(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXBQ(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXWD(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXWQ(int bits, X64Reg regOp1, OpArg arg);
	void VPMOVZXDQ(int bits, X64Reg regOp1, OpArg arg);
	// 256 performs two PSHUFBs, can't shuffle between.
	void VPSHUFB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	// 256 performs two PSHUFDs with same shuffle mask, can't shuffle between.
	void VPSHUFD(int bits, X64Reg regOp1, OpArg arg, u8 shuffle);
	// 256 performs two PSHUFHWs with same shuffle mask.
	void VPSHUFHW(int bits, X64Reg regOp1, OpArg arg, u8 shuffle);
	// 256 performs two PSHUFLWs with same shuffle mask.
	void VPSHUFLW(int bits, X64Reg regOp1, OpArg arg, u8 shuffle);
	void VPUNPCKHBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKHWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKHDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKHQDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKLBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKLWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKLDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPUNPCKLQDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void VPABSB(int bits, X64Reg regOp1, OpArg arg);
	void VPABSW(int bits, X64Reg regOp1, OpArg arg);
	void VPABSD(int bits, X64Reg regOp1, OpArg arg);
	void VPADDB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDUSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPADDUSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPAVGB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPAVGW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPEQB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPEQW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPEQD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPEQQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPGTB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPGTW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPGTD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPCMPGTQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPHADDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPHADDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPHADDSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VHMINPOSUW(X64Reg regOp1, OpArg arg);
	void VPHSUBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPHSUBD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPHSUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMADDUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMADDWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXSD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXUB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMAXUD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINSD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINUB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMINUD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULHRS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULHUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULHW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPMULUDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSADBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSIGNB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSIGNW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSIGND(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBUSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSUBUSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void VPAND(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPANDN(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPOR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSLLDQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSLLW(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSLLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSLLD(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSLLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSLLQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSLLQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRAW(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRAW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRAD(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRAD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRLDQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRLW(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRLD(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRLQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift);
	void VPSRLQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPXOR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void VBROADCASTSS(int bits, X64Reg regOp1, OpArg arg);
	void VBROADCASTSD(X64Reg regOp1, OpArg arg);
	void VBROADCASTF128(X64Reg regOp1, OpArg arg);
	void VEXTRACTF128(OpArg arg, X64Reg regOp1, u8 subreg);
	void VINSERTF128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPERM2F128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VPERMILPS(int bits, X64Reg dest, X64Reg src, OpArg mask);
	void VPERMILPS(int bits, X64Reg dest, OpArg src, u8 mask);
	void VPERMILPD(int bits, X64Reg dest, X64Reg src, OpArg mask);
	void VPERMILPD(int bits, X64Reg dest, OpArg src, u8 mask);

	void VMASKMOVPS(int bits, X64Reg dest, X64Reg maskReg, OpArg src);
	void VMASKMOVPS(int bits, OpArg dest, X64Reg maskReg, X64Reg src);
	void VMASKMOVPD(int bits, X64Reg dest, X64Reg maskReg, OpArg src);
	void VMASKMOVPD(int bits, OpArg dest, X64Reg maskReg, X64Reg src);

	void VTESTPS(int bits, X64Reg regOp1, OpArg arg);
	void VTESTPD(int bits, X64Reg regOp1, OpArg arg);

	void VZEROALL();
	void VZEROUPPER();

	// AVX2
	void VEXTRACTI128(OpArg arg, X64Reg regOp1, u8 subreg);
	void VINSERTI128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg);
	void VPBLENDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VPBROADCASTB(int bits, X64Reg regOp1, OpArg arg);
	void VPBROADCASTW(int bits, X64Reg regOp1, OpArg arg);
	void VPBROADCASTD(int bits, X64Reg regOp1, OpArg arg);
	void VPBROADCASTQ(int bits, X64Reg regOp1, OpArg arg);
	// Must be memory for this one.
	void VBROADCASTI128(X64Reg regOp1, OpArg arg);
	void VPERM2I128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask);
	void VPERMD(X64Reg dest, X64Reg shuffle, OpArg src);
	void VPERMPS(X64Reg dest, X64Reg shuffle, OpArg src);
	void VPERMPD(X64Reg dest, OpArg src, u8 shuffle);
	void VPERMQ(X64Reg dest, OpArg src, u8 shuffle);

	void VPMASKMOVD(int bits, X64Reg regOp1, X64Reg mask, OpArg arg);
	void VPMASKMOVD(int bits, OpArg arg, X64Reg mask, X64Reg regOp2);
	void VPMASKMOVQ(int bits, X64Reg regOp1, X64Reg mask, OpArg arg);
	void VPMASKMOVQ(int bits, OpArg arg, X64Reg mask, X64Reg regOp2);

	// Use an XMM for the scaled reg in MComplex.
	void VGATHERDPS(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VGATHERDPD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VGATHERQPS(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VGATHERQPD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VPGATHERDD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VPGATHERQD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VPGATHERDQ(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void VPGATHERQQ(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);

	void VPSLLVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSLLVQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRAVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRLVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VPSRLVQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	// FMA3
	void VFMADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADD231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUB231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMADD231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFNMSUB231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMADDSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void VFMSUBADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	// VEX GPR instructions
	void SARX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void SHLX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void SHRX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void RORX(int bits, X64Reg regOp, OpArg arg, u8 rotate);
	void PEXT(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void PDEP(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void MULX(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);
	void BZHI(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void BLSR(int bits, X64Reg regOp, OpArg arg);
	void BLSMSK(int bits, X64Reg regOp, OpArg arg);
	void BLSI(int bits, X64Reg regOp, OpArg arg);
	void BEXTR(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2);
	void ANDN(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg);

	void RDTSC();

	// Utility functions
	// The difference between this and CALL is that this aligns the stack
	// where appropriate.
	void ABI_CallFunction(const void *func);
	template <typename T>
	void ABI_CallFunction(T (*func)()) {
		ABI_CallFunction((const void *)func);
	}

	void ABI_CallFunction(const u8 *func) {
		ABI_CallFunction((const void *)func);
	}
	void ABI_CallFunctionC16(const void *func, u16 param1);
	void ABI_CallFunctionCC16(const void *func, u32 param1, u16 param2);


	// These only support u32 parameters, but that's enough for a lot of uses.
	// These will destroy the 1 or 2 first "parameter regs".
	void ABI_CallFunctionC(const void *func, u32 param1);
	void ABI_CallFunctionCC(const void *func, u32 param1, u32 param2);
	void ABI_CallFunctionCCC(const void *func, u32 param1, u32 param2, u32 param3);
	void ABI_CallFunctionCCP(const void *func, u32 param1, u32 param2, void *param3);
	void ABI_CallFunctionCCCP(const void *func, u32 param1, u32 param2, u32 param3, void *param4);
	void ABI_CallFunctionP(const void *func, void *param1);
	void ABI_CallFunctionPA(const void *func, void *param1, const Gen::OpArg &arg2);
	void ABI_CallFunctionPAA(const void *func, void *param1, const Gen::OpArg &arg2, const Gen::OpArg &arg3);
	void ABI_CallFunctionPPC(const void *func, void *param1, void *param2, u32 param3);
	void ABI_CallFunctionAC(const void *func, const Gen::OpArg &arg1, u32 param2);
	void ABI_CallFunctionACC(const void *func, const Gen::OpArg &arg1, u32 param2, u32 param3);
	void ABI_CallFunctionA(const void *func, const Gen::OpArg &arg1);
	void ABI_CallFunctionAA(const void *func, const Gen::OpArg &arg1, const Gen::OpArg &arg2);

	// Pass a register as a parameter.
	void ABI_CallFunctionR(const void *func, X64Reg reg1);
	void ABI_CallFunctionRR(const void *func, X64Reg reg1, X64Reg reg2);

	template <typename Tr, typename T1>
	void ABI_CallFunctionC(Tr (*func)(T1), u32 param1) {
		ABI_CallFunctionC((const void *)func, param1);
	}

	// A function that doesn't have any control over what it will do to regs,
	// such as the dispatcher, should be surrounded by these.
	void ABI_PushAllCalleeSavedRegsAndAdjustStack();
	void ABI_PopAllCalleeSavedRegsAndAdjustStack();

	// A function that doesn't know anything about it's surroundings, should
	// be surrounded by these to establish a safe environment, where it can roam free.
	// An example is a backpatch injected function.
	void ABI_PushAllCallerSavedRegsAndAdjustStack();
	void ABI_PopAllCallerSavedRegsAndAdjustStack();

	unsigned int ABI_GetAlignedFrameSize(unsigned int frameSize);
	void ABI_AlignStack(unsigned int frameSize);
	void ABI_RestoreStack(unsigned int frameSize);

	// Sets up a __cdecl function.
	// Only x64 really needs the parameter count.
	void ABI_EmitPrologue(int maxCallParams);
	void ABI_EmitEpilogue(int maxCallParams);

	#if PPSSPP_ARCH(X86)
	inline int ABI_GetNumXMMRegs() { return 8; }
	#else
	inline int ABI_GetNumXMMRegs() { return 16; }
	#endif
};  // class XEmitter


// Everything that needs to generate X86 code should inherit from this.
// You get memory management for free, plus, you can use all the MOV etc functions without
// having to prefix them with gen-> or something similar.

class XCodeBlock : public CodeBlock<XEmitter> {
public:
	void PoisonMemory(int offset) override;
	bool RipAccessible(const void *ptr) const {
		// For debugging
		// return false;
#if PPSSPP_ARCH(X86)
		return true;
#else
		ptrdiff_t diff = GetCodePtr() - (const uint8_t *)ptr;
		return diff > -0x7FFFFFE0 && diff < 0x7FFFFFE0;
#endif
	}
};

}  // namespace
