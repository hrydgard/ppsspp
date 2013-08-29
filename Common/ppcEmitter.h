// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

// WARNING - THIS LIBRARY IS NOT THREAD SAFE!!!


// http://www.csd.uwo.ca/~mburrel/stuff/ppc-asm.html
// http://publib.boulder.ibm.com/infocenter/pseries/v5r3/index.jsp?topic=/com.ibm.aix.aixassem/doc/alangref/linkage_convent.htm 
// http://publib.boulder.ibm.com/infocenter/pseries/v5r3/index.jsp?topic=/com.ibm.aix.aixassem/doc/alangref/instruction_set.htm

#ifndef _DOLPHIN_PPC_CODEGEN_
#define _DOLPHIN_PPC_CODEGEN_

#include "Common.h"
#include "MemoryUtil.h"
#include <vector>

#undef _IP
#undef R0
#undef _SP
#undef _LR
#undef _PC
#undef CALL

namespace PpcGen
{
	enum PPCReg
	{
		// GPRs (32)
		// Behaves as zero does in some instructions 
		R0 = 0,  
		// Stack pointer (SP) 
		R1, 
		// Reserved
		R2,  
		// Used to pass integer function parameters and return values 
		R3,  R4,  
		// Used to pass integer function parameters
		R5,  R6,  R7,  R8,  R9,  R10, 
		// General purpose 
		R11, 
		// Scratch 
		R12, 
		// Unused by the compiler reserved 
		R13, 
		// General purpose
		R14, R15, R16, R17, R18, R19,
		R20, R21, R22, R23, R24, R25, 
		R26, R27, R28, R29,	R30, R31,

		// CRs (7)
		CR0 = 0,

		// FPRs (32)
		// Scratch 
		FPR0 = 0,	
		// Used to pass double word function parameters and return values 
		FPR1,	FPR2,	FPR3,	FPR4,	
		FPR5,	FPR6,	FPR7,	FPR8,
		FPR9,	FPR10,	FPR11,	FPR12,	
		FPR13,
		// General purpose 
		FPR14,	FPR15,	FPR16,	FPR17,	
		FPR18,	FPR19,	FPR20,	FPR21,
		FPR22,	FPR23,	FPR24,	FPR25,
		FPR26,	FPR27,	FPR28,	FPR29,	
		FPR30,	FPR31,


		// Vmx (128)
		VR0 = 0, 
		// Used to pass vector function parameters and return values 
		VR1,	VR2,	VR3,	VR4,	
		VR5,	VR6,	VR7,	VR8,	
		VR9,	VR10,	VR11,	VR12,	
		VR13, // ...

		// Others regs
		LR, CTR, XER, FPSCR,

		// End

		INVALID_REG = 0xFFFFFFFF
	};
	enum IntegerSize
	{
		I_I8 = 0, 
		I_I16,
		I_I32,
		I_I64
	};

	enum
	{
		NUMGPRs = 31,
	};

	typedef const u8* JumpTarget;


	enum FixupBranchType {
		_B,
		_BEQ,
		_BNE,
		_BLT,
		_BLE,
		_BGT,
		_BGE,
		// Link register
		_BL
	};

	struct FixupBranch
	{
		u8 *ptr;
		u32 condition; // Remembers our codition at the time
		FixupBranchType type; //0 = B 1 = BL
	};

	class PPCXEmitter
	{
	private:
		u8 *code, *startcode;
		u8 *lastCacheFlushEnd;	
		u32 condition;

	protected:
		// Write opcode
		inline void Write32(u32 value) {*(u32*)code = value; code+=4;}
	public:
		PPCXEmitter() : code(0), startcode(0), lastCacheFlushEnd(0) {
		}
		PPCXEmitter(u8 *code_ptr) {
			code = code_ptr;
			lastCacheFlushEnd = code_ptr;
			startcode = code_ptr;
		}
		virtual ~PPCXEmitter() {}

		void SetCodePtr(u8 *ptr);
		void ReserveCodeSpace(u32 bytes);
		const u8 *AlignCode16();
		const u8 *AlignCodePage();
		const u8 *GetCodePtr() const;
		void FlushIcache();
		void FlushIcacheSection(u8 *start, u8 *end);
		u8 *GetWritableCodePtr();


		// Special purpose instructions

		// Debug Breakpoint
		void BKPT(u16 arg);

		// Hint instruction
		void YIELD();

		// Do nothing
		void NOP(int count = 1); //nop padding - TODO: fast nop slides, for amd and intel (check their manuals)

		// FixupBranch ops
		FixupBranch B();
		FixupBranch BL();	
		FixupBranch BNE();
		FixupBranch BLT();	
		FixupBranch BLE();	

		FixupBranch B_Cond(FixupBranchType type);

		void SetJumpTarget(FixupBranch const &branch);

		// Branch ops
		void B (const void *fnptr);
		void BL(const void *fnptr);
		void BA (const void *fnptr);
		void BLA(const void *fnptr);
		void BEQ(const void *fnptr);
		void BLE(const void *fnptr);
		void BLT(const void *fnptr);
		void BGT(const void *fnptr);
		void BEQ (PPCReg r);

