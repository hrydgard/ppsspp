#include <xtl.h>
#include "ppcEmitter.h"

// Helper

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |       D      |       A      |       B      |              XO             |Rc|
#define X_FORM(OPCD, D, A, B, XO, Rc) { \
    int a = (A), b = (B), d = (D); \
    Write32((OPCD << 26) | (d << 21) | (a << 16) | (b << 11) | (((XO) & 0x3ff) << 1) | (Rc)); \
}

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |       D      |       A      |       B      |OE|            XO            |Rc|
#define XO_FORM(OPCD, D, A, B, OE, XO, Rc) { \
    int a = (A), b = (B), d = (D); \
    Write32((OPCD << 26) | (d << 21) | (a << 16) | (b << 11) | (OE << 10) | (((XO) & 0x1ff) << 1) | (Rc)); \
}

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |       D      |       A      |      B       |       C      |      XO      |Rc|
#define A_FORM(OPCD, D, A, B, C, XO, Rc) { \
    int a = (A), b = (B), c = (C), d = (D); \
    Write32((OPCD << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (XO << 1) | (Rc)); \
}

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |       D      |       A      |                 d/UIMM/SIMM                   |
#define D_FORM(OPCD, RD, RA, IMM) { \
    int _ra = (RA), _rd = (RD); \
    Write32((OPCD << 26) | (_rd << 21) | (_ra << 16) | ((IMM) & 0xffff)); \
}

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |       D      |       A      |      B       |       C      |      XO      |Rc|
#define A_FORM(OPCD, D, A, B, C, XO, Rc) { \
    int a = (A), b = (B), c = (C), d = (D); \
    Write32((OPCD << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (XO << 1) | (Rc)); \
}

//   0 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
//  |      OPCD      |    BO/crbD   |    BI/crbA   |     crbB     |              XO             |LK|
#define XL_FORM(OPCD, crbD, crbA, crbB, XO, LK) { \
    X_FORM(OPCD, crbD, crbA, crbB, XO, LK); \
}

namespace PpcGen {

	// Mul stuff
	
