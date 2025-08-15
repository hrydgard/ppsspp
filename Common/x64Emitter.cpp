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

#include "ppsspp_config.h"
#include <cstring>

#include "x64Emitter.h"
#include "ABI.h"
#include "CPUDetect.h"
#include "MemoryUtil.h"

#define PRIx64 "llx"

namespace Gen
{

struct NormalOpDef
{
	u8 toRm8, toRm32, fromRm8, fromRm32, imm8, imm32, simm8, eaximm8, eaximm32, ext;
};

// 0xCC is code for invalid combination of immediates
static const NormalOpDef normalops[11] =
{
	{0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x83, 0x04, 0x05, 0}, //ADD
	{0x10, 0x11, 0x12, 0x13, 0x80, 0x81, 0x83, 0x14, 0x15, 2}, //ADC

	{0x28, 0x29, 0x2A, 0x2B, 0x80, 0x81, 0x83, 0x2C, 0x2D, 5}, //SUB
	{0x18, 0x19, 0x1A, 0x1B, 0x80, 0x81, 0x83, 0x1C, 0x1D, 3}, //SBB

	{0x20, 0x21, 0x22, 0x23, 0x80, 0x81, 0x83, 0x24, 0x25, 4}, //AND
	{0x08, 0x09, 0x0A, 0x0B, 0x80, 0x81, 0x83, 0x0C, 0x0D, 1}, //OR

	{0x30, 0x31, 0x32, 0x33, 0x80, 0x81, 0x83, 0x34, 0x35, 6}, //XOR
	{0x88, 0x89, 0x8A, 0x8B, 0xC6, 0xC7, 0xCC, 0xCC, 0xCC, 0}, //MOV

	{0x84, 0x85, 0x84, 0x85, 0xF6, 0xF7, 0xCC, 0xA8, 0xA9, 0}, //TEST (to == from)
	{0x38, 0x39, 0x3A, 0x3B, 0x80, 0x81, 0x83, 0x3C, 0x3D, 7}, //CMP

	{0x86, 0x87, 0x86, 0x87, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 7}, //XCHG
};

enum NormalSSEOps
{
	sseCMP         = 0xC2,
	sseADD         = 0x58, //ADD
	sseSUB         = 0x5C, //SUB
	sseAND         = 0x54, //AND
	sseANDN        = 0x55, //ANDN
	sseOR          = 0x56,
	sseXOR         = 0x57,
	sseMUL         = 0x59, //MUL
	sseDIV         = 0x5E, //DIV
	sseMIN         = 0x5D, //MIN
	sseMAX         = 0x5F, //MAX
	sseCOMIS       = 0x2F, //COMIS
	sseUCOMIS      = 0x2E, //UCOMIS
	sseSQRT        = 0x51, //SQRT
	sseRCP         = 0x53, //RCP
	sseRSQRT       = 0x52, //RSQRT (NO DOUBLE PRECISION!!!)
	sseMOVAPfromRM = 0x28, //MOVAP from RM
	sseMOVAPtoRM   = 0x29, //MOVAP to RM
	sseMOVUPfromRM = 0x10, //MOVUP from RM
	sseMOVUPtoRM   = 0x11, //MOVUP to RM
	sseMOVLPfromRM= 0x12,
	sseMOVLPtoRM  = 0x13,
	sseMOVHPfromRM= 0x16,
	sseMOVHPtoRM  = 0x17,
	sseMOVHLPS     = 0x12,
	sseMOVLHPS     = 0x16,
	sseMOVDQfromRM = 0x6F,
	sseMOVDQtoRM   = 0x7F,
	sseMASKMOVDQU  = 0xF7,
	sseLDDQU       = 0xF0,
	sseSHUF        = 0xC6,
	sseMOVNTDQ     = 0xE7,
	sseMOVNTP      = 0x2B,
	sseHADD        = 0x7C,
};


void XEmitter::SetCodePointer(u8 *ptr, u8 *writePtr)
{
	code = ptr;
}

const u8 *XEmitter::GetCodePointer() const
{
	return code;
}

u8 *XEmitter::GetWritableCodePtr()
{
	return code;
}

void XEmitter::ReserveCodeSpace(int bytes)
{
	for (int i = 0; i < bytes; i++)
		*code++ = 0xCC;
}

const u8 *XEmitter::AlignCode4()
{
	int c = int((u64)code & 3);
	if (c)
		ReserveCodeSpace(4-c);
	return code;
}

const u8 *XEmitter::AlignCode16()
{
	int c = int((u64)code & 15);
	if (c)
		ReserveCodeSpace(16-c);
	return code;
}

const u8 *XEmitter::AlignCodePage()
{
	// Memory protection pages matter.
	int page_size = GetMemoryProtectPageSize();
	int c = int((u64)code & (page_size - 1));
	if (c)
		ReserveCodeSpace(page_size - c);
	return code;
}

const u8 *XEmitter::NopAlignCode16() {
	int nops = 16 - ((u64)code & 15);
	if (nops == 16)
		return code;

	// note: the string lengths are obviously not computable with strlen, but are equal to the index.
	// Nop strings from https://stackoverflow.com/questions/25545470/long-multi-byte-nops-commonly-understood-macros-or-other-notation
	static const char * const nopStrings[16] = {
		"",
		"\x90",
		"\x66\x90",
		"\x0f\x1f\00",
		"\x0f\x1f\x40\x00",
		"\x0f\x1f\x44\x00\x00",
		"\x66\x0f\x1f\x44\x00\x00",
		"\x0f\x1f\x80\x00\x00\x00\x00",
		"\x0f\x1f\x84\x00\x00\x00\x00\x00",
		"\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
		"\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
		"\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
		"\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00\x90",
		"\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00\x66\x90",
		"\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00\x0f\x1f\00",
		"\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00\x0f\x1f\x40\x00",
	};

	memcpy(code, nopStrings[nops], nops);
	code += nops;
	return code;
}

// This operation modifies flags; check to see the flags are locked.
// If the flags are locked, we should immediately and loudly fail before
// causing a subtle JIT bug.
void XEmitter::CheckFlags()
{
	_assert_msg_(!flags_locked, "Attempt to modify flags while flags locked!");
}

void XEmitter::WriteModRM(int mod, int reg, int rm)
{
	Write8((u8)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

void XEmitter::WriteSIB(int scale, int index, int base)
{
	Write8((u8)((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

void OpArg::WriteRex(XEmitter *emit, int opBits, int bits, int customOp) const
{
	if (customOp == -1)       customOp = operandReg;
#if PPSSPP_ARCH(AMD64)
	u8 op = 0x40;
	// REX.W (whether operation is a 64-bit operation)
	if (opBits == 64)         op |= 8;
	// REX.R (whether ModR/M reg field refers to R8-R15.
	if (customOp & 8)         op |= 4;
	// REX.X (whether ModR/M SIB index field refers to R8-R15)
	if (indexReg & 8)         op |= 2;
	// REX.B (whether ModR/M rm or SIB base or opcode reg field refers to R8-R15)
	if (offsetOrBaseReg & 8)  op |= 1;
	// Write REX if wr have REX bits to write, or if the operation accesses
	// SIL, DIL, BPL, or SPL.
	if (op != 0x40 ||
	    (scale == SCALE_NONE && bits == 8 && (offsetOrBaseReg & 0x10c) == 4) ||
	    (opBits == 8 && (customOp & 0x10c) == 4))
	{
		emit->Write8(op);
		// Check the operation doesn't access AH, BH, CH, or DH.
		_dbg_assert_((offsetOrBaseReg & 0x100) == 0);
		_dbg_assert_((customOp & 0x100) == 0);
	}
#else
	_dbg_assert_(opBits != 64);
	_dbg_assert_((customOp & 8) == 0 || customOp == -1);
	_dbg_assert_((indexReg & 8) == 0);
	_dbg_assert_((offsetOrBaseReg & 8) == 0);
	_dbg_assert_(opBits != 8 || (customOp & 0x10c) != 4 || customOp == -1);
	_dbg_assert_(scale == SCALE_ATREG || bits != 8 || (offsetOrBaseReg & 0x10c) != 4);
#endif
}

void OpArg::WriteVex(XEmitter* emit, X64Reg regOp1, X64Reg regOp2, int L, int pp, int mmmmm, int W) const
{
	int R = !(regOp1 & 8);
	int X = !(indexReg & 8);
	int B = !(offsetOrBaseReg & 8);

	int vvvv = (regOp2 == X64Reg::INVALID_REG) ? 0xf : (regOp2 ^ 0xf);

	// do we need any VEX fields that only appear in the three-byte form?
	if (X == 1 && B == 1 && W == 0 && mmmmm == 1)
	{
		u8 RvvvvLpp = (R << 7) | (vvvv << 3) | (L << 2) | pp;
		emit->Write8(0xC5);
		emit->Write8(RvvvvLpp);
	}
	else
	{
		u8 RXBmmmmm = (R << 7) | (X << 6) | (B << 5) | mmmmm;
		u8 WvvvvLpp = (W << 7) | (vvvv << 3) | (L << 2) | pp;
		emit->Write8(0xC4);
		emit->Write8(RXBmmmmm);
		emit->Write8(WvvvvLpp);
	}
}

void OpArg::WriteRest(XEmitter *emit, int extraBytes, X64Reg _operandReg,
	bool warn_64bit_offset) const
{
	if (_operandReg == INVALID_REG)
		_operandReg = (X64Reg)this->operandReg;
	int mod = 0;
	int ireg = indexReg;
	bool SIB = false;
	int _offsetOrBaseReg = this->offsetOrBaseReg;

	if (scale == SCALE_RIP) //Also, on 32-bit, just an immediate address
	{
		// Oh, RIP addressing.
		_offsetOrBaseReg = 5;
		emit->WriteModRM(0, _operandReg, _offsetOrBaseReg);
		//TODO : add some checks
#if PPSSPP_ARCH(AMD64)
		u64 ripAddr = (u64)emit->GetCodePointer() + 4 + extraBytes;
		s64 distance = (s64)offset - (s64)ripAddr;
		_assert_msg_(
		             (distance < 0x80000000LL &&
		              distance >=  -0x80000000LL) ||
		             !warn_64bit_offset,
		             "WriteRest: op out of range (0x%" PRIx64 " uses 0x%" PRIx64 ")",
		             ripAddr, offset);
		s32 offs = (s32)distance;
		emit->Write32((u32)offs);
#else
		emit->Write32((u32)offset);
#endif
		return;
	}

	if (scale == 0)
	{
		// Oh, no memory, Just a reg.
		mod = 3; //11
	}
	else if (scale >= 1)
	{
		//Ah good, no scaling.
		if (scale == SCALE_ATREG && !((_offsetOrBaseReg & 7) == 4 || (_offsetOrBaseReg & 7) == 5))
		{
			//Okay, we're good. No SIB necessary.
			int ioff = (int)offset;
			if (ioff == 0)
			{
				mod = 0;
			}
			else if (ioff < -128 || ioff > 127)
			{
				mod = 2; //32-bit displacement
			}
			else
			{
				mod = 1; //8-bit displacement
			}
		}
		else if (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8)
		{
			SIB = true;
			mod = 0;
			_offsetOrBaseReg = 5;
		}
		else //if (scale != SCALE_ATREG)
		{
			if ((_offsetOrBaseReg & 7) == 4) //this would occupy the SIB encoding :(
			{
				//So we have to fake it with SIB encoding :(
				SIB = true;
			}

			if (scale >= SCALE_1 && scale < SCALE_ATREG)
			{
				SIB = true;
			}

			if (scale == SCALE_ATREG && ((_offsetOrBaseReg & 7) == 4))
			{
				SIB = true;
				ireg = _offsetOrBaseReg;
			}

			//Okay, we're fine. Just disp encoding.
			//We need displacement. Which size?
			int ioff = (int)(s64)offset;
			if (ioff < -128 || ioff > 127)
			{
				mod = 2; //32-bit displacement
			}
			else
			{
				mod = 1; //8-bit displacement
			}
		}
	}

	// Okay. Time to do the actual writing
	// ModRM byte:
	int oreg = _offsetOrBaseReg;
	if (SIB)
		oreg = 4;

	// TODO(ector): WTF is this if about? I don't remember writing it :-)
	//if (RIP)
	//    oreg = 5;

	emit->WriteModRM(mod, _operandReg&7, oreg&7);

	if (SIB)
	{
		//SIB byte
		int ss;
		switch (scale)
		{
		case SCALE_NONE: _offsetOrBaseReg = 4; ss = 0; break; //RSP
		case SCALE_1: ss = 0; break;
		case SCALE_2: ss = 1; break;
		case SCALE_4: ss = 2; break;
		case SCALE_8: ss = 3; break;
		case SCALE_NOBASE_2: ss = 1; break;
		case SCALE_NOBASE_4: ss = 2; break;
		case SCALE_NOBASE_8: ss = 3; break;
		case SCALE_ATREG: ss = 0; break;
		default: _assert_msg_(false, "Invalid scale for SIB byte"); ss = 0; break;
		}
		emit->Write8((u8)((ss << 6) | ((ireg&7)<<3) | (_offsetOrBaseReg&7)));
	}

	if (mod == 1) //8-bit disp
	{
		emit->Write8((u8)(s8)(s32)offset);
	}
	else if (mod == 2 || (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8)) //32-bit disp
	{
		emit->Write32((u32)offset);
	}
}

// W = operand extended width (1 if 64-bit)
// R = register# upper bit
// X = scale amnt upper bit
// B = base register# upper bit
void XEmitter::Rex(int w, int r, int x, int b)
{
	w = w ? 1 : 0;
	r = r ? 1 : 0;
	x = x ? 1 : 0;
	b = b ? 1 : 0;
	u8 rx = (u8)(0x40 | (w << 3) | (r << 2) | (x << 1) | (b));
	if (rx != 0x40)
		Write8(rx);
}

void XEmitter::JMP(const u8 *addr, bool force5Bytes)
{
	u64 fn = (u64)addr;
	if (!force5Bytes)
	{
		s64 distance = (s64)(fn - ((u64)code + 2));
		_assert_msg_(distance >= -0x80 && distance < 0x80,
			     "Jump target too far away, needs force5Bytes = true");
		//8 bits will do
		Write8(0xEB);
		Write8((u8)(s8)distance);
	}
	else
	{
		s64 distance = (s64)(fn - ((u64)code + 5));

		_assert_msg_(distance >= -0x80000000LL && distance < 0x80000000LL,
		             "Jump target too far away, needs indirect register");
		Write8(0xE9);
		Write32((u32)(s32)distance);
	}
}

void XEmitter::JMPptr(const OpArg &arg2)
{
	OpArg arg = arg2;
	if (arg.IsImm()) _assert_msg_(false, "JMPptr - Imm argument");
	arg.operandReg = 4;
	arg.WriteRex(this, 0, 0);
	Write8(0xFF);
	arg.WriteRest(this);
}

//Can be used to trap other processors, before overwriting their code
// not used in dolphin
void XEmitter::JMPself()
{
	Write8(0xEB);
	Write8(0xFE);
}

void XEmitter::CALLptr(OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(false, "CALLptr - Imm argument");
	arg.operandReg = 2;
	arg.WriteRex(this, 0, 0);
	Write8(0xFF);
	arg.WriteRest(this);
}

void XEmitter::CALL(const void *fnptr)
{
	u64 distance = u64(fnptr) - (u64(code) + 5);
	_assert_msg_(distance < 0x0000000080000000ULL ||
	             distance >=  0xFFFFFFFF80000000ULL,
	             "CALL out of range (%p calls %p)", code, fnptr);
	Write8(0xE8);
	Write32(u32(distance));
}

bool XEmitter::CanCALLDirect(const void *fnptr) {
	u64 distance = u64(fnptr) - (u64(code) + 5);
	return distance < 0x0000000080000000ULL || distance >= 0xFFFFFFFF80000000ULL;
}

FixupBranch XEmitter::J(bool force5bytes)
{
	FixupBranch branch;
	branch.type = force5bytes ? 1 : 0;
	branch.ptr = code + (force5bytes ? 5 : 2);
	if (!force5bytes)
	{
		//8 bits will do
		Write8(0xEB);
		Write8(0);
	}
	else
	{
		Write8(0xE9);
		Write32(0);
	}
	return branch;
}

FixupBranch XEmitter::J_CC(CCFlags conditionCode, bool force5bytes)
{
	FixupBranch branch;
	branch.type = force5bytes ? 1 : 0;
	branch.ptr = code + (force5bytes ? 6 : 2);
	if (!force5bytes)
	{
		//8 bits will do
		Write8(0x70 + conditionCode);
		Write8(0);
	}
	else
	{
		Write8(0x0F);
		Write8(0x80 + conditionCode);
		Write32(0);
	}
	return branch;
}

void XEmitter::J_CC(CCFlags conditionCode, const u8* addr, bool force5bytes)
{
	u64 fn = (u64)addr;
	s64 distance = (s64)(fn - ((u64)code + 2));
	if (distance < -0x80 || distance >= 0x80 || force5bytes)
	{
		distance = (s64)(fn - ((u64)code + 6));
		_assert_msg_(distance >= -0x80000000LL && distance < 0x80000000LL,
		             "Jump target too far away, needs indirect register");
		Write8(0x0F);
		Write8(0x80 + conditionCode);
		Write32((u32)(s32)distance);
	}
	else
	{
		Write8(0x70 + conditionCode);
		Write8((u8)(s8)distance);
	}
}

void XEmitter::SetJumpTarget(const FixupBranch &branch)
{
	if (branch.type == 0)
	{
		s64 distance = (s64)(code - branch.ptr);
		_assert_msg_(distance >= -0x80 && distance < 0x80, "Jump target too far away, needs force5Bytes = true");
		branch.ptr[-1] = (u8)(s8)distance;
	}
	else if (branch.type == 1)
	{
		s64 distance = (s64)(code - branch.ptr);
		_assert_msg_(distance >= -0x80000000LL && distance < 0x80000000LL, "Jump target too far away, needs indirect register");
		const s32 distance32 = static_cast<s32>(distance);
		std::memcpy(branch.ptr - sizeof(s32), &distance32, sizeof(s32));
	}
}

// INC/DEC considered harmful on newer CPUs due to partial flag set.
// Use ADD, SUB instead.

/*
void XEmitter::INC(int bits, OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(false, "INC - Imm argument");
	arg.operandReg = 0;
	if (bits == 16) {Write8(0x66);}
	arg.WriteRex(this, bits, bits);
	Write8(bits == 8 ? 0xFE : 0xFF);
	arg.WriteRest(this);
}
void XEmitter::DEC(int bits, OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(false, "DEC - Imm argument");
	arg.operandReg = 1;
	if (bits == 16) {Write8(0x66);}
	arg.WriteRex(this, bits, bits);
	Write8(bits == 8 ? 0xFE : 0xFF);
	arg.WriteRest(this);
}
*/

//Single byte opcodes
//There is no PUSHAD/POPAD in 64-bit mode.
void XEmitter::INT3() {Write8(0xCC);}
void XEmitter::RET()  {Write8(0xC3);}
void XEmitter::RET_FAST()  {Write8(0xF3); Write8(0xC3);} //two-byte return (rep ret) - recommended by AMD optimization manual for the case of jumping to a ret

// The first sign of decadence: optimized NOPs.
void XEmitter::NOP(size_t size)
{
	_dbg_assert_((int)size > 0);
	while (true)
	{
		switch (size)
		{
		case 0:
			return;
		case 1:
			Write8(0x90);
			return;
		case 2:
			Write8(0x66); Write8(0x90);
			return;
		case 3:
			Write8(0x0F); Write8(0x1F); Write8(0x00);
			return;
		case 4:
			Write8(0x0F); Write8(0x1F); Write8(0x40); Write8(0x00);
			return;
		case 5:
			Write8(0x0F); Write8(0x1F); Write8(0x44); Write8(0x00);
			Write8(0x00);
			return;
		case 6:
			Write8(0x66); Write8(0x0F); Write8(0x1F); Write8(0x44);
			Write8(0x00); Write8(0x00);
			return;
		case 7:
			Write8(0x0F); Write8(0x1F); Write8(0x80); Write8(0x00);
			Write8(0x00); Write8(0x00); Write8(0x00);
			return;
		case 8:
			Write8(0x0F); Write8(0x1F); Write8(0x84); Write8(0x00);
			Write8(0x00); Write8(0x00); Write8(0x00); Write8(0x00);
			return;
		case 9:
			Write8(0x66); Write8(0x0F); Write8(0x1F); Write8(0x84);
			Write8(0x00); Write8(0x00); Write8(0x00); Write8(0x00);
			Write8(0x00);
			return;
		case 10:
			Write8(0x66); Write8(0x66); Write8(0x0F); Write8(0x1F);
			Write8(0x84); Write8(0x00); Write8(0x00); Write8(0x00);
			Write8(0x00); Write8(0x00);
			return;
		default:
			// Even though x86 instructions are allowed to be up to 15 bytes long,
			// AMD advises against using NOPs longer than 11 bytes because they
			// carry a performance penalty on CPUs older than AMD family 16h.
			Write8(0x66); Write8(0x66); Write8(0x66); Write8(0x0F);
			Write8(0x1F); Write8(0x84); Write8(0x00); Write8(0x00);
			Write8(0x00); Write8(0x00); Write8(0x00);
			size -= 11;
			continue;
		}
	}
}

void XEmitter::PAUSE() {Write8(0xF3); NOP();} //use in tight spinloops for energy saving on some cpu
void XEmitter::CLC()  {CheckFlags(); Write8(0xF8);} //clear carry
void XEmitter::CMC()  {CheckFlags(); Write8(0xF5);} //flip carry
void XEmitter::STC()  {CheckFlags(); Write8(0xF9);} //set carry

//TODO: xchg ah, al ???
void XEmitter::XCHG_AHAL()
{
	Write8(0x86);
	Write8(0xe0);
	// alt. 86 c4
}

//These two can not be executed on early Intel 64-bit CPU:s, only on AMD!
void XEmitter::LAHF() {Write8(0x9F);}
void XEmitter::SAHF() {CheckFlags(); Write8(0x9E);}

void XEmitter::PUSHF() {Write8(0x9C);}
void XEmitter::POPF()  {CheckFlags(); Write8(0x9D);}

void XEmitter::LFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xE8);}
void XEmitter::MFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xF0);}
void XEmitter::SFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xF8);}

void XEmitter::WriteSimple1Byte(int bits, u8 byte, X64Reg reg)
{
	if (bits == 16)
		Write8(0x66);
	Rex(bits == 64, 0, 0, (int)reg >> 3);
	Write8(byte + ((int)reg & 7));
}

void XEmitter::WriteSimple2Byte(int bits, u8 byte1, u8 byte2, X64Reg reg)
{
	if (bits == 16)
		Write8(0x66);
	Rex(bits==64, 0, 0, (int)reg >> 3);
	Write8(byte1);
	Write8(byte2 + ((int)reg & 7));
}

void XEmitter::CWD(int bits)
{
	if (bits == 16)
		Write8(0x66);
	Rex(bits == 64, 0, 0, 0);
	Write8(0x99);
}

void XEmitter::CBW(int bits)
{
	if (bits == 8)
		Write8(0x66);
	Rex(bits == 32, 0, 0, 0);
	Write8(0x98);
}

//Simple opcodes


//push/pop do not need wide to be 64-bit
void XEmitter::PUSH(X64Reg reg) {WriteSimple1Byte(32, 0x50, reg);}
void XEmitter::POP(X64Reg reg)  {WriteSimple1Byte(32, 0x58, reg);}

void XEmitter::PUSH(int bits, const OpArg &reg)
{
	if (reg.IsSimpleReg())
		PUSH(reg.GetSimpleReg());
	else if (reg.IsImm())
	{
		switch (reg.GetImmBits())
		{
		case 8:
			Write8(0x6A);
			Write8((u8)(s8)reg.offset);
			break;
		case 16:
			Write8(0x66);
			Write8(0x68);
			Write16((u16)(s16)(s32)reg.offset);
			break;
		case 32:
			Write8(0x68);
			Write32((u32)reg.offset);
			break;
		default:
			_assert_msg_(false, "PUSH - Bad imm bits");
			break;
		}
	}
	else
	{
		if (bits == 16)
			Write8(0x66);
		reg.WriteRex(this, bits, bits);
		Write8(0xFF);
		reg.WriteRest(this, 0, (X64Reg)6);
	}
}

void XEmitter::POP(int /*bits*/, const OpArg &reg)
{
	if (reg.IsSimpleReg())
		POP(reg.GetSimpleReg());
	else
		_assert_msg_(false, "POP - Unsupported encoding");
}

void XEmitter::BSWAP(int bits, X64Reg reg)
{
	if (bits >= 32)
	{
		WriteSimple2Byte(bits, 0x0F, 0xC8, reg);
	}
	else if (bits == 16)
	{
		ROL(16, R(reg), Imm8(8));
	}
	else if (bits == 8)
	{
		// Do nothing - can't bswap a single byte...
	}
	else
	{
		_assert_msg_(false, "BSWAP - Wrong number of bits");
	}
}

// Undefined opcode - reserved
// If we ever need a way to always cause a non-breakpoint hard exception...
void XEmitter::UD2()
{
	Write8(0x0F);
	Write8(0x0B);
}

void XEmitter::PREFETCH(PrefetchLevel level, OpArg arg)
{
	_assert_msg_(!arg.IsImm(), "PREFETCH - Imm argument");
	arg.operandReg = (u8)level;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(0x18);
	arg.WriteRest(this);
}

void XEmitter::SETcc(CCFlags flag, OpArg dest)
{
	_assert_msg_(!dest.IsImm(), "SETcc - Imm argument");
	dest.operandReg = 0;
	dest.WriteRex(this, 0, 8);
	Write8(0x0F);
	Write8(0x90 + (u8)flag);
	dest.WriteRest(this);
}

void XEmitter::CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag)
{
	_assert_msg_(!src.IsImm(), "CMOVcc - Imm argument");
	_assert_msg_(bits != 8, "CMOVcc - 8 bits unsupported");
	if (bits == 16)
		Write8(0x66);
	src.operandReg = dest;
	src.WriteRex(this, bits, bits);
	Write8(0x0F);
	Write8(0x40 + (u8)flag);
	src.WriteRest(this);
}