		void BLR();
		void BGTLR(); // ??? used ?
		void BLTCTR();
		void BGTCTR();
		void BLECTR();
		void BGECTR();
		void BCTRL ();
		void BCTR();

		// Link Register
		void MFLR(PPCReg r);
		void MTLR(PPCReg r);
		void MTCTR(PPCReg r);


		// Logical Ops	
		void AND  (PPCReg Rs, PPCReg Ra, PPCReg Rb);
		void ANDI (PPCReg Rdest, PPCReg Ra, unsigned short imm);
		void ANDIS(PPCReg Rdest, PPCReg Ra, unsigned short imm);
		void NAND (PPCReg Rs, PPCReg Ra, PPCReg Rb);
		void OR   (PPCReg Rs, PPCReg Ra, PPCReg Rb);
		void ORI  (PPCReg Rdest, PPCReg Ra, unsigned short imm);
		void NOR  (PPCReg Rs, PPCReg Ra, PPCReg Rb);
		void XOR  (PPCReg Rs, PPCReg Ra, PPCReg Rb);
		void XORI (PPCReg Rdest, PPCReg Ra, unsigned short imm);
		void NEG  (PPCReg Rs, PPCReg Ra);
		void EQV	(PPCReg a, PPCReg b, PPCReg c);

		// Arithmetics ops
		void ADD	(PPCReg Rd, PPCReg Ra, PPCReg Rb);
		void ADDI	(PPCReg Rd, PPCReg Ra, short imm);
		void ADDIS	(PPCReg Rd, PPCReg Ra, short imm);
		void ADDC	(PPCReg Rd, PPCReg Ra, PPCReg Rb);
		void ADDZE	(PPCReg Rd, PPCReg Ra);
		void SUB	(PPCReg Rd, PPCReg Ra, PPCReg Rb) {
			// reverse ?
			SUBF(Rd, Rb, Ra);
		}
		// if RCFlags update CR0
		void SUBF	(PPCReg Rd, PPCReg Ra, PPCReg Rb, int RCFlags = 0);
		void SUBFC	(PPCReg Rd, PPCReg Ra, PPCReg Rb);
		void SUBFE	(PPCReg Rd, PPCReg Ra, PPCReg Rb);

		// Floating ops
		void DIVW(PPCReg dest, PPCReg dividend, PPCReg divisor);
		void DIVWU(PPCReg dest, PPCReg dividend, PPCReg divisor);
		void MULLW(PPCReg dest,	PPCReg src, PPCReg op2);
		void MULHW (PPCReg dest,	PPCReg src, PPCReg op2);
		void MULHWS(PPCReg dest,	PPCReg src, PPCReg op2);

		// Memory load/store operations
		void LI		(PPCReg dest, unsigned short imm);
		void LIS	(PPCReg dest, unsigned short imm);
		// dest = LIS(imm) + ORI(+imm)
		void MOVI2R	(PPCReg dest, unsigned int imm);

		// 8bit
		void LBZ	(PPCReg dest, PPCReg src, int offset = 0);
		void LBZX	(PPCReg dest, PPCReg a, PPCReg b);

		// 16bit
		void LHZ	(PPCReg dest, PPCReg src, int offset = 0);
		void LHBRX	(PPCReg dest, PPCReg src, PPCReg offset);
		// 32 bit
		void LWZ	(PPCReg dest, PPCReg src, int offset = 0);
		void LWBRX	(PPCReg dest, PPCReg src, PPCReg offset);
		// 64 bit
		void LD		(PPCReg dest, PPCReg src, int offset = 0);

		// 8 bit
		void STB	(PPCReg dest, PPCReg src, int offset = 0);
		void STBX	(PPCReg dest, PPCReg a, PPCReg b);
		// 16 bit
		void STH	(PPCReg dest, PPCReg src, int offset = 0);
		void STHBRX (PPCReg dest, PPCReg src, PPCReg offset);
		// 32 bit
		void STW	(PPCReg dest, PPCReg src, int offset = 0);
		void STWU	(PPCReg dest, PPCReg src, int offset = 0);
		void STWBRX (PPCReg dest, PPCReg src, PPCReg offset);
		// 64 bit
		void STD	(PPCReg dest, PPCReg src, int offset = 0);

		// sign
		void EXTSB	(PPCReg dest, PPCReg src);
		void EXTSH	(PPCReg dest, PPCReg src);

		// 
		void RLWINM (PPCReg dest, PPCReg src, int shift, int start, int end);

		// Shift Instructions
		void SRAW	(PPCReg dest, PPCReg src, PPCReg shift);
		void SRAWI	(PPCReg dest, PPCReg src, unsigned short imm);
		
		void SLW	(PPCReg dest, PPCReg src, PPCReg shift);
		void SLWI	(PPCReg dest, PPCReg src, unsigned short imm);

		void SRW	(PPCReg dest, PPCReg src, PPCReg shift);
		void SRWI	(PPCReg dest, PPCReg src, unsigned short imm);
		
		void ROTRW	(PPCReg dest, PPCReg src, PPCReg shift);
		void ROTRWI	(PPCReg dest, PPCReg src, unsigned short imm);

