#include <xtl.h>
#include "ppcEmitter.h"

namespace PpcGen {

	// Arithmetics ops
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

	void PPCXEmitter::ANDI	(PPCReg Rd, PPCReg Ra, unsigned short imm) {
		u32 instr = (0x70000000 | (Rd << 21) | (Ra << 16) | ((imm) & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::ANDIS	(PPCReg Rd, PPCReg Ra, unsigned short imm) {
		u32 instr = (0x74000000 | (Rd << 21) | (Ra << 16) | ((imm) & 0xffff));
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

	void PPCXEmitter::CMP(PPCReg a, PPCReg b) {
		Write32((31 << 26) | (a << 16) | (b << 11));
	}
	void PPCXEmitter::CMPL(PPCReg a, PPCReg b) {
		Write32((31 << 26) | (a << 16) | (b << 11) | (1<<6));
	}

	// Others operation
	void PPCXEmitter::ORI(PPCReg src, PPCReg dest, unsigned short imm) {
		u32 instr = (0x60000000 | (src << 21) | (dest << 16) | (imm & 0xffff));
		Write32(instr);
	}

	void PPCXEmitter::OR(PPCReg Rd, PPCReg Ra, PPCReg Rb) {	
		u32 instr = (0x7C000378 | (Ra << 21) | (Rd << 16) | (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::XOR(PPCReg Rd, PPCReg Ra, PPCReg Rb) {	
		u32 instr = (0x7C000278 | (Ra << 21) | (Rd << 16) | (Rb << 11));
		Write32(instr);
	}

	void PPCXEmitter::SUBF(PPCReg Rd, PPCReg Ra, PPCReg Rb, int RCFlags) {	
		u32 instr = (0x7C000050 | (Rd << 21) | (Ra << 16) |  (Rb << 11) | (RCFlags & 1));
		Write32(instr);
	}

	// Quick Call
	// dest = LIS(imm) + ORI(+imm)
	void PPCXEmitter::MOVI2R(PPCReg dest, unsigned int imm) {
		if (imm == (unsigned short)imm) {
			// 16bit
			LI(dest, imm & 0xFFFF);
		} else {	
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

	void PPCXEmitter::RLWINM (PPCReg dest, PPCReg src, int shift, int start, int end) {
		Write32((21<<26) | (src << 21) | (dest << 16) | (shift << 11) | (start << 6) | (end << 1));
	}

	// Prologue / epilogue

	void PPCXEmitter::Prologue() {
		// Save regs
		u32 regSize = 8; // 4 in 32bit system
		u32 stackFrameSize = 32*32;//(35 - 12) * regSize;

		// Write Prologue (setup stack frame etc ...)
		// Save Lr
		MFLR(R12);

		for(int i = 14; i < 32; i ++) {
			STD((PPCReg)i, R1, -((33 - i) * regSize));
		}

		// Save r12
		STW(R12, R1, -0x8);

		// allocate stack
		STWU(R1, R1, -stackFrameSize);
	}

	void PPCXEmitter::Epilogue() {		
		u32 regSize = 8; // 4 in 32bit system
		u32 stackFrameSize = 32*32;//(35 - 12) * regSize;

		// Write Epilogue (restore stack frame, return)
		// free stack
		ADDI(R1, R1, stackFrameSize);	

		// Restore regs
		for(int i = 14; i < 32; i ++) {
			LD((PPCReg)i, R1, -((33 - i) * regSize));
		}

		// recover r12 (LR saved register)
		LWZ (R12, R1, -0x8);

		// Restore Lr
		MTLR(R12);
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