void XEmitter::WriteMulDivType(int bits, OpArg src, int ext)
{
	_assert_msg_(!src.IsImm(), "WriteMulDivType - Imm argument");
	CheckFlags();
	src.operandReg = ext;
	if (bits == 16)
		Write8(0x66);
	src.WriteRex(this, bits, bits, 0);
	if (bits == 8)
	{
		Write8(0xF6);
	}
	else
	{
		Write8(0xF7);
	}
	src.WriteRest(this);
}

void XEmitter::MUL(int bits, OpArg src)  {WriteMulDivType(bits, src, 4);}
void XEmitter::DIV(int bits, OpArg src)  {WriteMulDivType(bits, src, 6);}
void XEmitter::IMUL(int bits, OpArg src) {WriteMulDivType(bits, src, 5);}
void XEmitter::IDIV(int bits, OpArg src) {WriteMulDivType(bits, src, 7);}
void XEmitter::NEG(int bits, OpArg src)  {WriteMulDivType(bits, src, 3);}
void XEmitter::NOT(int bits, OpArg src)  {WriteMulDivType(bits, src, 2);}

void XEmitter::WriteBitSearchType(int bits, X64Reg dest, OpArg src, u8 byte2, bool rep)
{
	_assert_msg_(!src.IsImm(), "WriteBitSearchType - Imm argument");
	CheckFlags();
	src.operandReg = (u8)dest;
	if (bits == 16)
		Write8(0x66);
	if (rep)
		Write8(0xF3);
	src.WriteRex(this, bits, bits);
	Write8(0x0F);
	Write8(byte2);
	src.WriteRest(this);
}

void XEmitter::MOVNTI(int bits, OpArg dest, X64Reg src)
{
	if (bits <= 16)
		_assert_msg_(false, "MOVNTI - bits<=16");
	WriteBitSearchType(bits, src, dest, 0xC3);
}

void XEmitter::BSF(int bits, X64Reg dest, OpArg src) {WriteBitSearchType(bits,dest,src,0xBC);} //bottom bit to top bit
void XEmitter::BSR(int bits, X64Reg dest, OpArg src) {WriteBitSearchType(bits,dest,src,0xBD);} //top bit to bottom bit

void XEmitter::TZCNT(int bits, X64Reg dest, OpArg src)
{
	CheckFlags();
	_assert_msg_(cpu_info.bBMI1, "Trying to use BMI1 on a system that doesn't support it.");
	WriteBitSearchType(bits, dest, src, 0xBC, true);
}
void XEmitter::LZCNT(int bits, X64Reg dest, OpArg src)
{
	CheckFlags();
	_assert_msg_(cpu_info.bLZCNT, "Trying to use LZCNT on a system that doesn't support it.");
	WriteBitSearchType(bits, dest, src, 0xBD, true);
}