		void ROTLW	(PPCReg dest, PPCReg src, PPCReg shift);
		void ROTLWI	(PPCReg dest, PPCReg src, unsigned short imm);

		// Compare
		enum CONDITION_REGISTER{
			CR0,
			CR1,
			CR2,
			CR3,
			CR4,
			CR5,
			CR6,
			CR7
		};

		void CMPLI	(PPCReg dest, unsigned short imm);	
		void CMPI	(PPCReg dest, unsigned short imm);
		void CMPL	(PPCReg a, PPCReg b, CONDITION_REGISTER cr = CR0);
		void CMP	(PPCReg a, PPCReg b, CONDITION_REGISTER cr = CR0);
		void MFCR	(PPCReg dest);
		void MTCR	(PPCReg dest);

		void ISEL	(PPCReg Rt, PPCReg Ra, PPCReg Rb, CONDITION_REGISTER cr = CR0);

		void Prologue();
		void Epilogue();

		// Debug !
		void Break() {
			Write32(0x0FE00016);
		}

		void MR		(PPCReg to, PPCReg from) {
			OR(to, from, from);
		}

		// Fpu
		void LFS	(PPCReg FRt, PPCReg Ra, unsigned short offset = 0);
		void LFD	(PPCReg FRt, PPCReg Ra, unsigned short offset = 0);
		void SFS	(PPCReg FRt, PPCReg Ra, unsigned short offset = 0);
		void SFD	(PPCReg FRt, PPCReg Ra, unsigned short offset = 0);
		void SaveFloatSwap(PPCReg FRt, PPCReg Ra, PPCReg offset);
		void LoadFloatSwap(PPCReg FRt, PPCReg Ra, PPCReg offset);

		// Fpu move instruction
		void FMR	(PPCReg FRt, PPCReg FRb);
		void FNEG	(PPCReg FRt, PPCReg FRb);
		void FABS	(PPCReg FRt, PPCReg FRb);
		void FNABS	(PPCReg FRt, PPCReg FRb);
		void FCPSGN	(PPCReg FRt, PPCReg FRb);

		// Fpu arith
		void FADD	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FSUB	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FADDS	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FSUBS	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FMUL	(PPCReg FRt, PPCReg FRa, PPCReg FRc);
		void FMULS	(PPCReg FRt, PPCReg FRa, PPCReg FRc);
		void FDIV	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FDIVS	(PPCReg FRt, PPCReg FRa, PPCReg FRb);
		void FSQRT	(PPCReg FRt, PPCReg FRb);
		void FSQRTS	(PPCReg FRt, PPCReg FRb);
		void FSQRTE	(PPCReg FRt, PPCReg FRb);
		void FSQRTES(PPCReg FRt, PPCReg FRb);
		void FRE	(PPCReg FRt, PPCReg FRb);
		void FRES	(PPCReg FRt, PPCReg FRb);

		// Fpu mul add
		void FMADD	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb);
		void FMSUB	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb);
		void FMADDS	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb);
		void FMSUBS	(PPCReg FRt, PPCReg FRa, PPCReg FRc, PPCReg FRb);

		

		void QuickCallFunction(void *func);
	protected:

	};  // class PPCXEmitter


	// You get memory management for free, plus, you can use all the MOV etc functions without
	// having to prefix them with gen-> or something similar.
	class PPCXCodeBlock : public PPCXEmitter
	{
	protected:
		u8 *region;
		size_t region_size;

	public:
		PPCXCodeBlock() : region(NULL), region_size(0) {}
		virtual ~PPCXCodeBlock() { if (region) FreeCodeSpace(); }

		// Call this before you generate any code.
		void AllocCodeSpace(int size)
		{
			region_size = size;
			region = (u8*)AllocateExecutableMemory(region_size);
			SetCodePtr(region);
		}

		// Always clear code space with breakpoints, so that if someone accidentally executes
		// uninitialized, it just breaks into the debugger.
		void ClearCodeSpace() 
		{
			// x86/64: 0xCC = breakpoint
			memset(region, 0xCC, region_size);
			ResetCodePtr();
		}

		// Call this when shutting down. Don't rely on the destructor, even though it'll do the job.
		void FreeCodeSpace()
		{
			region = NULL;
			region_size = 0;
		}

		bool IsInSpace(u8 *ptr)
		{
			return ptr >= region && ptr < region + region_size;
		}

		// Cannot currently be undone. Will write protect the entire code region.
		// Start over if you need to change the code (call FreeCodeSpace(), AllocCodeSpace()).
		void WriteProtect()
		{
			//WriteProtectMemory(region, region_size, true);
		}
		void UnWriteProtect()
		{
			//UnWriteProtectMemory(region, region_size, false);
		}

		void ResetCodePtr()
		{
			SetCodePtr(region);
		}

		size_t GetSpaceLeft() const
		{
			return region_size - (GetCodePtr() - region);
		}

		u8 *GetBasePtr() {
			return region;
		}

		size_t GetOffset(u8 *ptr) {
			return ptr - region;
		}
	};

}  // namespace

#endif // _DOLPHIN_INTEL_CODEGEN_