	void PPCXEmitter::DIVW	(PPCReg Rt,	PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rt, Ra, Rb, 0, 491, 0); 
	}
	void PPCXEmitter::DIVWU	(PPCReg Rt,	PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rt, Ra, Rb, 0, 459, 0); 
	}
	void PPCXEmitter::MULLW	(PPCReg Rt,	PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rt, Ra, Rb, 0, 235, 0); 
	}
	void PPCXEmitter::MULHW	(PPCReg Rt,	PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rt, Ra, Rb, 0, 75, 0); 
	}
	void PPCXEmitter::MULHWU(PPCReg Rt,	PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rt, Ra, Rb, 0, 11, 0); 
	}

	// Arithmetics ops	
	void PPCXEmitter::ADDZE	(PPCReg Rd, PPCReg Ra) {
		XO_FORM(31, Rd, Ra, 0, 0, 202, 0); 
	}

	void PPCXEmitter::ADD	(PPCReg Rd, PPCReg Ra, PPCReg Rb) {
		u32 instr = (0x7C000214 | (Rd << 21) | (Ra << 16) |  (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::ADDI	(PPCReg Rd, PPCReg Ra, short imm) {
		u32 instr = (0x38000000  | (Rd << 21) | (Ra << 16) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::ADDIS	(PPCReg Rd, PPCReg Ra, short imm) {
		u32 instr = (0x3C000000  | (Rd << 21) | (Ra << 16) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::AND  (PPCReg Rs, PPCReg Ra, PPCReg Rb) {
		u32 instr = (0x7C000038 | (Ra << 21) | (Rs << 16) |  (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::ANDI	(PPCReg Rdest, PPCReg Ra, unsigned short imm) {
		u32 instr = (0x70000000 | (Ra << 21) | (Rdest << 16) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::ANDIS	(PPCReg Rdest, PPCReg Ra, unsigned short imm) {
		u32 instr = (0x74000000 | (Ra << 21) | (Rdest << 16) | ((imm) & 0xffff));
		Write32(instr);
	}

	// Memory load/store operations
	void PPCXEmitter::LI(PPCReg dest, unsigned short imm) {
		u32 instr = (0x38000000 | (dest << 21) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::LIS(PPCReg dest, unsigned short imm) {
		u32 instr = (0x3C000000 | (dest << 21) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::LBZ	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0x88000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::LBZX	(PPCReg dest, PPCReg a, PPCReg b) {
		u32 instr = ((31<<26) | (dest << 21) | (a << 16) | (b << 11) | (87<<1));
		Write32(instr);
	}

	void PPCXEmitter::LHZ	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0xA0000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::LHBRX	(PPCReg dest, PPCReg src, PPCReg offset) {
		u32 instr = (0x7C00062C | (dest << 21) | (src << 16) | (offset << 11));
		Write32(instr);
	}

	void PPCXEmitter::LWZ	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0x80000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::LWBRX	(PPCReg dest, PPCReg src, PPCReg offset) {
		u32 instr = (0x7C00042C | (dest << 21) | (src << 16) | (offset << 11));
		Write32(instr);
	}

	void PPCXEmitter::STB	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0x98000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::STBX	(PPCReg dest, PPCReg a, PPCReg b) {
		u32 instr = ((31<<26) | (dest << 21) | (a << 16) | (b << 11) | (215 << 1));
		Write32(instr);
	}

	void PPCXEmitter::STH	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0xB0000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::STHBRX (PPCReg dest, PPCReg src, PPCReg offset) {
		u32 instr = (0x7C00072C | (dest << 21) | (src << 16) | (offset << 11));
		Write32(instr);
	}

	void PPCXEmitter::STW	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0x90000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}	

	void PPCXEmitter::STWU	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = (0x94000000 | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::STWBRX (PPCReg dest, PPCReg src, PPCReg offset) {
		u32 instr = (0x7C00052C | (dest << 21) | (src << 16) | (offset << 11));
		Write32(instr);
	}

	void PPCXEmitter::LD	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = ((58 << 26) | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}
	void PPCXEmitter::STD	(PPCReg dest, PPCReg src, int offset) {
		u32 instr = ((62 << 26) | (dest << 21) | (src << 16) | ((offset) & 0xffff));
		Write32(instr);
	}

	// Branch operations
	void PPCXEmitter::B (const void *fnptr) {
		s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x48000000 | ((s32)((func) & 0x3fffffc)));
		Write32(instr);
	}

	void PPCXEmitter::BL(const void *fnptr) {
		s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x48000001 | ((s32)((func) & 0x3fffffc)));
		Write32(instr);
	}

	void PPCXEmitter::BA (const void *fnptr) {
		s32 func =  (s32)fnptr;
		u32 instr = (0x48000002 | ((s32)((func) & 0x3fffffc)));
		Write32(instr);
	}

	void PPCXEmitter::BLA (const void *fnptr) {
		s32 func =  (s32)fnptr;
		u32 instr = (0x48000003 | ((s32)((func) & 0x3fffffc)));
		Write32(instr);
	}


#define IS_SMALL_JUMP (((u32)code - (u32)fnptr)>=-32767 && ((u32)code - (u32)fnptr)<=-32767)
#define CHECK_SMALL_JUMP { if(IS_SMALL_JUMP) { DebugBreak(); } }

	void PPCXEmitter::BEQ (const void *fnptr) {
		CHECK_SMALL_JUMP

			s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x41820000 | ( func & 0xfffc));
		Write32(instr);
	}


	void PPCXEmitter::BGT(const void *fnptr) {
		CHECK_SMALL_JUMP

			s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x41810000 | (((s16)(((func)+1))) & 0xfffc));
		Write32(instr);
	}


	void PPCXEmitter::BLTCTR() {
		Write32((19 << 26) | (12 << 21) | (528 <<1));
		//	Break();
	}

	void PPCXEmitter::BLT (const void *fnptr) {
		//CHECK_JUMP
		if (!IS_SMALL_JUMP) {
			u32 func_addr = (u32) fnptr;
			// Load func address
			MOVI2R(R0, func_addr);
			// Set it to link register
			MTCTR(R0);
			// Branch
			BLTCTR();
			return;
		}

		s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x41800000 | (((s16)(((func)+1))) & 0xfffc));
		Write32(instr);
	}

	void PPCXEmitter::BLE (const void *fnptr) {		
		CHECK_SMALL_JUMP

		s32 func =  (s32)fnptr - s32(code);
		u32 instr = (0x40810000 | (((s16)(((func)+1))) & 0xfffc));
		Write32(instr);
	}

	void PPCXEmitter::BCTRL() {
		Write32(0x4E800421);
	}

	void PPCXEmitter::BCTR() {
		Write32(0x4E800420);
	}

	// Link Register
	void PPCXEmitter::MFLR(PPCReg r) {
		Write32(0x7C0802A6 | r << 21);
	}

	void PPCXEmitter::MTLR(PPCReg r) {
		Write32(0x7C0803A6 | r << 21);
	}

	void PPCXEmitter::MTCTR(PPCReg r) {
		Write32(0x7C0903A6 | r << 21);
	}

	void PPCXEmitter::BLR() {
		Write32(0x4E800020);
	}

	void PPCXEmitter::BGTLR() {
		Write32(0x4D810020);
	}

	// Fixup
	FixupBranch PPCXEmitter::B()
	{
		FixupBranch branch;
		branch.type = _B;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}

	FixupBranch PPCXEmitter::BL()
	{
		FixupBranch branch;
		branch.type = _BL;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}


	FixupBranch PPCXEmitter::BNE() {
		FixupBranch branch;
		branch.type = _BNE;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}

	FixupBranch PPCXEmitter::BLT() {
		FixupBranch branch;
		branch.type = _BLT;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}

	FixupBranch PPCXEmitter::BLE() {
		FixupBranch branch;
		branch.type = _BLE;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}

	FixupBranch PPCXEmitter::B_Cond(FixupBranchType type) {
		FixupBranch branch;
		branch.type = type;
		branch.ptr = code;
		branch.condition = condition;
		//We'll write NOP here for now.
		Write32(0x60000000);
		return branch;
	}

	void PPCXEmitter::SetJumpTarget(FixupBranch const &branch)
	{
		s32 distance =  s32(code) - (s32)branch.ptr;
		_assert_msg_(DYNA_REC, distance > -32767
			&& distance <=  32767,
			"SetJumpTarget out of range (%p calls %p)", code,
			branch.ptr);

		switch(branch.type) {
		case _B:
			*(u32*)branch.ptr =  (0x48000000 | ((s32)((distance) & 0x3fffffc)));
			break;
		case _BL:
			*(u32*)branch.ptr =  (0x48000001 | ((s32)((distance) & 0x3fffffc)));
			break;
		case _BEQ:			
			*(u32*)branch.ptr =  (0x41820000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		case _BNE:
			*(u32*)branch.ptr =  (0x40820000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		case _BLT:			
			*(u32*)branch.ptr =  (0x41800000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		case _BLE:
			*(u32*)branch.ptr =  (0x40810000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		case _BGT:
			*(u32*)branch.ptr =  (0x41810000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		case _BGE:
			*(u32*)branch.ptr =  (0x40800000 | ((s16)(((distance)+1)) & 0xfffc));
			break;
		default:
			// Error !!!
			_assert_msg_(DYNA_REC, 0, "SetJumpTarget unknow branch type: %d", branch.type);
			break;
		}
	}

	// Compare (Only use CR0 atm...)
	void PPCXEmitter::CMPI(PPCReg dest, unsigned short imm) {
		Write32((11<<26) | (dest << 16) | ((imm) & 0xffff));
	}

	void PPCXEmitter::CMPLI(PPCReg dest, unsigned short imm) {
		Write32((10<<26) | (dest << 16) | ((imm) & 0xffff));
	}

	void PPCXEmitter::CMP(PPCReg a, PPCReg b, CONDITION_REGISTER cr) {
		Write32((31 << 26) | (a << 16) | (b << 11));
	}
	void PPCXEmitter::CMPL(PPCReg a, PPCReg b, CONDITION_REGISTER cr) {
		Write32((31 << 26) | (a << 16) | (b << 11) | (1<<6));
	}
	void PPCXEmitter::MFCR	(PPCReg dest) {
		Write32(0x7C000026 | (dest << 21));
	}
	void PPCXEmitter::MTCR	(PPCReg dest) {
		Write32(0x7C000120 | (dest << 21) | (0xff<<12));
	}
	
	void PPCXEmitter::CROR	(int bt, int ba, int bb) {
		XL_FORM(19, bt, ba, bb, 449, 0);
	}

	void PPCXEmitter::ISEL	(PPCReg Rt, PPCReg Ra, PPCReg Rb, CONDITION_REGISTER cr) {
		// Not working !!
		A_FORM(31, Rt, Ra, Rb, cr, 15, 0);
		Break();
	}

	// Others operation
	void PPCXEmitter::ORI(PPCReg Rd, PPCReg Ra, unsigned short imm) {
		u32 instr = (0x60000000 | (Ra << 21) | (Rd << 16) | (imm & 0xffff));
		Write32(instr);
	}	
	void PPCXEmitter::XORI	(PPCReg Rdest, PPCReg Ra, unsigned short imm) {	
		u32 instr = (0x68000000 | (Ra << 21) | (Rdest << 16) | (imm & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::OR(PPCReg Rdest, PPCReg Ra, PPCReg Rb) {	
		u32 instr = (0x7C000378 | (Ra << 21) | (Rdest << 16) | (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::XOR(PPCReg Rd, PPCReg Ra, PPCReg Rb) {	
		u32 instr = (0x7C000278 | (Ra << 21) | (Rd << 16) | (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::NEG(PPCReg Rd, PPCReg Ra) {	
		XO_FORM(31, Rd, Ra, 0, 0, 104, 0);
	}
	

	void PPCXEmitter::NOR(PPCReg Rd, PPCReg Ra, PPCReg Rb) {	
		u32 instr = (0x7C0000f8 | (Ra << 21) | (Rd << 16) | (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::SUBF(PPCReg Rd, PPCReg Ra, PPCReg Rb, int RCFlags) {	
		u32 instr = (0x7C000050 | (Rd << 21) | (Ra << 16) |  (Rb << 11) | (RCFlags & 1));
		Write32(instr);
	}
		
	void PPCXEmitter::SUBFC	(PPCReg Rd, PPCReg Ra, PPCReg Rb) {
		XO_FORM(31, Rd, Ra, Rb, 0, 8, 0);
	}

	
	void PPCXEmitter::SUBFE(PPCReg Rd, PPCReg Ra, PPCReg Rb) {	
		XO_FORM(31, Rd, Ra, Rb, 0, 136, 0);
	}

	// Quick Call
	// dest = LIS(imm) + ORI(+imm)
	void PPCXEmitter::MOVI2R(PPCReg dest, unsigned int imm) {
		/*if (imm == (unsigned short)imm) {
			// 16bit
			LI(dest, imm & 0xFFFF);
		} else */{	
			// HI 16bit
			LIS(dest, imm>>16);
			if ((imm & 0xFFFF) != 0) {
				// LO 16bit
				ORI(dest, dest, imm & 0xFFFF);
			}
		}
	}

	void PPCXEmitter::QuickCallFunction(void *func) {
		/** TODO : can use simple jump **/
		
		u32 func_addr = (u32) func;
		// Load func address
		MOVI2R(R0, func_addr);
		// Set it to link register
		MTCTR(R0);
		// Branch
		BCTRL();
	}

	// sign
	void PPCXEmitter::EXTSB	(PPCReg dest, PPCReg src) {
		Write32((0x7C000774 | (src << 21) | (dest << 16)));
	}

	void PPCXEmitter::EXTSH	(PPCReg dest, PPCReg src) {
		Write32(0x7C000734 | (src << 21) | (dest << 16));
	}

	void PPCXEmitter::EQV	(PPCReg Ra, PPCReg Rs, PPCReg Rb) {
		X_FORM(31, Rs, Ra, Rb, 284, 0);
	}

	void PPCXEmitter::RLWINM (PPCReg dest, PPCReg src, int shift, int start, int end) {
		Write32((21<<26) | (src << 21) | (dest << 16) | (shift << 11) | (start << 6) | (end << 1));
	}

	// Shift Instructions
	void PPCXEmitter::SRAW	(PPCReg dest, PPCReg src, PPCReg shift) {
		X_FORM(31, src, dest, shift, 792, 0);
	}
	void PPCXEmitter::SRAWI	(PPCReg dest, PPCReg src, unsigned short imm) {
		X_FORM(31, src, dest, imm, 824, 0);
	}

	void PPCXEmitter::SLW	(PPCReg dest, PPCReg src, PPCReg shift) {
		X_FORM(31, src, dest, shift, 24, 0);
	}

	void PPCXEmitter::SLWI	(PPCReg dest, PPCReg src, unsigned short imm) {
		RLWINM(dest, src, imm, 0, (31-imm));
	}

	void PPCXEmitter::SRW	(PPCReg dest, PPCReg src, PPCReg shift) {
		X_FORM(31, src, dest, shift, 536, 0);
	}

	void PPCXEmitter::SRWI	(PPCReg dest, PPCReg src, unsigned short imm) {
		RLWINM(dest, src, (32-imm), imm, 31);
	}
	
	void PPCXEmitter::ROTRW	(PPCReg dest, PPCReg src, PPCReg shift) {
		
	}

	void PPCXEmitter::ROTRWI(PPCReg dest, PPCReg src, unsigned short imm) {
		RLWINM(dest, src, (32-imm), 0, 31);
	}

	void  PPCXEmitter::ROTLW	(PPCReg dest, PPCReg src, PPCReg shift) {
	}

	void  PPCXEmitter::ROTLWI	(PPCReg dest, PPCReg src, unsigned short imm) {
	}

	// Fpu
	void PPCXEmitter::LFS	(PPCReg FRt, PPCReg Ra, unsigned short offset) {
		D_FORM(48, FRt, Ra, offset);
	}
	void PPCXEmitter::LFD	(PPCReg FRt, PPCReg Ra, unsigned short offset) {
		D_FORM(50, FRt, Ra, offset);
	}
	void PPCXEmitter::SFS	(PPCReg FRt, PPCReg Ra, unsigned short offset) {
		D_FORM(52, FRt, Ra, offset);
	}
	void PPCXEmitter::SFD	(PPCReg FRt, PPCReg Ra, unsigned short offset) {
		D_FORM(54, FRt, Ra, offset);
	}

	
	void PPCXEmitter::MOVI2F	(PPCReg dest, float imm, bool negate) {
		u32 tmp;

		union convert { 
			unsigned int i; 
			float f; 
		} fc;

		fc.f = imm;

		MOVI2R(R6, fc.i);

		// R7 = imm
		MOVI2R(R7, (u32)&tmp);
		STW(R6, R7);

		// dest = R7
		LFS(dest, R7, 0);

		if (negate == true) {
			FNEG(dest, dest);
		}
	}
	
	void PPCXEmitter::SaveFloatSwap(PPCReg FRt, PPCReg Base, PPCReg offset) {
		// used for swapping float ...
		u32 tmp;

		// Save Value in tmp	
		MOVI2R(R7, (u32)&tmp);	
		SFS(FRt, R7, 0);

		// Load the value in R6
		LWZ(R6, R7); 
		
		// Save the final value
		STWBRX(R6, Base, offset); 
	}

	void PPCXEmitter::LoadFloatSwap(PPCReg FRt, PPCReg Base, PPCReg offset) {
		// used for swapping float ...
		u32 tmp;
		
		// Load Value into a temp REG
		LWBRX(R6, Base, offset); 

		// Save it in tmp
		MOVI2R(R7, (u32)&tmp);
		STW(R6, R7);

		// Load the final value
		LFS(FRt, R7, 0);
	}

	// Fpu move instruction
	void PPCXEmitter::FMR	(PPCReg FRt, PPCReg FRb) {
		X_FORM(63, FRt, 0, FRb, 72, 0);
	}
	void PPCXEmitter::FNEG	(PPCReg FRt, PPCReg FRb) {
		X_FORM(63, FRt, 0, FRb, 40, 0);
	}
	void PPCXEmitter::FABS	(PPCReg FRt, PPCReg FRb) {
		X_FORM(63, FRt, 0, FRb, 264, 0);
	}
	void PPCXEmitter::FNABS	(PPCReg FRt, PPCReg FRb) {
		Break();
		X_FORM(63, FRt, 0, FRb, 136, 0);
	}
	void PPCXEmitter::FCPSGN	(PPCReg FRt, PPCReg FRb) {
		Break();
		X_FORM(63, FRt, 0, FRb, 8, 0);
	}

	// Fpu arith	
	void PPCXEmitter::FADDS	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		A_FORM(59, FRt, FRa, FRb, 0, 21, 0);
	}
	void PPCXEmitter::FSUBS	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		A_FORM(59, FRt, FRa, FRb, 0, 20, 0);
	}
	void PPCXEmitter::FADD	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		A_FORM(63, FRt, FRa, FRb, 0, 21, 0);
	}
	void PPCXEmitter::FSUB	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		A_FORM(63, FRt, FRa, FRb, 0, 20, 0);
	}
	void PPCXEmitter::FMUL	(PPCReg FRt, PPCReg FRa, PPCReg FRc) {
		A_FORM(63, FRt, FRa, 0, FRc, 25, 0);
	}
	void PPCXEmitter::FMULS	(PPCReg FRt, PPCReg FRa, PPCReg FRc) {
		A_FORM(59, FRt, FRa, 0, FRc, 25, 0);
	}
	void PPCXEmitter::FDIV	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		A_FORM(63, FRt, FRa, FRb, 0, 18, 0);
	}
	void PPCXEmitter::FDIVS	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {		
		A_FORM(59, FRt, FRa, FRb, 0, 18, 0);
	}
	void PPCXEmitter::FSQRT	(PPCReg FRt, PPCReg FRb) {		
		A_FORM(63, FRt, 0, FRb, 0, 22, 0);
	}
	void PPCXEmitter::FSQRTS	(PPCReg FRt, PPCReg FRb) {		
		A_FORM(59, FRt, 0, FRb, 0, 22, 0);
	}
	void PPCXEmitter::FSQRTE	(PPCReg FRt, PPCReg FRb) {
		Break();
	}
	void PPCXEmitter::FSQRTES(PPCReg FRt, PPCReg FRb) {
		Break();
	}
	void PPCXEmitter::FRE	(PPCReg FRt, PPCReg FRb) {
		Break();
	}
	void PPCXEmitter::FRES	(PPCReg FRt, PPCReg FRb) {
		Break();
	}

	// Fpu mul add
	void PPCXEmitter::FMADD	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb) {
		A_FORM(63, FRt, FRa, FRb, FRc, 29, 0);
	}
	void PPCXEmitter::FMSUB	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb) {
		A_FORM(63, FRt, FRa, FRb, FRc, 28, 0);
	}
	void PPCXEmitter::FMADDS	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb) {
		A_FORM(59, FRt, FRa, FRb, FRc, 29, 0);
	}
	void PPCXEmitter::FMSUBS	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb) {
		A_FORM(59, FRt, FRa, FRb, FRc, 28, 0);
	}

	// Fpu sel
	void PPCXEmitter::FSEL	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb) {		
		A_FORM(63, FRt, FRa, FRb, FRc, 23, 0);
	}
	// #define fpmin(a,b) __fsel((a)-(b), b,a)
	void PPCXEmitter::FMIN	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		PPCReg safe = FPR3; // hope it's safe !!
		FSUBS(safe, FRa, FRb);
		FSEL(FRt, safe, FRb, FRa);
		//Break();
	}
	// #define fpmax(a,b) __fsel((a)-(b), a,b)
	void PPCXEmitter::FMAX	(PPCReg FRt, PPCReg FRa, PPCReg FRb) {
		PPCReg safe = FPR3; // hope it's safe !!
		FSUBS(safe, FRa, FRb);
		FSEL(FRt, safe, FRa, FRb);
		//Break();
	}

	

	void PPCXEmitter::FCMPU	(int Bf, PPCReg FRa, PPCReg FRb) { // unordered
		X_FORM(63, Bf, FRa, FRb, 0, 0);
	} 

	void PPCXEmitter::FCMPO	(int Bf, PPCReg FRa, PPCReg FRb) { // ordered
		X_FORM(63, Bf, FRa, FRb, 32, 0);
	}

	// Prologue / epilogue

	/** save/load fpr in a static buffer ... **/
	static double _fprTmp[32];

	void PPCXEmitter::Prologue() {
		// Save regs
		u32 regSize = 8; // 4 in 32bit system
		u32 stackFrameSize = 0x1F0;

		// Write Prologue (setup stack frame etc ...)
		// Save Lr
		MFLR(R12);

		// Save gpr
		for(int i = 14; i < 32; i ++) {
			STD((PPCReg)i, R1, -((33 - i) * regSize));
		}

		// Save r12
		STW(R12, R1, -0x8);
#if 0
		// add fpr frame
		ADDI(R12, R1, -0x98);
		
		// Load fpr
		for(int i = 14; i < 32; i ++) {
			SFD((PPCReg)i, R1, -((32 - i) * regSize));
		}
#endif
		// allocate stack
		STWU(R1, R1, -stackFrameSize);		

#if 1
		// load fpr buff
		MOVI2R(R5, (u32)&_fprTmp);
		
		// Save fpr
		for(int i = 14; i < 32; i ++) {
			SFD((PPCReg)i, R5, i * regSize);
		}
#endif
	}

	void PPCXEmitter::Epilogue() {		
		u32 regSize = 8; // 4 in 32bit system
		u32 stackFrameSize = 0x1F0;

		//Break();

		// Write Epilogue (restore stack frame, return)
		// free stack
		ADDI(R1, R1, stackFrameSize);	
#if 0
		ADDI(R12, R1, -0x98);	

		// Restore fpr
		for(int i = 14; i < 32; i ++) {
			LFD((PPCReg)i, R1, -((32 - i) * regSize));
		}
#endif
		// Restore gpr
		for(int i = 14; i < 32; i ++) {
			LD((PPCReg)i, R1, -((33 - i) * regSize));
		}		

		// recover r12 (LR saved register)
		LWZ (R12, R1, -0x8);

		// Restore Lr
		MTLR(R12);
		
#if 1
		// load fpr buff
		MOVI2R(R5, (u32)&_fprTmp);
		
		// Load fpr
		for(int i = 14; i < 32; i ++) {
			LFD((PPCReg)i, R5, i * regSize);
		}
#endif
	}

	// Others ...
	void PPCXEmitter::SetCodePtr(u8 *ptr)
	{
		code = ptr;
		startcode = code;
		lastCacheFlushEnd = ptr;
	}

	const u8 *PPCXEmitter::GetCodePtr() const
	{
		return code;
	}

	u8 *PPCXEmitter::GetWritableCodePtr()
	{
		return code;
	}

	void PPCXEmitter::ReserveCodeSpace(u32 bytes)
	{
		for (u32 i = 0; i < bytes/4; i++)
			Write32(0x60000000); //nop
	}

	const u8 *PPCXEmitter::AlignCode16()
	{
		ReserveCodeSpace((-(s32)code) & 15);
		return code;
	}

	const u8 *PPCXEmitter::AlignCodePage()
	{
		ReserveCodeSpace((-(s32)code) & 4095);
		return code;
	}

	void PPCXEmitter::FlushIcache()
	{
		FlushIcacheSection(lastCacheFlushEnd, code);
		lastCacheFlushEnd = code;
	}

	void PPCXEmitter::FlushIcacheSection(u8 *start, u8 *end)
	{
		u8 * addr = start;
		while(addr < end) {
			__asm dcbst r0, addr
			__asm icbi r0, addr
			addr += 4;
		}
		__emit(0x7c0004ac);//sync
		__emit(0x4C00012C);//isync
	}


} // namespace