void XEmitter::MOVSX(int dbits, int sbits, X64Reg dest, OpArg src)
{
	_assert_msg_(!src.IsImm(), "MOVSX - Imm argument");
	if (dbits == sbits)
	{
		MOV(dbits, R(dest), src);
		return;
	}
	src.operandReg = (u8)dest;
	if (dbits == 16)
		Write8(0x66);
	src.WriteRex(this, dbits, sbits);
	if (sbits == 8)
	{
		Write8(0x0F);
		Write8(0xBE);
	}
	else if (sbits == 16)
	{
		Write8(0x0F);
		Write8(0xBF);
	}
	else if (sbits == 32 && dbits == 64)
	{
		Write8(0x63);
	}
	else
	{
		Crash();
	}
	src.WriteRest(this);
}

void XEmitter::MOVZX(int dbits, int sbits, X64Reg dest, OpArg src)
{
	_assert_msg_(!src.IsImm(), "MOVZX - Imm argument");
	if (dbits == sbits)
	{
		MOV(dbits, R(dest), src);
		return;
	}
	src.operandReg = (u8)dest;
	if (dbits == 16)
		Write8(0x66);
	//the 32bit result is automatically zero extended to 64bit
	src.WriteRex(this, dbits == 64 ? 32 : dbits, sbits);
	if (sbits == 8)
	{
		Write8(0x0F);
		Write8(0xB6);
	}
	else if (sbits == 16)
	{
		Write8(0x0F);
		Write8(0xB7);
	}
	else if (sbits == 32 && dbits == 64)
	{
		Write8(0x8B);
	}
	else
	{
		_assert_msg_(false, "MOVZX - Invalid size");
	}
	src.WriteRest(this);
}

void XEmitter::MOVBE(int bits, const OpArg& dest, const OpArg& src)
{
	_assert_msg_(cpu_info.bMOVBE, "Generating MOVBE on a system that does not support it.");
	if (bits == 8)
	{
		MOV(bits, dest, src);
		return;
	}

	if (bits == 16)
		Write8(0x66);

	if (dest.IsSimpleReg())
	{
		_assert_msg_(!src.IsSimpleReg() && !src.IsImm(), "MOVBE: Loading from !mem");
		src.WriteRex(this, bits, bits, dest.GetSimpleReg());
		Write8(0x0F); Write8(0x38); Write8(0xF0);
		src.WriteRest(this, 0, dest.GetSimpleReg());
	}
	else if (src.IsSimpleReg())
	{
		_assert_msg_(!dest.IsSimpleReg() && !dest.IsImm(), "MOVBE: Storing to !mem");
		dest.WriteRex(this, bits, bits, src.GetSimpleReg());
		Write8(0x0F); Write8(0x38); Write8(0xF1);
		dest.WriteRest(this, 0, src.GetSimpleReg());
	}
	else
	{
		_assert_msg_(false, "MOVBE: Not loading or storing to mem");
	}
}


void XEmitter::LEA(int bits, X64Reg dest, OpArg src)
{
	_assert_msg_(!src.IsImm(), "LEA - Imm argument");
	src.operandReg = (u8)dest;
	if (bits == 16)
		Write8(0x66); //TODO: performance warning
	src.WriteRex(this, bits, bits);
	Write8(0x8D);
	src.WriteRest(this, 0, INVALID_REG, bits == 64);
}

//shift can be either imm8 or cl
void XEmitter::WriteShift(int bits, OpArg dest, OpArg &shift, int ext)
{
	CheckFlags();
	bool writeImm = false;
	if (dest.IsImm())
	{
		_assert_msg_(false, "WriteShift - can't shift imms");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(false, "WriteShift - illegal argument");
	}
	dest.operandReg = ext;
	if (bits == 16)
		Write8(0x66);
	dest.WriteRex(this, bits, bits, 0);
	if (shift.GetImmBits() == 8)
	{
		//ok an imm
		u8 imm = (u8)shift.offset;
		if (imm == 1)
		{
			Write8(bits == 8 ? 0xD0 : 0xD1);
		}
		else
		{
			writeImm = true;
			Write8(bits == 8 ? 0xC0 : 0xC1);
		}
	}
	else
	{
		Write8(bits == 8 ? 0xD2 : 0xD3);
	}
	dest.WriteRest(this, writeImm ? 1 : 0);
	if (writeImm)
		Write8((u8)shift.offset);
}

// large rotates and shift are slower on intel than amd
// intel likes to rotate by 1, and the op is smaller too
void XEmitter::ROL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 0);}
void XEmitter::ROR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 1);}
void XEmitter::RCL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 2);}
void XEmitter::RCR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 3);}
void XEmitter::SHL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 4);}
void XEmitter::SHR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 5);}
void XEmitter::SAR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 7);}

// index can be either imm8 or register, don't use memory destination because it's slow
void XEmitter::WriteBitTest(int bits, OpArg &dest, OpArg &index, int ext)
{
	CheckFlags();
	if (dest.IsImm())
	{
		_assert_msg_(false, "WriteBitTest - can't test imms");
	}
	if ((index.IsImm() && index.GetImmBits() != 8))
	{
		_assert_msg_(false, "WriteBitTest - illegal argument");
	}
	if (bits == 16)
		Write8(0x66);
	if (index.IsImm())
	{
		dest.WriteRex(this, bits, bits);
		Write8(0x0F); Write8(0xBA);
		dest.WriteRest(this, 1, (X64Reg)ext);
		Write8((u8)index.offset);
	}
	else
	{
		X64Reg operand = index.GetSimpleReg();
		dest.WriteRex(this, bits, bits, operand);
		Write8(0x0F); Write8(0x83 + 8*ext);
		dest.WriteRest(this, 1, operand);
	}
}

void XEmitter::BT(int bits, OpArg dest, OpArg index)  {WriteBitTest(bits, dest, index, 4);}
void XEmitter::BTS(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 5);}
void XEmitter::BTR(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 6);}
void XEmitter::BTC(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 7);}

//shift can be either imm8 or cl
void XEmitter::SHRD(int bits, OpArg dest, OpArg src, OpArg shift)
{
	CheckFlags();
	if (dest.IsImm())
	{
		_assert_msg_(false, "SHRD - can't use imms as destination");
	}
	if (!src.IsSimpleReg())
	{
		_assert_msg_(false, "SHRD - must use simple register as source");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(false, "SHRD - illegal shift");
	}
	if (bits == 16)
		Write8(0x66);
	X64Reg operand = src.GetSimpleReg();
	dest.WriteRex(this, bits, bits, operand);
	if (shift.GetImmBits() == 8)
	{
		Write8(0x0F); Write8(0xAC);
		dest.WriteRest(this, 1, operand);
		Write8((u8)shift.offset);
	}
	else
	{
		Write8(0x0F); Write8(0xAD);
		dest.WriteRest(this, 0, operand);
	}
}

void XEmitter::SHLD(int bits, OpArg dest, OpArg src, OpArg shift)
{
	CheckFlags();
	if (dest.IsImm())
	{
		_assert_msg_(false, "SHLD - can't use imms as destination");
	}
	if (!src.IsSimpleReg())
	{
		_assert_msg_(false, "SHLD - must use simple register as source");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(false, "SHLD - illegal shift");
	}
	if (bits == 16)
		Write8(0x66);
	X64Reg operand = src.GetSimpleReg();
	dest.WriteRex(this, bits, bits, operand);
	if (shift.GetImmBits() == 8)
	{
		Write8(0x0F); Write8(0xA4);
		dest.WriteRest(this, 1, operand);
		Write8((u8)shift.offset);
	}
	else
	{
		Write8(0x0F); Write8(0xA5);
		dest.WriteRest(this, 0, operand);
	}
}

void OpArg::WriteSingleByteOp(XEmitter *emit, u8 op, X64Reg _operandReg, int bits)
{
	if (bits == 16)
		emit->Write8(0x66);

	this->operandReg = (u8)_operandReg;
	WriteRex(emit, bits, bits);
	emit->Write8(op);
	WriteRest(emit);
}

//operand can either be immediate or register
void OpArg::WriteNormalOp(XEmitter *emit, bool toRM, NormalOp op, const OpArg &operand, int bits) const
{
	X64Reg _operandReg;
	if (IsImm())
	{
		_assert_msg_(false, "WriteNormalOp - Imm argument, wrong order");
	}

	if (bits == 16)
		emit->Write8(0x66);

	int immToWrite = 0;

	if (operand.IsImm())
	{
		WriteRex(emit, bits, bits);

		if (!toRM)
		{
			_assert_msg_(false, "WriteNormalOp - Writing to Imm (!toRM)");
		}

		if (operand.scale == SCALE_IMM8 && bits == 8)
		{
			// op al, imm8
			if (!scale && offsetOrBaseReg == AL && normalops[op].eaximm8 != 0xCC)
			{
				emit->Write8(normalops[op].eaximm8);
				emit->Write8((u8)operand.offset);
				return;
			}
			// mov reg, imm8
			if (!scale && op == nrmMOV)
			{
				emit->Write8(0xB0 + (offsetOrBaseReg & 7));
				emit->Write8((u8)operand.offset);
				return;
			}
			// op r/m8, imm8
			emit->Write8(normalops[op].imm8);
			immToWrite = 8;
		}
		else if ((operand.scale == SCALE_IMM16 && bits == 16) ||
				 (operand.scale == SCALE_IMM32 && bits == 32) ||
				 (operand.scale == SCALE_IMM32 && bits == 64))
		{
			// Try to save immediate size if we can, but first check to see
			// if the instruction supports simm8.
			// op r/m, imm8
			if (normalops[op].simm8 != 0xCC &&
			    ((operand.scale == SCALE_IMM16 && (s16)operand.offset == (s8)operand.offset) ||
			     (operand.scale == SCALE_IMM32 && (s32)operand.offset == (s8)operand.offset)))
			{
				emit->Write8(normalops[op].simm8);
				immToWrite = 8;
			}
			else
			{
				// mov reg, imm
				if (!scale && op == nrmMOV && bits != 64)
				{
					emit->Write8(0xB8 + (offsetOrBaseReg & 7));
					if (bits == 16)
						emit->Write16((u16)operand.offset);
					else
						emit->Write32((u32)operand.offset);
					return;
				}
				// op eax, imm
				if (!scale && offsetOrBaseReg == EAX && normalops[op].eaximm32 != 0xCC)
				{
					emit->Write8(normalops[op].eaximm32);
					if (bits == 16)
						emit->Write16((u16)operand.offset);
					else
						emit->Write32((u32)operand.offset);
					return;
				}
				// op r/m, imm
				emit->Write8(normalops[op].imm32);
				immToWrite = bits == 16 ? 16 : 32;
			}
		}
		else if ((operand.scale == SCALE_IMM8 && bits == 16) ||
				 (operand.scale == SCALE_IMM8 && bits == 32) ||
				 (operand.scale == SCALE_IMM8 && bits == 64))
		{
			// op r/m, imm8
			emit->Write8(normalops[op].simm8);
			immToWrite = 8;
		}
		else if (operand.scale == SCALE_IMM64 && bits == 64)
		{
			if (scale)
			{
				_assert_msg_(false, "WriteNormalOp - MOV with 64-bit imm requres register destination");
			}
			// mov reg64, imm64
			else if (op == nrmMOV)
			{
				emit->Write8(0xB8 + (offsetOrBaseReg & 7));
				emit->Write64((u64)operand.offset);
				return;
			}
			_assert_msg_(false, "WriteNormalOp - Only MOV can take 64-bit imm");
		}
		else
		{
			_assert_msg_(false, "WriteNormalOp - Unhandled case");
		}
		_operandReg = (X64Reg)normalops[op].ext; //pass extension in REG of ModRM
	}
	else
	{
		_operandReg = (X64Reg)operand.offsetOrBaseReg;
		WriteRex(emit, bits, bits, _operandReg);
		// op r/m, reg
		if (toRM)
		{
			emit->Write8(bits == 8 ? normalops[op].toRm8 : normalops[op].toRm32);
		}
		// op reg, r/m
		else
		{
			emit->Write8(bits == 8 ? normalops[op].fromRm8 : normalops[op].fromRm32);
		}
	}
	WriteRest(emit, immToWrite >> 3, _operandReg);
	switch (immToWrite)
	{
	case 0:
		break;
	case 8:
		emit->Write8((u8)operand.offset);
		break;
	case 16:
		emit->Write16((u16)operand.offset);
		break;
	case 32:
		emit->Write32((u32)operand.offset);
		break;
	default:
		_assert_msg_(false, "WriteNormalOp - Unhandled case");
	}
}

void XEmitter::WriteNormalOp(XEmitter *emit, int bits, NormalOp op, const OpArg &a1, const OpArg &a2)
{
	if (a1.IsImm())
	{
		//Booh! Can't write to an imm
		_assert_msg_(false, "WriteNormalOp - a1 cannot be imm");
		return;
	}
	if (a2.IsImm())
	{
		a1.WriteNormalOp(emit, true, op, a2, bits);
	}
	else
	{
		if (a1.IsSimpleReg())
		{
			a2.WriteNormalOp(emit, false, op, a1, bits);
		}
		else
		{
			_assert_msg_(a2.IsSimpleReg() || a2.IsImm(), "WriteNormalOp - a1 and a2 cannot both be memory");
			a1.WriteNormalOp(emit, true, op, a2, bits);
		}
	}
}

void XEmitter::ADD (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmADD, a1, a2);}
void XEmitter::ADC (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmADC, a1, a2);}
void XEmitter::SUB (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmSUB, a1, a2);}
void XEmitter::SBB (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmSBB, a1, a2);}
void XEmitter::AND (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmAND, a1, a2);}
void XEmitter::OR  (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmOR , a1, a2);}
void XEmitter::XOR (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmXOR, a1, a2);}
void XEmitter::MOV (int bits, const OpArg &a1, const OpArg &a2)
{
	if (a1.IsSimpleReg() && a2.IsSimpleReg() && a1.GetSimpleReg() == a2.GetSimpleReg())
		ERROR_LOG(Log::JIT, "Redundant MOV @ %p - bug in JIT?", code);
	WriteNormalOp(this, bits, nrmMOV, a1, a2);
}
void XEmitter::TEST(int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmTEST, a1, a2);}
void XEmitter::CMP (int bits, const OpArg &a1, const OpArg &a2) {CheckFlags(); WriteNormalOp(this, bits, nrmCMP, a1, a2);}
void XEmitter::XCHG(int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmXCHG, a1, a2);}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a1, OpArg a2)
{
	CheckFlags();
	if (bits == 8)
	{
		_assert_msg_(false, "IMUL - illegal bit size!");
		return;
	}

	if (a1.IsImm())
	{
		_assert_msg_(false, "IMUL - second arg cannot be imm!");
		return;
	}

	if (!a2.IsImm())
	{
		_assert_msg_(false, "IMUL - third arg must be imm!");
		return;
	}

	if (bits == 16)
		Write8(0x66);
	a1.WriteRex(this, bits, bits, regOp);

	if (a2.GetImmBits() == 8 ||
	    (a2.GetImmBits() == 16 && (s8)a2.offset == (s16)a2.offset) ||
	    (a2.GetImmBits() == 32 && (s8)a2.offset == (s32)a2.offset))
	{
		Write8(0x6B);
		a1.WriteRest(this, 1, regOp);
		Write8((u8)a2.offset);
	}
	else
	{
		Write8(0x69);
		if (a2.GetImmBits() == 16 && bits == 16)
		{
			a1.WriteRest(this, 2, regOp);
			Write16((u16)a2.offset);
		}
		else if (a2.GetImmBits() == 32 && (bits == 32 || bits == 64))
		{
			a1.WriteRest(this, 4, regOp);
			Write32((u32)a2.offset);
		}
		else
		{
			_assert_msg_(false, "IMUL - unhandled case!");
		}
	}
}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a)
{
	CheckFlags();
	if (bits == 8)
	{
		_assert_msg_(false, "IMUL - illegal bit size!");
		return;
	}

	if (a.IsImm())
	{
		IMUL(bits, regOp, R(regOp), a) ;
		return;
	}

	if (bits == 16)
		Write8(0x66);
	a.WriteRex(this, bits, bits, regOp);
	Write8(0x0F);
	Write8(0xAF);
	a.WriteRest(this, 0, regOp);
}


void XEmitter::WriteSSEOp(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes)
{
	if (opPrefix)
		Write8(opPrefix);
	arg.operandReg = regOp;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	if (op > 0xFF)
		Write8((op >> 8) & 0xFF);
	Write8(op & 0xFF);
	arg.WriteRest(this, extrabytes);
}

void XEmitter::WriteAVXOp(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes, int W) {
	WriteAVXOp(bits, opPrefix, op, regOp, INVALID_REG, arg, extrabytes, W);
}

void XEmitter::WriteAVX12Op(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes, int W) {
	WriteAVX12Op(bits, opPrefix, op, regOp, INVALID_REG, arg, extrabytes, W);
}

void XEmitter::WriteAVX2Op(int bits, u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes, int W) {
	WriteAVX2Op(bits, opPrefix, op, regOp, INVALID_REG, arg, extrabytes, W);
}

static int GetVEXmmmmm(u16 op)
{
	// Currently, only 0x38 and 0x3A are used as secondary escape byte.
	if ((op >> 8) == 0x3A)
		return 3;
	else if ((op >> 8) == 0x38)
		return 2;
	else
		return 1;
}

static int GetVEXpp(u8 opPrefix)
{
	if (opPrefix == 0x66)
		return 1;
	else if (opPrefix == 0xF3)
		return 2;
	else if (opPrefix == 0xF2)
		return 3;
	else
		return 0;
}

void XEmitter::WriteAVXOp(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes, int W) {
	_assert_msg_(cpu_info.bAVX, "Trying to use AVX on a system that doesn't support it.");
	int mmmmm = GetVEXmmmmm(op);
	int pp = GetVEXpp(opPrefix);
	// Note: W "size" is not the vector size here.
	arg.WriteVex(this, regOp1, regOp2, bits == 256 ? 1 : 0, pp, mmmmm, W);
	Write8(op & 0xFF);
	arg.WriteRest(this, extrabytes, regOp1);
}

void XEmitter::WriteAVX12Op(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes, int W) {
	_assert_msg_(bits != 256 || cpu_info.bAVX2, "Trying to use AVX2 on a system that doesn't support it.");
	WriteAVXOp(bits, opPrefix, op, regOp1, regOp2, arg, extrabytes, W);
}

void XEmitter::WriteAVX2Op(int bits, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes, int W) {
	_assert_msg_(cpu_info.bAVX2, "Trying to use AVX2 on a system that doesn't support it.");
	WriteAVXOp(bits, opPrefix, op, regOp1, regOp2, arg, extrabytes, W);
}

// Like the above, but more general; covers GPR-based VEX operations, like BMI1/2
void XEmitter::WriteVEXOp(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes)
{
	_assert_msg_(size == 32 || size == 64, "VEX GPR instructions only support 32-bit and 64-bit modes!");
	int mmmmm = GetVEXmmmmm(op);
	int pp = GetVEXpp(opPrefix);
	arg.WriteVex(this, regOp1, regOp2, 0, pp, mmmmm, size == 64);
	Write8(op & 0xFF);
	arg.WriteRest(this, extrabytes, regOp1);
}

void XEmitter::WriteBMI1Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes)
{
	CheckFlags();
	_assert_msg_(cpu_info.bBMI1, "Trying to use BMI1 on a system that doesn't support it.");
	_assert_msg_(!arg.IsImm(), "Imm arg unsupported for this BMI1 instruction");
	WriteVEXOp(size, opPrefix, op, regOp1, regOp2, arg, extrabytes);
}

void XEmitter::WriteBMI2Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, OpArg arg, int extrabytes)
{
	CheckFlags();
	_assert_msg_(cpu_info.bBMI2, "Trying to use BMI2 on a system that doesn't support it.");
	_assert_msg_(!arg.IsImm(), "Imm arg unsupported for this BMI2 instruction");
	WriteVEXOp(size, opPrefix, op, regOp1, regOp2, arg, extrabytes);
}

void XEmitter::MOVD_xmm(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x6E, dest, arg, 0);}
void XEmitter::MOVD_xmm(const OpArg &arg, X64Reg src) {WriteSSEOp(0x66, 0x7E, src, arg, 0);}

void XEmitter::MOVQ_xmm(X64Reg dest, OpArg arg)
{
#if PPSSPP_ARCH(AMD64)
		// Alternate encoding
		// This does not display correctly in MSVC's debugger, it thinks it's a MOVD
		arg.operandReg = dest;
		Write8(0x66);
		arg.WriteRex(this, 64, 0);
		Write8(0x0f);
		Write8(0x6E);
		arg.WriteRest(this, 0);
#else
		arg.operandReg = dest;
		Write8(0xF3);
		Write8(0x0f);
		Write8(0x7E);
		arg.WriteRest(this, 0);
#endif
}

void XEmitter::MOVQ_xmm(OpArg arg, X64Reg src)
{
	if (src > 7 || arg.IsSimpleReg())
	{
		// Alternate encoding
		// This does not display correctly in MSVC's debugger, it thinks it's a MOVD
		arg.operandReg = src;
		Write8(0x66);
		arg.WriteRex(this, 64, 0);
		Write8(0x0f);
		Write8(0x7E);
		arg.WriteRest(this, 0);
	}
	else
	{
		arg.operandReg = src;
		Write8(0x66);
		arg.WriteRex(this, 0, 0);
		Write8(0x0f);
		Write8(0xD6);
		arg.WriteRest(this, 0);
	}
}

void XEmitter::WriteMXCSR(OpArg arg, int ext)
{
	if (arg.IsImm() || arg.IsSimpleReg())
		_assert_msg_(false, "MXCSR - invalid operand");

	arg.operandReg = ext;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(0xAE);
	arg.WriteRest(this);
}

void XEmitter::STMXCSR(OpArg memloc) {WriteMXCSR(memloc, 3);}
void XEmitter::LDMXCSR(OpArg memloc) {WriteMXCSR(memloc, 2);}

void XEmitter::MOVNTDQ(OpArg arg, X64Reg regOp) {WriteSSEOp(0x66, sseMOVNTDQ, regOp, arg);}
void XEmitter::MOVNTPS(OpArg arg, X64Reg regOp) {WriteSSEOp(0x00, sseMOVNTP, regOp, arg);}
void XEmitter::MOVNTPD(OpArg arg, X64Reg regOp) {WriteSSEOp(0x66, sseMOVNTP, regOp, arg);}

void XEmitter::ADDSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseADD, regOp, arg);}
void XEmitter::ADDSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseADD, regOp, arg);}
void XEmitter::SUBSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseSUB, regOp, arg);}
void XEmitter::SUBSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseSUB, regOp, arg);}
void XEmitter::CMPSS(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(0xF3, sseCMP, regOp, arg, 1); Write8(compare);}
void XEmitter::CMPSD(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(0xF2, sseCMP, regOp, arg, 1); Write8(compare);}
void XEmitter::MULSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseMUL, regOp, arg);}
void XEmitter::MULSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseMUL, regOp, arg);}
void XEmitter::DIVSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseDIV, regOp, arg);}
void XEmitter::DIVSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseDIV, regOp, arg);}
void XEmitter::MINSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseMIN, regOp, arg);}
void XEmitter::MINSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseMIN, regOp, arg);}
void XEmitter::MAXSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseMAX, regOp, arg);}
void XEmitter::MAXSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseMAX, regOp, arg);}
void XEmitter::SQRTSS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0xF3, sseSQRT, regOp, arg);}
void XEmitter::SQRTSD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0xF2, sseSQRT, regOp, arg);}
void XEmitter::RCPSS(X64Reg regOp, OpArg& arg)  {WriteSSEOp(0xF3, sseRCP, regOp, arg);}
void XEmitter::RSQRTSS(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, sseRSQRT, regOp, arg);}

void XEmitter::ADDPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseADD, regOp, arg);}
void XEmitter::ADDPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseADD, regOp, arg);}
void XEmitter::SUBPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseSUB, regOp, arg);}
void XEmitter::SUBPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseSUB, regOp, arg);}
void XEmitter::CMPPS(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(0x00, sseCMP, regOp, arg, 1); Write8(compare);}
void XEmitter::CMPPD(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(0x66, sseCMP, regOp, arg, 1); Write8(compare);}
void XEmitter::ANDPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseAND, regOp, arg);}
void XEmitter::ANDPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseAND, regOp, arg);}
void XEmitter::ANDNPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x00, sseANDN, regOp, arg);}
void XEmitter::ANDNPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseANDN, regOp, arg);}
void XEmitter::ORPS(X64Reg regOp, OpArg arg)    {WriteSSEOp(0x00, sseOR, regOp, arg);}
void XEmitter::ORPD(X64Reg regOp, OpArg arg)    {WriteSSEOp(0x66, sseOR, regOp, arg);}
void XEmitter::XORPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseXOR, regOp, arg);}
void XEmitter::XORPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseXOR, regOp, arg);}
void XEmitter::MULPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseMUL, regOp, arg);}
void XEmitter::MULPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseMUL, regOp, arg);}
void XEmitter::DIVPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseDIV, regOp, arg);}
void XEmitter::DIVPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseDIV, regOp, arg);}
void XEmitter::MINPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseMIN, regOp, arg);}
void XEmitter::MINPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseMIN, regOp, arg);}
void XEmitter::MAXPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x00, sseMAX, regOp, arg);}
void XEmitter::MAXPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0x66, sseMAX, regOp, arg);}
void XEmitter::SQRTPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x00, sseSQRT, regOp, arg);}
void XEmitter::SQRTPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseSQRT, regOp, arg);}
void XEmitter::RCPPS(X64Reg regOp, OpArg& arg)  {WriteSSEOp(0x00, sseRCP, regOp, arg);}
void XEmitter::RSQRTPS(X64Reg regOp, OpArg arg) {WriteSSEOp(0x00, sseRSQRT, regOp, arg);}
void XEmitter::SHUFPS(X64Reg regOp, OpArg arg, u8 shuffle) {WriteSSEOp(0x00, sseSHUF, regOp, arg,1); Write8(shuffle);}
void XEmitter::SHUFPD(X64Reg regOp, OpArg arg, u8 shuffle) {WriteSSEOp(0x66, sseSHUF, regOp, arg,1); Write8(shuffle);}

void XEmitter::HADDPS(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, sseHADD, regOp, arg);}

void XEmitter::COMISS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x00, sseCOMIS, regOp, arg);} //weird that these should be packed
void XEmitter::COMISD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseCOMIS, regOp, arg);} //ordered
void XEmitter::UCOMISS(X64Reg regOp, OpArg arg) {WriteSSEOp(0x00, sseUCOMIS, regOp, arg);} //unordered
void XEmitter::UCOMISD(X64Reg regOp, OpArg arg) {WriteSSEOp(0x66, sseUCOMIS, regOp, arg);}

void XEmitter::MOVAPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x00, sseMOVAPfromRM, regOp, arg);}
void XEmitter::MOVAPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseMOVAPfromRM, regOp, arg);}
void XEmitter::MOVAPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(0x00, sseMOVAPtoRM, regOp, arg);}
void XEmitter::MOVAPD(OpArg arg, X64Reg regOp)  {WriteSSEOp(0x66, sseMOVAPtoRM, regOp, arg);}

void XEmitter::MOVUPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x00, sseMOVUPfromRM, regOp, arg);}
void XEmitter::MOVUPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseMOVUPfromRM, regOp, arg);}
void XEmitter::MOVUPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(0x00, sseMOVUPtoRM, regOp, arg);}
void XEmitter::MOVUPD(OpArg arg, X64Reg regOp)  {WriteSSEOp(0x66, sseMOVUPtoRM, regOp, arg);}

void XEmitter::MOVDQA(X64Reg regOp, OpArg arg)  {WriteSSEOp(0x66, sseMOVDQfromRM, regOp, arg);}
void XEmitter::MOVDQA(OpArg arg, X64Reg regOp)  {WriteSSEOp(0x66, sseMOVDQtoRM, regOp, arg);}
void XEmitter::MOVDQU(X64Reg regOp, OpArg arg)  {WriteSSEOp(0xF3, sseMOVDQfromRM, regOp, arg);}
void XEmitter::MOVDQU(OpArg arg, X64Reg regOp)  {WriteSSEOp(0xF3, sseMOVDQtoRM, regOp, arg);}

void XEmitter::MOVSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF3, sseMOVUPfromRM, regOp, arg);}
void XEmitter::MOVSD(X64Reg regOp, OpArg arg)   {WriteSSEOp(0xF2, sseMOVUPfromRM, regOp, arg);}
void XEmitter::MOVSS(OpArg arg, X64Reg regOp) {
	// Make Valgrind happy.
	if (arg.IsSimpleReg())
		MOVSS(arg.GetSimpleReg(), R(regOp));
	else
		WriteSSEOp(0xF3, sseMOVUPtoRM, regOp, arg);
}
void XEmitter::MOVSD(OpArg arg, X64Reg regOp) {
	// Make Valgrind happy.
	if (arg.IsSimpleReg())
		MOVSD(arg.GetSimpleReg(), R(regOp));
	else
		WriteSSEOp(0xF2, sseMOVUPtoRM, regOp, arg);
}

void XEmitter::MOVLPS(X64Reg regOp, OpArg arg)  { WriteSSEOp(0x00, sseMOVLPfromRM, regOp, arg); }
void XEmitter::MOVLPD(X64Reg regOp, OpArg arg)  { WriteSSEOp(0x66, sseMOVLPfromRM, regOp, arg); }
void XEmitter::MOVLPS(OpArg arg, X64Reg regOp)  { WriteSSEOp(0x00, sseMOVLPtoRM, regOp, arg); }
void XEmitter::MOVLPD(OpArg arg, X64Reg regOp)  { WriteSSEOp(0x66, sseMOVLPtoRM, regOp, arg); }

void XEmitter::MOVHPS(X64Reg regOp, OpArg arg)  { WriteSSEOp(0x00, sseMOVHPfromRM, regOp, arg); }
void XEmitter::MOVHPD(X64Reg regOp, OpArg arg)  { WriteSSEOp(0x66, sseMOVHPfromRM, regOp, arg); }
void XEmitter::MOVHPS(OpArg arg, X64Reg regOp)  { WriteSSEOp(0x00, sseMOVHPtoRM, regOp, arg); }
void XEmitter::MOVHPD(OpArg arg, X64Reg regOp)  { WriteSSEOp(0x66, sseMOVHPtoRM, regOp, arg); }

void XEmitter::MOVHLPS(X64Reg regOp1, X64Reg regOp2) {WriteSSEOp(0x00, sseMOVHLPS, regOp1, R(regOp2));}
void XEmitter::MOVLHPS(X64Reg regOp1, X64Reg regOp2) {WriteSSEOp(0x00, sseMOVLHPS, regOp1, R(regOp2));}

void XEmitter::CVTPS2PD(X64Reg regOp, OpArg arg) {WriteSSEOp(0x00, 0x5A, regOp, arg);}
void XEmitter::CVTPD2PS(X64Reg regOp, OpArg arg) {WriteSSEOp(0x66, 0x5A, regOp, arg);}

void XEmitter::CVTSD2SS(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, 0x5A, regOp, arg);}
void XEmitter::CVTSS2SD(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0x5A, regOp, arg);}
void XEmitter::CVTSD2SI(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, 0x2D, regOp, arg);}
void XEmitter::CVTSS2SI(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0x2D, regOp, arg);}
void XEmitter::CVTSI2SD(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, 0x2A, regOp, arg);}
void XEmitter::CVTSI2SS(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0x2A, regOp, arg);}

void XEmitter::CVTDQ2PD(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0xE6, regOp, arg);}
void XEmitter::CVTDQ2PS(X64Reg regOp, OpArg arg) {WriteSSEOp(0x00, 0x5B, regOp, arg);}
void XEmitter::CVTPD2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, 0xE6, regOp, arg);}
void XEmitter::CVTPS2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(0x66, 0x5B, regOp, arg);}

void XEmitter::CVTTSD2SI(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF2, 0x2C, regOp, arg);}
void XEmitter::CVTTSS2SI(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0x2C, regOp, arg);}
void XEmitter::CVTTPS2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(0xF3, 0x5B, regOp, arg);}
void XEmitter::CVTTPD2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(0x66, 0xE6, regOp, arg);}

void XEmitter::MASKMOVDQU(X64Reg dest, X64Reg src)  {WriteSSEOp(0x66, sseMASKMOVDQU, dest, R(src));}

void XEmitter::MOVSHDUP(X64Reg regOp, OpArg arg) { WriteSSEOp(0xF3, sseMOVHPfromRM, regOp, arg); }
void XEmitter::MOVSLDUP(X64Reg regOp, OpArg arg) { WriteSSEOp(0xF3, sseMOVLPfromRM, regOp, arg); }

void XEmitter::MOVMSKPS(X64Reg dest, OpArg arg) {WriteSSEOp(0x00, 0x50, dest, arg);}
void XEmitter::MOVMSKPD(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x50, dest, arg);}

void XEmitter::LDDQU(X64Reg dest, OpArg arg)    {WriteSSEOp(0xF2, sseLDDQU, dest, arg);} // For integer data only

void XEmitter::UNPCKLPS(X64Reg dest, OpArg arg) {WriteSSEOp(0x00, 0x14, dest, arg);}
void XEmitter::UNPCKHPS(X64Reg dest, OpArg arg) {WriteSSEOp(0x00, 0x15, dest, arg);}

void XEmitter::UNPCKLPD(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x14, dest, arg);}
void XEmitter::UNPCKHPD(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x15, dest, arg);}

void XEmitter::MOVDDUP(X64Reg regOp, OpArg arg)
{
	if (cpu_info.bSSE3)
	{
		WriteSSEOp(0xF2, 0x12, regOp, arg); //SSE3 movddup
	}
	else
	{
		// Simulate this instruction with SSE2 instructions
		if (!arg.IsSimpleReg(regOp))
			MOVSD(regOp, arg);
		UNPCKLPD(regOp, R(regOp));
	}
}

//There are a few more left

// Also some integer instructions are missing
void XEmitter::PACKSSDW(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x6B, dest, arg);}
void XEmitter::PACKSSWB(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x63, dest, arg);}
void XEmitter::PACKUSWB(X64Reg dest, OpArg arg) {WriteSSEOp(0x66, 0x67, dest, arg);}

void XEmitter::PUNPCKLBW(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x60, dest, arg);}
void XEmitter::PUNPCKLWD(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x61, dest, arg);}
void XEmitter::PUNPCKLDQ(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x62, dest, arg);}
void XEmitter::PUNPCKLQDQ(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x6C, dest, arg);}

void XEmitter::PUNPCKHBW(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x68, dest, arg);}
void XEmitter::PUNPCKHWD(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x69, dest, arg);}
void XEmitter::PUNPCKHDQ(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x6A, dest, arg);}
void XEmitter::PUNPCKHQDQ(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0x6D, dest, arg);}

void XEmitter::PSRLW(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRLW(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x71, (X64Reg)2, R(dest));
	Write8(shift);
}

void XEmitter::PSRLD(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRLD(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x72, (X64Reg)2, R(dest));
	Write8(shift);
}

void XEmitter::PSRLQ(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRLQ(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x73, (X64Reg)2, R(dest));
	Write8(shift);
}

void XEmitter::PSRLDQ(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRLDQ(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x73, (X64Reg)3, R(dest));
	Write8(shift);
}

void XEmitter::PSRLW(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xD1, reg, arg); }
void XEmitter::PSRLD(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xD2, reg, arg); }
void XEmitter::PSRLQ(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xD3, reg, arg); }

void XEmitter::PSLLW(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSLLW(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x71, (X64Reg)6, R(dest));
	Write8(shift);
}

void XEmitter::PSLLD(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSLLD(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x72, (X64Reg)6, R(dest));
	Write8(shift);
}

void XEmitter::PSLLQ(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSLLQ(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x73, (X64Reg)6, R(dest));
	Write8(shift);
}

void XEmitter::PSLLDQ(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSLLDQ(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x73, (X64Reg)7, R(dest));
	Write8(shift);
}

void XEmitter::PSLLW(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xF1, reg, arg); }
void XEmitter::PSLLD(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xF2, reg, arg); }
void XEmitter::PSLLQ(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xF3, reg, arg); }

void XEmitter::PSRAW(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRAW(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x71, (X64Reg)4, R(dest));
	Write8(shift);
}

void XEmitter::PSRAD(X64Reg dest, X64Reg reg, int shift) {
	if (dest != reg) {
		if (cpu_info.bAVX) {
			VPSRAD(128, dest, reg, shift);
			return;
		}
		MOVDQA(dest, R(reg));
	}
	WriteSSEOp(0x66, 0x72, (X64Reg)4, R(dest));
	Write8(shift);
}

void XEmitter::PSRAW(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xE1, reg, arg); }
void XEmitter::PSRAD(X64Reg reg, OpArg arg) { WriteSSEOp(0x66, 0xE2, reg, arg); }

void XEmitter::PMULLW(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0xD5, dest, arg);}
void XEmitter::PMULHW(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0xE5, dest, arg);}
void XEmitter::PMULHUW(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0xE4, dest, arg);}
void XEmitter::PMULUDQ(X64Reg dest, const OpArg &arg) {WriteSSEOp(0x66, 0xF4, dest, arg);}

void XEmitter::PMULLD(X64Reg dest, const OpArg &arg) {WriteSSE41Op(0x66, 0x3840, dest, arg);}
void XEmitter::PMULDQ(X64Reg dest, const OpArg &arg) {WriteSSE41Op(0x66, 0x3828, dest, arg);}

void XEmitter::WriteSSSE3Op(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes)
{
	_assert_msg_(cpu_info.bSSSE3, "Trying to use SSSE3 on a system that doesn't support it.");
	WriteSSEOp(opPrefix, op, regOp, arg, extrabytes);
}

void XEmitter::WriteSSE41Op(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes)
{
	_assert_msg_(cpu_info.bSSE4_1, "Trying to use SSE4.1 on a system that doesn't support it.");
	WriteSSEOp(opPrefix, op, regOp, arg, extrabytes);
}

void XEmitter::PSHUFB(X64Reg dest, OpArg arg)   {WriteSSSE3Op(0x66, 0x3800, dest, arg);}
void XEmitter::PTEST(X64Reg dest, OpArg arg)    {WriteSSE41Op(0x66, 0x3817, dest, arg);}
void XEmitter::PACKUSDW(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x382b, dest, arg);}
void XEmitter::DPPS(X64Reg dest, OpArg arg, u8 mask) {WriteSSE41Op(0x66, 0x3A40, dest, arg, 1); Write8(mask);}

void XEmitter::INSERTPS(X64Reg dest, OpArg arg, u8 dstsubreg, u8 srcsubreg, u8 zmask) { WriteSSE41Op(0x66, 0x3A21, dest, arg, 1); Write8((srcsubreg << 6) | (dstsubreg << 4) | zmask); }
void XEmitter::EXTRACTPS(OpArg dest, X64Reg arg, u8 subreg) { WriteSSE41Op(0x66, 0x3A17, arg, dest, 1); Write8(subreg); }

void XEmitter::PMINSB(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x3838, dest, arg);}
void XEmitter::PMINSD(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x3839, dest, arg);}
void XEmitter::PMINUW(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383a, dest, arg);}
void XEmitter::PMINUD(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383b, dest, arg);}
void XEmitter::PMAXSB(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383c, dest, arg);}
void XEmitter::PMAXSD(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383d, dest, arg);}
void XEmitter::PMAXUW(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383e, dest, arg);}
void XEmitter::PMAXUD(X64Reg dest, OpArg arg)   {WriteSSE41Op(0x66, 0x383f, dest, arg);}

void XEmitter::PMOVSXBW(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3820, dest, arg);}
void XEmitter::PMOVSXBD(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3821, dest, arg);}
void XEmitter::PMOVSXBQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3822, dest, arg);}
void XEmitter::PMOVSXWD(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3823, dest, arg);}
void XEmitter::PMOVSXWQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3824, dest, arg);}
void XEmitter::PMOVSXDQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3825, dest, arg);}
void XEmitter::PMOVZXBW(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3830, dest, arg);}
void XEmitter::PMOVZXBD(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3831, dest, arg);}
void XEmitter::PMOVZXBQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3832, dest, arg);}
void XEmitter::PMOVZXWD(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3833, dest, arg);}
void XEmitter::PMOVZXWQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3834, dest, arg);}
void XEmitter::PMOVZXDQ(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3835, dest, arg);}

void XEmitter::PBLENDVB(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3810, dest, arg);}
void XEmitter::BLENDVPS(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3814, dest, arg);}
void XEmitter::BLENDVPD(X64Reg dest, OpArg arg) {WriteSSE41Op(0x66, 0x3815, dest, arg);}

void XEmitter::PBLENDW(X64Reg dest, OpArg arg, u8 mask) {WriteSSE41Op(0x66, 0x3A0E, dest, arg, 1); Write8(mask);}
void XEmitter::BLENDPS(X64Reg dest, OpArg arg, u8 mask) {WriteSSE41Op(0x66, 0x3A0C, dest, arg, 1); Write8(mask);}
void XEmitter::BLENDPD(X64Reg dest, OpArg arg, u8 mask) {WriteSSE41Op(0x66, 0x3A0D, dest, arg, 1); Write8(mask);}

void XEmitter::ROUNDSS(X64Reg dest, OpArg arg, u8 mode) {WriteSSE41Op(0x66, 0x3A0A, dest, arg, 1); Write8(mode);}
void XEmitter::ROUNDSD(X64Reg dest, OpArg arg, u8 mode) {WriteSSE41Op(0x66, 0x3A0B, dest, arg, 1); Write8(mode);}
void XEmitter::ROUNDPS(X64Reg dest, OpArg arg, u8 mode) {WriteSSE41Op(0x66, 0x3A08, dest, arg, 1); Write8(mode);}
void XEmitter::ROUNDPD(X64Reg dest, OpArg arg, u8 mode) {WriteSSE41Op(0x66, 0x3A09, dest, arg, 1); Write8(mode);}

void XEmitter::PAND(X64Reg dest, OpArg arg)     {WriteSSEOp(0x66, 0xDB, dest, arg);}
void XEmitter::PANDN(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xDF, dest, arg);}
void XEmitter::PXOR(X64Reg dest, OpArg arg)     {WriteSSEOp(0x66, 0xEF, dest, arg);}
void XEmitter::POR(X64Reg dest, OpArg arg)      {WriteSSEOp(0x66, 0xEB, dest, arg);}

void XEmitter::PADDB(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xFC, dest, arg);}
void XEmitter::PADDW(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xFD, dest, arg);}
void XEmitter::PADDD(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xFE, dest, arg);}
void XEmitter::PADDQ(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xD4, dest, arg);}

void XEmitter::PADDSB(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xEC, dest, arg);}
void XEmitter::PADDSW(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xED, dest, arg);}
void XEmitter::PADDUSB(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0xDC, dest, arg);}
void XEmitter::PADDUSW(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0xDD, dest, arg);}

void XEmitter::PSUBB(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xF8, dest, arg);}
void XEmitter::PSUBW(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xF9, dest, arg);}
void XEmitter::PSUBD(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xFA, dest, arg);}
void XEmitter::PSUBQ(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xFB, dest, arg);}

void XEmitter::PSUBSB(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xE8, dest, arg);}
void XEmitter::PSUBSW(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xE9, dest, arg);}
void XEmitter::PSUBUSB(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0xD8, dest, arg);}
void XEmitter::PSUBUSW(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0xD9, dest, arg);}

void XEmitter::PAVGB(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xE0, dest, arg);}
void XEmitter::PAVGW(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xE3, dest, arg);}

void XEmitter::PCMPEQB(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x74, dest, arg);}
void XEmitter::PCMPEQW(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x75, dest, arg);}
void XEmitter::PCMPEQD(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x76, dest, arg);}

void XEmitter::PCMPGTB(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x64, dest, arg);}
void XEmitter::PCMPGTW(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x65, dest, arg);}
void XEmitter::PCMPGTD(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0x66, dest, arg);}

void XEmitter::PEXTRW(X64Reg dest, X64Reg arg, u8 subreg)   {WriteSSEOp(0x66, 0xC5, dest, R(arg), 1); Write8(subreg);}
void XEmitter::PINSRW(X64Reg dest, OpArg arg, u8 subreg)    {WriteSSEOp(0x66, 0xC4, dest, arg, 1); Write8(subreg);}
void XEmitter::PEXTRB(OpArg dest, X64Reg arg, u8 subreg)    {WriteSSE41Op(0x66, 0x3A14, arg, dest, 1); Write8(subreg);}
void XEmitter::PEXTRW(OpArg dest, X64Reg arg, u8 subreg)    {WriteSSE41Op(0x66, 0x3A15, arg, dest, 1); Write8(subreg);}
void XEmitter::PEXTRD(OpArg dest, X64Reg arg, u8 subreg)    {WriteSSE41Op(0x66, 0x3A16, arg, dest, 1); Write8(subreg);}
void XEmitter::PEXTRQ(OpArg dest, X64Reg arg, u8 subreg) {
	_assert_msg_(cpu_info.bSSE4_1, "Trying to use SSE4.1 on a system that doesn't support it.");
	Write8(0x66);
	dest.operandReg = arg;
	dest.WriteRex(this, 64, 0);
	Write8(0x0F);
	Write8(0x3A);
	Write8(0x16);
	dest.WriteRest(this, 1);
	Write8(subreg);
}
void XEmitter::PINSRB(X64Reg dest, OpArg arg, u8 subreg)    {WriteSSE41Op(0x66, 0x3A20, dest, arg, 1); Write8(subreg);}
void XEmitter::PINSRD(X64Reg dest, OpArg arg, u8 subreg)    {WriteSSE41Op(0x66, 0x3A22, dest, arg, 1); Write8(subreg);}
void XEmitter::PINSRQ(X64Reg dest, OpArg arg, u8 subreg) {
	_assert_msg_(cpu_info.bSSE4_1, "Trying to use SSE4.1 on a system that doesn't support it.");
	Write8(0x66);
	arg.operandReg = dest;
	arg.WriteRex(this, 64, 0);
	Write8(0x0F);
	Write8(0x3A);
	Write8(0x22);
	arg.WriteRest(this, 1);
	Write8(subreg);
}

void XEmitter::PMADDWD(X64Reg dest, OpArg arg)  {WriteSSEOp(0x66, 0xF5, dest, arg); }
void XEmitter::PMADDUBSW(X64Reg dest, OpArg arg) {WriteSSSE3Op(0x66, 0x3804, dest, arg);}
void XEmitter::PSADBW(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xF6, dest, arg);}

void XEmitter::PMAXSW(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xEE, dest, arg); }
void XEmitter::PMAXUB(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xDE, dest, arg); }
void XEmitter::PMINSW(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xEA, dest, arg); }
void XEmitter::PMINUB(X64Reg dest, OpArg arg)   {WriteSSEOp(0x66, 0xDA, dest, arg); }

void XEmitter::PMOVMSKB(X64Reg dest, OpArg arg)    {WriteSSEOp(0x66, 0xD7, dest, arg); }
void XEmitter::PSHUFD(X64Reg regOp, OpArg arg, u8 shuffle)    {WriteSSEOp(0x66, 0x70, regOp, arg, 1); Write8(shuffle);}
void XEmitter::PSHUFLW(X64Reg regOp, OpArg arg, u8 shuffle)   {WriteSSEOp(0xF2, 0x70, regOp, arg, 1); Write8(shuffle);}
void XEmitter::PSHUFHW(X64Reg regOp, OpArg arg, u8 shuffle)   {WriteSSEOp(0xF3, 0x70, regOp, arg, 1); Write8(shuffle);}

// VEX
void XEmitter::VADDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseADD, regOp1, regOp2, arg); }
void XEmitter::VADDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseADD, regOp1, regOp2, arg); }
void XEmitter::VADDSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseADD, regOp1, regOp2, arg); }
void XEmitter::VADDSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseADD, regOp1, regOp2, arg); }
void XEmitter::VADDSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0xF2, 0xD0, regOp1, regOp2, arg); }
void XEmitter::VADDSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0xD0, regOp1, regOp2, arg); }
void XEmitter::VCMPPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare) { WriteAVXOp(bits, 0x00, sseCMP, regOp1, regOp2, arg, 1); Write8(compare); }
void XEmitter::VCMPPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare) { WriteAVXOp(bits, 0x66, sseCMP, regOp1, regOp2, arg, 1); Write8(compare); }
void XEmitter::VCMPSS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare) { WriteAVXOp(0, 0xF3, sseCMP, regOp1, regOp2, arg, 1); Write8(compare); }
void XEmitter::VCMPSD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 compare) { WriteAVXOp(0, 0xF2, sseCMP, regOp1, regOp2, arg, 1); Write8(compare); }
void XEmitter::VCOMISS(X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0x00, sseCOMIS, regOp1, arg); }
void XEmitter::VCOMISD(X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0x66, sseCOMIS, regOp1, arg); }
void XEmitter::VDIVPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseDIV, regOp1, regOp2, arg); }
void XEmitter::VDIVPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseDIV, regOp1, regOp2, arg); }
void XEmitter::VDIVSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseDIV, regOp1, regOp2, arg); }
void XEmitter::VDIVSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseDIV, regOp1, regOp2, arg); }
void XEmitter::VDPPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVXOp(bits, 0x66, 0x3A40, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VDPPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) {
	_assert_msg_(bits == 0 || bits == 128, "DPPD doesn't support 256 bit");
	WriteAVXOp(bits, 0x66, 0x3A41, regOp1, regOp2, arg, 1);
	Write8(mask);
}
void XEmitter::VHADDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0xF2, sseHADD, regOp1, regOp2, arg); }
void XEmitter::VHADDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseHADD, regOp1, regOp2, arg); }
void XEmitter::VHSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0xF2, 0x7D, regOp1, regOp2, arg); }
void XEmitter::VHSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x7D, regOp1, regOp2, arg); }
void XEmitter::VMAXPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseMAX, regOp1, regOp2, arg); }
void XEmitter::VMAXPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseMAX, regOp1, regOp2, arg); }
void XEmitter::VMAXSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseMAX, regOp1, regOp2, arg); }
void XEmitter::VMAXSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseMAX, regOp1, regOp2, arg); }
void XEmitter::VMINPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseMIN, regOp1, regOp2, arg); }
void XEmitter::VMINPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseMIN, regOp1, regOp2, arg); }
void XEmitter::VMINSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseMIN, regOp1, regOp2, arg); }
void XEmitter::VMINSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseMIN, regOp1, regOp2, arg); }
void XEmitter::VMULPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseMUL, regOp1, regOp2, arg); }
void XEmitter::VMULPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseMUL, regOp1, regOp2, arg); }
void XEmitter::VMULSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseMUL, regOp1, regOp2, arg); }
void XEmitter::VMULSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseMUL, regOp1, regOp2, arg); }
void XEmitter::VRCPPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, sseRCP, regOp1, arg); }
void XEmitter::VRCPSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseRCP, regOp1, regOp2, arg); }
void XEmitter::VRSQRTPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, sseRSQRT, regOp1, arg); }
void XEmitter::VRSQRTSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseRSQRT, regOp1, regOp2, arg); }
void XEmitter::VSQRTPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, sseSQRT, regOp1, arg); }
void XEmitter::VSQRTPD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, sseSQRT, regOp1, arg); }
void XEmitter::VSQRTSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseSQRT, regOp1, regOp2, arg); }
void XEmitter::VSQRTSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseSQRT, regOp1, regOp2, arg); }
void XEmitter::VSUBPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseSUB, regOp1, regOp2, arg); }
void XEmitter::VSUBPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseSUB, regOp1, regOp2, arg); }
void XEmitter::VSUBSS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, sseSUB, regOp1, regOp2, arg); }
void XEmitter::VSUBSD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, sseSUB, regOp1, regOp2, arg); }
void XEmitter::VUCOMISS(X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0x00, sseUCOMIS, regOp1, arg); }
void XEmitter::VUCOMISD(X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0x66, sseUCOMIS, regOp1, arg); }

void XEmitter::VBLENDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVXOp(bits, 0x66, 0x3A0C, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VBLENDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVXOp(bits, 0x66, 0x3A0D, regOp1, regOp2, arg, 1); Write8(mask); }
// Note: doesn't match non-VEX.
void XEmitter::VBLENDVPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg mask) { WriteAVXOp(bits, 0x66, 0x3A4A, regOp1, regOp2, arg, 1); Write8(mask << 4); }
// Note: doesn't match non-VEX.
void XEmitter::VBLENDVPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg mask) { WriteAVXOp(bits, 0x66, 0x3A4B, regOp1, regOp2, arg, 1); Write8(mask << 4); }
void XEmitter::VCVTDQ2PS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, 0x5B, regOp1, arg); }
void XEmitter::VCVTDQ2PD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF3, 0xE6, regOp1, arg); }
void XEmitter::VCVTPS2DQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x5B, regOp1, arg); }
void XEmitter::VCVTPD2DQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF2, 0xE6, regOp1, arg); }
void XEmitter::VCVTPS2PD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, 0x5A, regOp1, arg); }
void XEmitter::VCVTPD2PS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x5A, regOp1, arg); }
void XEmitter::VCVTSS2SI(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0xF3, 0x2D, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VCVTSS2SD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, 0x5A, regOp1, regOp2, arg); }
void XEmitter::VCVTSD2SI(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0xF2, 0x2D, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VCVTSD2SS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, 0x5A, regOp1, regOp2, arg); }
void XEmitter::VCVTSI2SS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF3, 0x2A, regOp1, regOp2, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VCVTSI2SD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(0, 0xF2, 0x2A, regOp1, regOp2, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VCVTTPS2DQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF3, 0x5B, regOp1, arg); }
void XEmitter::VCVTTPD2DQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0xE6, regOp1, arg); }
void XEmitter::VCVTTSS2SI(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0xF3, 0x2C, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VCVTTSD2SI(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0xF2, 0x2C, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VEXTRACTPS(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A17, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VINSERTPS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 dstsubreg, u8 srcsubreg, u8 zmask) { WriteAVXOp(0, 0x66, 0x3A21, regOp1, regOp2, arg, 1); Write8((srcsubreg << 6) | (dstsubreg << 4) | zmask); }
void XEmitter::VLDDQU(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF2, sseLDDQU, regOp1, arg); }
void XEmitter::VMOVAPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, sseMOVAPfromRM, regOp1, arg); }
void XEmitter::VMOVAPD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, sseMOVAPfromRM, regOp1, arg); }
void XEmitter::VMOVAPS(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x00, sseMOVAPtoRM, regOp1, arg); }
void XEmitter::VMOVAPD(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, sseMOVAPtoRM, regOp1, arg); }
void XEmitter::VMOVD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x6E, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VMOVD(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, 0x7E, regOp1, arg, 0, bits == 64 ? 1 : 0); }
void XEmitter::VMOVD(X64Reg regOp1, OpArg arg) { VMOVD(32, regOp1, arg); }
void XEmitter::VMOVD(OpArg arg, X64Reg regOp1) { VMOVD(32, arg, regOp1); }
void XEmitter::VMOVQ(X64Reg regOp1, OpArg arg) { VMOVD(64, regOp1, arg); }
void XEmitter::VMOVQ(OpArg arg, X64Reg regOp1) { VMOVD(64, arg, regOp1); }
void XEmitter::VMOVDDUP(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF2, 0x12, regOp1, arg); }
void XEmitter::VMOVDQA(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, sseMOVDQfromRM, regOp1, arg); }
void XEmitter::VMOVDQA(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, sseMOVDQtoRM, regOp1, arg); }
void XEmitter::VMOVDQU(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF3, sseMOVDQfromRM, regOp1, arg); }
void XEmitter::VMOVDQU(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0xF3, sseMOVDQtoRM, regOp1, arg); }
void XEmitter::VMOVHLPS(X64Reg regOp1, X64Reg regOp2, X64Reg arg) { WriteAVXOp(0, 0x00, sseMOVHLPS, regOp1, regOp2, R(arg)); }
void XEmitter::VMOVLHPS(X64Reg regOp1, X64Reg regOp2, X64Reg arg) { WriteAVXOp(0, 0x00, sseMOVLHPS, regOp1, regOp2, R(arg)); }
void XEmitter::VMOVHPS(X64Reg regOp1, X64Reg regOp2, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVHPS cannot be used for registers");
	WriteAVXOp(0, 0x00, sseMOVHPfromRM, regOp1, regOp2, arg);
}
void XEmitter::VMOVHPS(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVHPS cannot be used for registers");
	WriteAVXOp(0, 0x00, sseMOVHPtoRM, regOp1, arg);
}
void XEmitter::VMOVHPD(X64Reg regOp1, X64Reg regOp2, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVHPD cannot be used for registers");
	WriteAVXOp(0, 0x66, sseMOVHPfromRM, regOp1, regOp2, arg);
}
void XEmitter::VMOVHPD(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVHPD cannot be used for registers");
	WriteAVXOp(0, 0x66, sseMOVHPtoRM, regOp1, arg);
}
void XEmitter::VMOVLPS(X64Reg regOp1, X64Reg regOp2, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVLPS cannot be used for registers");
	WriteAVXOp(0, 0x00, sseMOVLPfromRM, regOp1, regOp2, arg);
}
void XEmitter::VMOVLPS(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVLPS cannot be used for registers");
	WriteAVXOp(0, 0x00, sseMOVLPtoRM, regOp1, arg);
}
void XEmitter::VMOVLPD(X64Reg regOp1, X64Reg regOp2, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVLPD cannot be used for registers");
	WriteAVXOp(0, 0x66, sseMOVLPfromRM, regOp1, regOp2, arg);
}
void XEmitter::VMOVLPD(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVLPD cannot be used for registers");
	WriteAVXOp(0, 0x66, sseMOVLPtoRM, regOp1, arg);
}
void XEmitter::VMOVMSKPS(int bits, X64Reg genReg, X64Reg xmmReg) { WriteAVXOp(bits, 0x00, 0x50, genReg, R(xmmReg)); }
void XEmitter::VMOVMSKPD(int bits, X64Reg genReg, X64Reg xmmReg) { WriteAVXOp(bits, 0x66, 0x50, genReg, R(xmmReg)); }
void XEmitter::VMOVNTDQ(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, sseMOVNTDQ, regOp1, arg); }
void XEmitter::VMOVNTPS(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x00, sseMOVNTP, regOp1, arg); }
void XEmitter::VMOVNTPD(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, sseMOVNTP, regOp1, arg); }
void XEmitter::VMOVQ(X64Reg regOp1, X64Reg arg) { WriteAVXOp(0, 0xF3, 0x7E, regOp1, R(arg)); }
void XEmitter::VMOVSS(X64Reg regOp1, X64Reg regOp2, X64Reg arg) { WriteAVXOp(0, 0xF3, sseMOVUPfromRM, regOp1, regOp2, R(arg)); }
void XEmitter::VMOVSS(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVSS requires three registers, or register and memory");
	WriteAVXOp(0, 0xF3, sseMOVUPtoRM, regOp1, arg);
}
void XEmitter::VMOVSS(X64Reg regOp1, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVSS requires three registers, or register and memory");
	WriteAVXOp(0, 0xF3, sseMOVUPfromRM, regOp1, arg);
}
void XEmitter::VMOVSD(X64Reg regOp1, X64Reg regOp2, X64Reg arg) { WriteAVXOp(0, 0xF2, sseMOVUPfromRM, regOp1, regOp2, R(arg)); }
void XEmitter::VMOVSD(OpArg arg, X64Reg regOp1) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVSD requires three registers, or register and memory");
	WriteAVXOp(0, 0xF2, sseMOVUPtoRM, regOp1, arg);
}
void XEmitter::VMOVSD(X64Reg regOp1, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VMOVSD requires three registers, or register and memory");
	WriteAVXOp(0, 0xF2, sseMOVUPfromRM, regOp1, arg);
}
void XEmitter::VMOVSHDUP(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF3, sseMOVHPfromRM, regOp1, arg); }
void XEmitter::VMOVSLDUP(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0xF3, sseMOVLPfromRM, regOp1, arg); }
void XEmitter::VMOVUPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x00, sseMOVUPfromRM, regOp1, arg); }
void XEmitter::VMOVUPD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, sseMOVUPfromRM, regOp1, arg); }
void XEmitter::VMOVUPS(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x00, sseMOVUPtoRM, regOp1, arg); }
void XEmitter::VMOVUPD(int bits, OpArg arg, X64Reg regOp1) { WriteAVXOp(bits, 0x66, sseMOVUPtoRM, regOp1, arg); }
void XEmitter::VROUNDPS(int bits, X64Reg dest, OpArg arg, u8 mode) { WriteAVXOp(bits, 0x66, 0x3A08, dest, arg, 1); Write8(mode); }
void XEmitter::VROUNDPD(int bits, X64Reg dest, OpArg arg, u8 mode) { WriteAVXOp(bits, 0x66, 0x3A09, dest, arg, 1); Write8(mode); }
void XEmitter::VROUNDSS(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mode) { WriteAVXOp(0, 0x66, 0x3A0A, regOp1, regOp2, arg, 1); Write8(mode); }
void XEmitter::VROUNDSD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mode) { WriteAVXOp(0, 0x66, 0x3A0B, regOp1, regOp2, arg, 1); Write8(mode); }
void XEmitter::VSHUFPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shuffle) { WriteAVXOp(bits, 0x00, sseSHUF, regOp1, regOp2, arg, 1); Write8(shuffle); }
void XEmitter::VSHUFPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shuffle) { WriteAVXOp(bits, 0x66, sseSHUF, regOp1, regOp2, arg, 1); Write8(shuffle); }
void XEmitter::VUNPCKHPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, 0x15, regOp1, regOp2, arg); }
void XEmitter::VUNPCKHPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x15, regOp1, regOp2, arg); }
void XEmitter::VUNPCKLPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, 0x14, regOp1, regOp2, arg); }
void XEmitter::VUNPCKLPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x14, regOp1, regOp2, arg); }

void XEmitter::VANDPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseAND, regOp1, regOp2, arg); }
void XEmitter::VANDPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseAND, regOp1, regOp2, arg); }
void XEmitter::VANDNPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseANDN, regOp1, regOp2, arg); }
void XEmitter::VANDNPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseANDN, regOp1, regOp2, arg); }
void XEmitter::VORPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseOR, regOp1, regOp2, arg); }
void XEmitter::VORPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseOR, regOp1, regOp2, arg); }
void XEmitter::VXORPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x00, sseXOR, regOp1, regOp2, arg); }
void XEmitter::VXORPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, sseXOR, regOp1, regOp2, arg); }

void XEmitter::VPTEST(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3817, regOp1, arg); }

void XEmitter::VMOVNTDQA(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x382A, regOp1, arg); }
void XEmitter::VPACKSSWB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x63, regOp1, regOp2, arg); }
void XEmitter::VPACKSSDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x6B, regOp1, regOp2, arg); }
void XEmitter::VPACKUSWB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x67, regOp1, regOp2, arg); }
void XEmitter::VPACKUSDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x382B, regOp1, regOp2, arg); }
void XEmitter::VPALIGNR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x3A0F, regOp1, regOp2, arg, 1); Write8(shift); }
// Note: doesn't match non-VEX.
void XEmitter::VPBLENDVB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, X64Reg maskReg) { WriteAVX12Op(bits, 0x66, 0x3A4C, regOp1, regOp2, arg, 1); Write8(maskReg << 4); }
void XEmitter::VPBLENDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVX12Op(bits, 0x66, 0x3A0E, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VPEXTRB(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A14, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VPEXTRW(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A15, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VPEXTRD(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A16, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VPEXTRQ(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A16, regOp1, arg, 1, 1); Write8(subreg); }
void XEmitter::VPINSRB(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A20, regOp1, regOp2, arg, 1); Write8(subreg); }
void XEmitter::VPINSRW(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVXOp(0, 0x66, 0xC4, regOp1, regOp2, arg, 1); Write8(subreg); }
void XEmitter::VPINSRD(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A22, regOp1, regOp2, arg, 1); Write8(subreg); }
void XEmitter::VPINSRQ(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVXOp(0, 0x66, 0x3A22, regOp1, regOp2, arg, 1, 1); Write8(subreg); }
void XEmitter::VPMOVMSKB(int bits, X64Reg genReg, X64Reg arg) { WriteAVX12Op(bits, 0x66, 0xD7, genReg, R(arg)); }
void XEmitter::VPMOVSXBW(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3820, regOp1, arg); }
void XEmitter::VPMOVSXBD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3821, regOp1, arg); }
void XEmitter::VPMOVSXBQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3822, regOp1, arg); }
void XEmitter::VPMOVSXWD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3823, regOp1, arg); }
void XEmitter::VPMOVSXWQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3824, regOp1, arg); }
void XEmitter::VPMOVSXDQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3825, regOp1, arg); }
void XEmitter::VPMOVZXBW(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3830, regOp1, arg); }
void XEmitter::VPMOVZXBD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3831, regOp1, arg); }
void XEmitter::VPMOVZXBQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3832, regOp1, arg); }
void XEmitter::VPMOVZXWD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3833, regOp1, arg); }
void XEmitter::VPMOVZXWQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3834, regOp1, arg); }
void XEmitter::VPMOVZXDQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3835, regOp1, arg); }
void XEmitter::VPSHUFB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3800, regOp1, regOp2, arg); }
void XEmitter::VPSHUFD(int bits, X64Reg regOp1, OpArg arg, u8 shuffle) { WriteAVX12Op(bits, 0x66, 0x70, regOp1, arg, 1); Write8(shuffle); }
void XEmitter::VPSHUFHW(int bits, X64Reg regOp1, OpArg arg, u8 shuffle) { WriteAVX12Op(bits, 0xF3, 0x70, regOp1, arg, 1); Write8(shuffle); }
void XEmitter::VPSHUFLW(int bits, X64Reg regOp1, OpArg arg, u8 shuffle) { WriteAVX12Op(bits, 0xF2, 0x70, regOp1, arg, 1); Write8(shuffle); }
void XEmitter::VPUNPCKHBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x68, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKHWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x69, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKHDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x6A, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKHQDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x6D, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKLBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x60, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKLWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x61, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKLDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x62, regOp1, regOp2, arg); }
void XEmitter::VPUNPCKLQDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x6C, regOp1, regOp2, arg); }

void XEmitter::VPABSB(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x381C, regOp1, arg); }
void XEmitter::VPABSW(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x381D, regOp1, arg); }
void XEmitter::VPABSD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x381E, regOp1, arg); }
void XEmitter::VPADDB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xFC, regOp1, regOp2, arg); }
void XEmitter::VPADDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xFD, regOp1, regOp2, arg); }
void XEmitter::VPADDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xFE, regOp1, regOp2, arg); }
void XEmitter::VPADDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD4, regOp1, regOp2, arg); }
void XEmitter::VPADDSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xEC, regOp1, regOp2, arg); }
void XEmitter::VPADDSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xED, regOp1, regOp2, arg); }
void XEmitter::VPADDUSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDC, regOp1, regOp2, arg); }
void XEmitter::VPADDUSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDD, regOp1, regOp2, arg); }
void XEmitter::VPAVGB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE0, regOp1, regOp2, arg); }
void XEmitter::VPAVGW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE3, regOp1, regOp2, arg); }
void XEmitter::VPCMPEQB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x74, regOp1, regOp2, arg); }
void XEmitter::VPCMPEQW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x75, regOp1, regOp2, arg); }
void XEmitter::VPCMPEQD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x76, regOp1, regOp2, arg); }
void XEmitter::VPCMPEQQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3829, regOp1, regOp2, arg); }
void XEmitter::VPCMPGTB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x64, regOp1, regOp2, arg); }
void XEmitter::VPCMPGTW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x65, regOp1, regOp2, arg); }
void XEmitter::VPCMPGTD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x66, regOp1, regOp2, arg); }
void XEmitter::VPCMPGTQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3837, regOp1, regOp2, arg); }
void XEmitter::VPHADDW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3801, regOp1, regOp2, arg); }
void XEmitter::VPHADDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3802, regOp1, regOp2, arg); }
void XEmitter::VPHADDSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3803, regOp1, regOp2, arg); }
void XEmitter::VHMINPOSUW(X64Reg regOp1, OpArg arg) { WriteAVXOp(0, 0x66, 0x3841, regOp1, arg); }
void XEmitter::VPHSUBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3805, regOp1, regOp2, arg); }
void XEmitter::VPHSUBD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3806, regOp1, regOp2, arg); }
void XEmitter::VPHSUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3807, regOp1, regOp2, arg); }
void XEmitter::VPMADDUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3804, regOp1, regOp2, arg); }
void XEmitter::VPMADDWD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF5, regOp1, regOp2, arg); }
void XEmitter::VPMAXSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383C, regOp1, regOp2, arg); }
void XEmitter::VPMAXSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xEE, regOp1, regOp2, arg); }
void XEmitter::VPMAXSD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383D, regOp1, regOp2, arg); }
void XEmitter::VPMAXUB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDE, regOp1, regOp2, arg); }
void XEmitter::VPMAXUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383E, regOp1, regOp2, arg); }
void XEmitter::VPMAXUD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383F, regOp1, regOp2, arg); }
void XEmitter::VPMINSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3838, regOp1, regOp2, arg); }
void XEmitter::VPMINSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xEA, regOp1, regOp2, arg); }
void XEmitter::VPMINSD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3839, regOp1, regOp2, arg); }
void XEmitter::VPMINUB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDA, regOp1, regOp2, arg); }
void XEmitter::VPMINUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383A, regOp1, regOp2, arg); }
void XEmitter::VPMINUD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x383B, regOp1, regOp2, arg); }
void XEmitter::VPMULDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3828, regOp1, regOp2, arg); }
void XEmitter::VPMULHRS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x380B, regOp1, regOp2, arg); }
void XEmitter::VPMULHUW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE4, regOp1, regOp2, arg); }
void XEmitter::VPMULHW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE5, regOp1, regOp2, arg); }
void XEmitter::VPMULLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3840, regOp1, regOp2, arg); }
void XEmitter::VPMULLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD5, regOp1, regOp2, arg); }
void XEmitter::VPMULUDQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF4, regOp1, regOp2, arg); }
void XEmitter::VPSADBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF6, regOp1, regOp2, arg); }
void XEmitter::VPSIGNB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3808, regOp1, regOp2, arg); }
void XEmitter::VPSIGNW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x3809, regOp1, regOp2, arg); }
void XEmitter::VPSIGND(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0x380A, regOp1, regOp2, arg); }
void XEmitter::VPSUBB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF8, regOp1, regOp2, arg); }
void XEmitter::VPSUBW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF9, regOp1, regOp2, arg); }
void XEmitter::VPSUBD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xFA, regOp1, regOp2, arg); }
void XEmitter::VPSUBQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xFB, regOp1, regOp2, arg); }
void XEmitter::VPSUBSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE8, regOp1, regOp2, arg); }
void XEmitter::VPSUBSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE9, regOp1, regOp2, arg); }
void XEmitter::VPSUBUSB(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD8, regOp1, regOp2, arg); }
void XEmitter::VPSUBUSW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD9, regOp1, regOp2, arg); }

void XEmitter::VPAND(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDB, regOp1, regOp2, arg); }
void XEmitter::VPANDN(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xDF, regOp1, regOp2, arg); }
void XEmitter::VPOR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xEB, regOp1, regOp2, arg); }
void XEmitter::VPSLLDQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x73, (X64Reg)7, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSLLW(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x71, (X64Reg)6, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSLLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF1, regOp1, regOp2, arg); }
void XEmitter::VPSLLD(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x72, (X64Reg)6, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSLLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF2, regOp1, regOp2, arg); }
void XEmitter::VPSLLQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x73, (X64Reg)6, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSLLQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xF3, regOp1, regOp2, arg); }
void XEmitter::VPSRAW(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x71, (X64Reg)4, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRAW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE1, regOp1, regOp2, arg); }
void XEmitter::VPSRAD(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x72, (X64Reg)4, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRAD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xE2, regOp1, regOp2, arg); }
void XEmitter::VPSRLDQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x73, (X64Reg)3, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRLW(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x71, (X64Reg)2, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRLW(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD1, regOp1, regOp2, arg); }
void XEmitter::VPSRLD(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x72, (X64Reg)2, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRLD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD2, regOp1, regOp2, arg); }
void XEmitter::VPSRLQ(int bits, X64Reg regOp2, X64Reg arg, u8 shift) { WriteAVX12Op(bits, 0x66, 0x73, (X64Reg)2, regOp2, R(arg), 1); Write8(shift); }
void XEmitter::VPSRLQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xD3, regOp1, regOp2, arg); }
void XEmitter::VPXOR(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX12Op(bits, 0x66, 0xEF, regOp1, regOp2, arg); }

void XEmitter::VBROADCASTSS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3818, regOp1, arg); }
void XEmitter::VBROADCASTSD(X64Reg regOp1, OpArg arg) { WriteAVXOp(256, 0x66, 0x3819, regOp1, arg); }
void XEmitter::VBROADCASTF128(X64Reg regOp1, OpArg arg) { WriteAVXOp(256, 0x66, 0x381A, regOp1, arg); }
void XEmitter::VEXTRACTF128(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVXOp(256, 0x66, 0x3A19, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VINSERTF128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVXOp(256, 0x66, 0x3A18, regOp1, regOp2, arg, 1); Write8(subreg); }
void XEmitter::VPERM2F128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVXOp(256, 0x66, 0x3A06, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VPERMILPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x380C, regOp1, regOp2, arg); }
void XEmitter::VPERMILPS(int bits, X64Reg regOp1, OpArg arg, u8 mask) { WriteAVXOp(bits, 0x66, 0x3A04, regOp1, arg, 1); Write8(mask); }
void XEmitter::VPERMILPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x380D, regOp1, regOp2, arg); }
void XEmitter::VPERMILPD(int bits, X64Reg regOp1, OpArg arg, u8 mask) { WriteAVXOp(bits, 0x66, 0x3A05, regOp1, arg, 1); Write8(mask); }

void XEmitter::VMASKMOVPS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x382C, regOp1, regOp2, arg); }
void XEmitter::VMASKMOVPS(int bits, OpArg arg, X64Reg regOp1, X64Reg regOp2) { WriteAVXOp(bits, 0x66, 0x382E, regOp1, regOp2, arg); }
void XEmitter::VMASKMOVPD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x382D, regOp1, regOp2, arg); }
void XEmitter::VMASKMOVPD(int bits, OpArg arg, X64Reg regOp1, X64Reg regOp2) { WriteAVXOp(bits, 0x66, 0x382F, regOp1, regOp2, arg); }

void XEmitter::VTESTPS(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x380E, regOp1, arg); }
void XEmitter::VTESTPD(int bits, X64Reg regOp1, OpArg arg) { WriteAVXOp(bits, 0x66, 0x380F, regOp1, arg); }

void XEmitter::VZEROALL() {
	_assert_msg_(cpu_info.bAVX, "Trying to use AVX on a system that doesn't support it.");
	R(INVALID_REG).WriteVex(this, INVALID_REG, INVALID_REG, 1, 0, 1, 0);
	Write8(0x77);
}
void XEmitter::VZEROUPPER() {
	_assert_msg_(cpu_info.bAVX, "Trying to use AVX on a system that doesn't support it.");
	R(INVALID_REG).WriteVex(this, INVALID_REG, INVALID_REG, 0, 0, 1, 0);
	Write8(0x77);
}

void XEmitter::VEXTRACTI128(OpArg arg, X64Reg regOp1, u8 subreg) { WriteAVX2Op(256, 0x66, 0x3A39, regOp1, arg, 1); Write8(subreg); }
void XEmitter::VINSERTI128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 subreg) { WriteAVX2Op(256, 0x66, 0x3A38, regOp1, regOp2, arg, 1); Write8(subreg); }
void XEmitter::VPBLENDD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVX2Op(bits, 0x66, 0x3A02, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VPBROADCASTB(int bits, X64Reg regOp1, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3878, regOp1, arg); }
void XEmitter::VPBROADCASTW(int bits, X64Reg regOp1, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3879, regOp1, arg); }
void XEmitter::VPBROADCASTD(int bits, X64Reg regOp1, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3858, regOp1, arg); }
void XEmitter::VPBROADCASTQ(int bits, X64Reg regOp1, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3859, regOp1, arg); }
void XEmitter::VBROADCASTI128(X64Reg regOp1, OpArg arg) {
	_assert_msg_(!arg.IsSimpleReg(), "VBROADCASTI128 must come from memory");
	WriteAVX2Op(256, 0x66, 0x385A, regOp1, arg);
}
void XEmitter::VPERM2I128(X64Reg regOp1, X64Reg regOp2, OpArg arg, u8 mask) { WriteAVX2Op(256, 0x66, 0x3A46, regOp1, regOp2, arg, 1); Write8(mask); }
void XEmitter::VPERMD(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(256, 0x66, 0x3836, regOp1, regOp2, arg); }
void XEmitter::VPERMPS(X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(256, 0x66, 0x3816, regOp1, regOp2, arg); }
void XEmitter::VPERMPD(X64Reg regOp1, OpArg arg, u8 shuffle) { WriteAVX2Op(256, 0x66, 0x3A01, regOp1, arg, 1, 1); Write8(shuffle); }
void XEmitter::VPERMQ(X64Reg regOp1, OpArg arg, u8 shuffle) { WriteAVX2Op(256, 0x66, 0x3A00, regOp1, arg, 1, 1); Write8(shuffle); }

void XEmitter::VPMASKMOVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x388C, regOp1, regOp2, arg); }
void XEmitter::VPMASKMOVD(int bits, OpArg arg, X64Reg regOp1, X64Reg regOp2) { WriteAVX2Op(bits, 0x66, 0x388E, regOp1, regOp2, arg); }
void XEmitter::VPMASKMOVQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x388C, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VPMASKMOVQ(int bits, OpArg arg, X64Reg regOp1, X64Reg regOp2) { WriteAVX2Op(bits, 0x66, 0x388E, regOp1, regOp2, arg, 0, 1); }

void XEmitter::VGATHERDPS(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3892, regOp1, regOp2, arg);
}
void XEmitter::VGATHERDPD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3893, regOp1, regOp2, arg);
}
void XEmitter::VGATHERQPS(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3892, regOp1, regOp2, arg);
}
void XEmitter::VGATHERQPD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3893, regOp1, regOp2, arg);
}
void XEmitter::VPGATHERDD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VPGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3890, regOp1, regOp2, arg);
}
void XEmitter::VPGATHERQD(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VPGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3891, regOp1, regOp2, arg);
}
void XEmitter::VPGATHERDQ(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VPGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3890, regOp1, regOp2, arg, 0, 1);
}
void XEmitter::VPGATHERQQ(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {
	_assert_msg_(regOp1 != regOp2 && !arg.IsIndexedReg(regOp1) && !arg.IsIndexedReg(regOp2), "VPGATHER cannot have overlapped registers");
	WriteAVX2Op(bits, 0x66, 0x3891, regOp1, regOp2, arg, 0, 1);
}

void XEmitter::VPSLLVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3847, regOp1, regOp2, arg); }
void XEmitter::VPSLLVQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3847, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VPSRAVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3846, regOp1, regOp2, arg); }
void XEmitter::VPSRLVD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3845, regOp1, regOp2, arg); }
void XEmitter::VPSRLVQ(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVX2Op(bits, 0x66, 0x3845, regOp1, regOp2, arg, 0, 1); }

void XEmitter::VFMADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x3898, regOp1, regOp2, arg); }
void XEmitter::VFMADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38A8, regOp1, regOp2, arg); }
void XEmitter::VFMADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38B8, regOp1, regOp2, arg); }
void XEmitter::VFMADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x3898, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38A8, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38B8, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADD132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x3899, regOp1, regOp2, arg); }
void XEmitter::VFMADD213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38A9, regOp1, regOp2, arg); }
void XEmitter::VFMADD231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38B9, regOp1, regOp2, arg); }
void XEmitter::VFMADD132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x3899, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADD213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38A9, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADD231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38B9, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x389A, regOp1, regOp2, arg); }
void XEmitter::VFMSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38AA, regOp1, regOp2, arg); }
void XEmitter::VFMSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38BA, regOp1, regOp2, arg); }
void XEmitter::VFMSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x389A, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38AA, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)    { WriteAVXOp(bits, 0x66, 0x38BA, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x389B, regOp1, regOp2, arg); }
void XEmitter::VFMSUB213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38AB, regOp1, regOp2, arg); }
void XEmitter::VFMSUB231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38BB, regOp1, regOp2, arg); }
void XEmitter::VFMSUB132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x389B, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38AB, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUB231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)              { WriteAVXOp(0,    0x66, 0x38BB, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x389C, regOp1, regOp2, arg); }
void XEmitter::VFNMADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38AC, regOp1, regOp2, arg); }
void XEmitter::VFNMADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38BC, regOp1, regOp2, arg); }
void XEmitter::VFNMADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x389C, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38AC, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38BC, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x389D, regOp1, regOp2, arg); }
void XEmitter::VFNMADD213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38AD, regOp1, regOp2, arg); }
void XEmitter::VFNMADD231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38BD, regOp1, regOp2, arg); }
void XEmitter::VFNMADD132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x389D, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38AD, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMADD231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38BD, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x389E, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38AE, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38BE, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x389E, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38AE, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg)   { WriteAVXOp(bits, 0x66, 0x38BE, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB132SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x389F, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB213SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38AF, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB231SS(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38BF, regOp1, regOp2, arg); }
void XEmitter::VFNMSUB132SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x389F, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB213SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38AF, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFNMSUB231SD(X64Reg regOp1, X64Reg regOp2, OpArg arg)             { WriteAVXOp(0,    0x66, 0x38BF, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADDSUB132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3896, regOp1, regOp2, arg); }
void XEmitter::VFMADDSUB213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38A6, regOp1, regOp2, arg); }
void XEmitter::VFMADDSUB231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38B6, regOp1, regOp2, arg); }
void XEmitter::VFMADDSUB132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3896, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADDSUB213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38A6, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMADDSUB231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38B6, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUBADD132PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3897, regOp1, regOp2, arg); }
void XEmitter::VFMSUBADD213PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38A7, regOp1, regOp2, arg); }
void XEmitter::VFMSUBADD231PS(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38B7, regOp1, regOp2, arg); }
void XEmitter::VFMSUBADD132PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x3897, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUBADD213PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38A7, regOp1, regOp2, arg, 0, 1); }
void XEmitter::VFMSUBADD231PD(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) { WriteAVXOp(bits, 0x66, 0x38B7, regOp1, regOp2, arg, 0, 1); }

void XEmitter::SARX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {WriteBMI2Op(bits, 0xF3, 0x38F7, regOp1, regOp2, arg);}
void XEmitter::SHLX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {WriteBMI2Op(bits, 0x66, 0x38F7, regOp1, regOp2, arg);}
void XEmitter::SHRX(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {WriteBMI2Op(bits, 0xF2, 0x38F7, regOp1, regOp2, arg);}
void XEmitter::RORX(int bits, X64Reg regOp, OpArg arg, u8 rotate)      {WriteBMI2Op(bits, 0xF2, 0x3AF0, regOp, INVALID_REG, arg, 1); Write8(rotate);}
void XEmitter::PEXT(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) {WriteBMI2Op(bits, 0xF3, 0x38F5, regOp1, regOp2, arg);}
void XEmitter::PDEP(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) {WriteBMI2Op(bits, 0xF2, 0x38F5, regOp1, regOp2, arg);}
void XEmitter::MULX(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) {WriteBMI2Op(bits, 0xF2, 0x38F6, regOp2, regOp1, arg);}
void XEmitter::BZHI(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2) {WriteBMI2Op(bits, 0x00, 0x38F5, regOp1, regOp2, arg);}
void XEmitter::BLSR(int bits, X64Reg regOp, OpArg arg)                 {WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x1, regOp, arg);}
void XEmitter::BLSMSK(int bits, X64Reg regOp, OpArg arg)               {WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x2, regOp, arg);}
void XEmitter::BLSI(int bits, X64Reg regOp, OpArg arg)                 {WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x3, regOp, arg);}
void XEmitter::BEXTR(int bits, X64Reg regOp1, OpArg arg, X64Reg regOp2){WriteBMI1Op(bits, 0x00, 0x38F7, regOp1, regOp2, arg);}
void XEmitter::ANDN(int bits, X64Reg regOp1, X64Reg regOp2, OpArg arg) {WriteBMI1Op(bits, 0x00, 0x38F2, regOp1, regOp2, arg);}

// Prefixes

void XEmitter::LOCK()  { Write8(0xF0); }
void XEmitter::REP()   { Write8(0xF3); }
void XEmitter::REPNE() { Write8(0xF2); }
void XEmitter::FSOverride() { Write8(0x64); }
void XEmitter::GSOverride() { Write8(0x65); }

void XEmitter::FWAIT()
{
	Write8(0x9B);
}

// TODO: make this more generic
void XEmitter::WriteFloatLoadStore(int bits, FloatOp op, FloatOp op_80b, OpArg arg)
{
	int mf = 0;
	_assert_msg_(!(bits == 80 && op_80b == floatINVALID), "WriteFloatLoadStore: 80 bits not supported for this instruction");
	switch (bits)
	{
	case 32: mf = 0; break;
	case 64: mf = 4; break;
	case 80: mf = 2; break;
	default: _assert_msg_(false, "WriteFloatLoadStore: invalid bits (should be 32/64/80)");
	}
	Write8(0xd9 | mf);
	// x87 instructions use the reg field of the ModR/M byte as opcode:
	if (bits == 80)
		op = op_80b;
	arg.WriteRest(this, 0, (X64Reg) op);
}

void XEmitter::FLD(int bits, OpArg src) {WriteFloatLoadStore(bits, floatLD, floatLD80, src);}
void XEmitter::FST(int bits, OpArg dest) {WriteFloatLoadStore(bits, floatST, floatINVALID, dest);}
void XEmitter::FSTP(int bits, OpArg dest) {WriteFloatLoadStore(bits, floatSTP, floatSTP80, dest);}
void XEmitter::FNSTSW_AX() { Write8(0xDF); Write8(0xE0); }

void XEmitter::RDTSC() { Write8(0x0F); Write8(0x31); }

void XCodeBlock::PoisonMemory(int offset) {
	// x86/64: 0xCC = breakpoint
	memset(region + offset, 0xCC, region_size - offset);
}

}
