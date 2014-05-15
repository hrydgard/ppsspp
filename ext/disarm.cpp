/* disarm -- a simple disassembler for ARM instructions
 * (c) 2000 Gareth McCaughan
 *
 * This file may be distributed and used freely provided:
 * 1. You do not distribute any version that lacks this
 *    copyright notice (exactly as it appears here, extending
 *    from the start to the end of the C-language comment
 *    containing these words)); and,
 * 2. If you distribute any modified version, its source
 *    contains a clear description of the ways in which
 *    it differs from the original version, and a clear
 *    indication that the changes are not mine.
 * There is no restriction on your permission to use and
 * distribute object code or executable code derived from
 * this.
 *
 * The original version of this file (or perhaps a later
 * version by the original author) may or may not be
 * available at http://web.ukonline.co.uk/g.mccaughan/g/software.html .
 *
 * Share and enjoy!    -- g
 */

/* (*This* comment is NOT part of the notice mentioned in the
 * distribution conditions above.)
 *
 * The bulk of this code was ripped brutally from the middle
 * of a much more interesting piece of software whose purpose
 * is to disassemble object files in the format known as AOF;
 * it's quite clever at spotting blocks of non-code embedded
 * in code, identifying labels, and so on.
 *
 * This program, on the other hand, is very much simpler.
 * It simply disassembles one instruction at a time. Some
 * traces of the original purpose can be seen here and there.
 * You might want to make this do a two-phase disassembly,
 * adding labels etc the second time around. I've made this
 * work by loading the whole file into memory first, partly
 * because that makes a two-pass approach easier.
 *
 * One word of warning: I believe that the syntax this program
 * uses for the MSR instruction is now obsolete.
 *
 * Usage:
 *   disarm <filename> <base-address>
 * will disassemble every word in <filename>.
 *
 * <base-address> should be something understood by strtol.
 * So you can get hex (which is probably what you want)
 * by prefixing "0x".
 *
 * The -r option will byte-reverse each word before it's
 * disassembled.
 *
 * The code is rather unmaintainable. I'm sorry.
 *
 * Changes since original release:
 *   ????-??-?? v0.00 Initial release.
 *   2007-09-02 v0.11 Change %X to %lX in a format string.
 *                    (Thanks to Vincent Zweije for reporting this.)
 */

#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtautological-compare" //used to avoid warning, force compiler to accept it.
#pragma GCC diagnostic ignored "-Wstring-plus-int"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/basictypes.h"
#include "Common/ArmEmitter.h"

static const char *CCFlagsStr[] = {
	"EQ", // Equal
	"NEQ", // Not equal
	"CS", // Carry Set
	"CC", // Carry Clear
	"MI", // Minus (Negative)
	"PL", // Plus
	"VS", // Overflow
	"VC", // No Overflow
	"HI", // Unsigned higher
	"LS", // Unsigned lower or same
	"GE", // Signed greater than or equal
	"LT", // Signed less than
	"GT", // Signed greater than
	"LE", // Signed less than or equal
	"", // Always (unconditional) 14
};

int GetVd(uint32_t op, bool quad = false, bool dbl = false) {
	int val;
	if (!quad && !dbl) {
		val = ((op >> 22) & 1) | ((op >> 11) & 0x1E);
	} else {
		val = ((op >> 18) & 0x10) | ((op >> 12) & 0xF);
	}
	if (quad)
		val >>= 1;
	return val;
}

int GetVn(uint32_t op, bool quad = false, bool dbl = false) {
	int val;
	if (!quad && !dbl) {
		val = ((op >> 7) & 1) | ((op >> 15) & 0x1E);
	} else {
		val = ((op >> 16) & 0xF) | ((op >> 3) & 0x10);
	}
	if (quad)
		val >>= 1;
	return val;
}

int GetVm(uint32_t op, bool quad = false, bool dbl = false) {
	int val;
	if (!quad && !dbl) {
		val = ((op >> 5) & 1) | ((op << 1) & 0x1E);
	} else {
		val = ((op >> 1) & 0x10) | (op & 0xF);
	}
	if (quad)
		val >>= 1;
	return val;
}


// Modern VFP disassembler, written entirely separately because I can't figure out the old stuff :P
// Horrible array of hacks but hey. Can be cleaned up later.

bool DisasmVFP(uint32_t op, char *text) {
#if defined(ANDROID) && defined(_M_IX86)
	// Prevent linking errors with ArmEmitter which I've excluded on x86 android.
	strcpy(text, "ARM disasm not available");
#else
	const char *cond = CCFlagsStr[op >> 28];
	switch ((op >> 24) & 0xF) {
	case 0xC:
		// VLDMIA/VSTMIA
		{
			bool single_reg = ((op >> 8) & 0xF) == 10;
			int freg = ((op >> 11) & 0x1E) | ((op >> 22) & 1);
			int base = (op >> 16) & 0xF;
			bool load = (op >> 20) & 1;
			bool writeback = (op >> 21) & 1;
			int numregs = op & 0xF;
			bool add = (op >> 23) & 1;
			if (add && writeback && load && base == 13) {
				if (single_reg)
					sprintf(text, "VPOP%s {s%i-s%i}", cond, freg, freg-1+numregs);
				else
					sprintf(text, "VPOP%s {d%i-d%i}", cond, freg, freg-1+(numregs/2));

				return true;
			}
			if (single_reg)
				sprintf(text, "%s%s r%i%s, {s%i-s%i}", load ? "VLDMIA" : "VSTMIA", cond, base, writeback ? "!":"", freg, freg-1+numregs);
			else
				sprintf(text, "%s%s r%i%s, {d%i-d%i}", load ? "VLDMIA" : "VSTMIA", cond, base, writeback ? "!":"", freg, freg-1+(numregs/2));

			return true;
		}
	case 0xD:
		// VLDR/VSTR/VLDMDB/VSTMDB
		{
			bool single_reg = ((op >> 8) & 0xF) == 10;
			int freg = ((op >> 11) & 0x1E) | ((op >> 22) & 1);
			int base = (op >> 16) & 0xF;
			bool load = (op >> 20) & 1;
			bool add = (op >> 23) & 1;
			bool writeback = (op >> 21) & 1;
			if (writeback) { // Multiple
				int numregs = op & 0xF;
				if (!add && !load && base == 13) {
					if (single_reg)
						sprintf(text, "VPUSH%s {s%i-s%i}", cond, freg, freg-1+numregs);
					else
						sprintf(text, "VPUSH%s {d%i-d%i}", cond, freg, freg-1+(numregs/2));

					return true;
				}

				if (single_reg)
					sprintf(text, "%s%s r%i, {s%i-s%i}", load ? "VLDMDB" : "VSTMDB", cond, base, freg, freg-1+numregs);
				else
					sprintf(text, "%s%s r%i, {d%i-d%i}", load ? "VLDMDB" : "VSTMDB", cond, base, freg, freg-1+(numregs/2));
			} else {
				int offset = (op & 0xFF) << 2;
				if (!add) offset = -offset;
				sprintf(text, "%s%s s%i, [r%i, #%i]", load ? "VLDR" : "VSTR", cond, freg, base, offset);
			}

			return true;
		}

	case 0xE:
		{
			switch ((op >> 20) & 0xF) {
			case 0xE: // VMSR
				if ((op & 0xFFF) != 0xA10)
					break;
				sprintf(text, "VMSR%s r%i", cond, (op >> 12) & 0xF);
				return true;
			case 0xF: // VMRS
				if ((op & 0xFFF) != 0xA10)
					break;
				if (op == 0xEEF1FA10) {
					sprintf(text, "VMRS%s APSR", cond);
				} else {
					sprintf(text, "VMRS%s r%i", cond, (op >> 12) & 0xF);
				}
				return true;
			default:
				break;
			}

			if (((op >> 19) & 0x7) == 0x7) {
				// VCVT
				sprintf(text, "VCVT ...");
				return true;
			}

			int part1 = ((op >> 23) & 0x1F);
			int part2 = ((op >> 9) & 0x7) ;
			int part3 = ((op >> 20) & 0x3) ;
			if (part3 == 3 && part2 == 5 && part1 == 0x1D && (op & (1<<6))) {
				// VMOV, VCMP
				int vn = GetVn(op);
				if (vn != 1 && vn != 3) {
					int vm = GetVm(op);
					int vd = GetVd(op);

					const char *name = "VMOV";
					if (op & 0x40000)
						name = (op & 0x80) ? "VCMPE" : "VCMP";
					sprintf(text, "%s%s s%i, s%i", name, cond, vd, vm);
					return true;
				}
			}

			// Arithmetic (buggy!)

			bool quad_reg = (op >> 6) & 1;
			bool double_reg = (op >> 8) & 1;
			int opnum = -1;
			int opc1 = (op >> 20) & 0xFB;
			int opc2 = (op >> 4) & 0xAC;
			for (int i = 0; i < 16; i++) {
				// What the hell?
				int fixed_opc2 = opc2;
				if (!(ArmGen::VFPOps[i][0].opc2 & 0x8))
					fixed_opc2 &= 0xA7;
				if (ArmGen::VFPOps[i][0].opc1 == opc1 && ArmGen::VFPOps[i][0].opc2 == fixed_opc2) {
					opnum = i;
					break;
				}
			}
			if (opnum < 0)
				return false;
			char c = double_reg ? 'd' : 's';
			switch (opnum) {
			case 8:
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
				{
					quad_reg = false;
					int vd = GetVd(op, quad_reg, double_reg);
					int vn = GetVn(op, quad_reg, true);
					int vm = GetVm(op, quad_reg, double_reg);
					if (opnum == 8 && vn == 0x11)
						opnum += 3;
					sprintf(text, "%s%s %c%i, %c%i", ArmGen::VFPOpNames[opnum], cond, c, vd, c, vm);
					return true;
				}
			default:
				{
					quad_reg = false;
					int vd = GetVd(op, quad_reg, double_reg);
					int vn = GetVn(op, quad_reg, double_reg);
					int vm = GetVm(op, quad_reg, double_reg);
					sprintf(text, "%s%s %c%i, %c%i, %c%i", ArmGen::VFPOpNames[opnum], cond, c, vd, c, vn, c, vm);
					return true;
				}
			}
			return true;
		}
		break;
	}
#endif
	return false;
}

static const char *GetSizeString(int sz) {
	switch (sz) {
	case 0:
		return "8";
	case 1:
		return "16";
	case 2:
		return "32";
	case 3:
		return "64";
	default:
		return "(err)";
	}
}

static const char *GetISizeString(int sz) {
	switch (sz) {
	case 0:
		return "i8";
	case 1:
		return "i16";
	case 2:
		return "i32";
	case 3:
		return "i64";
	default:
		return "(err)";
	}
}

static int GetRegCount(int type) {
	switch (type) {
	case 7: return 1;
	case 10: return 2;
	case 6: return 3;
	case 4: return 4;
	default:
		return 0;
	}
}

// VLD1 / VST1
static bool DisasmNeonLDST(uint32_t op, char *text) {
	bool load = (op >> 21) & 1;
	int Rn = (op >> 16) & 0xF;
	int Rm = (op & 0xF);
	int Vd = GetVd(op, false, true);
	int sz = (op >> 6) & 3;
	int regCount = GetRegCount((op >> 8) & 0xF);

	int startReg = Vd;
	int endReg = Vd + regCount - 1;

	if (startReg == endReg)
		sprintf(text, "V%s1.%s {d%i}, [r%i]", load ? "LD" : "ST", GetSizeString(sz), startReg, Rn);
	else
		sprintf(text, "V%s1.%s {d%i-d%i}, [r%i]", load ? "LD" : "ST", GetSizeString(sz), startReg, endReg, Rn);

	return true;
}

static bool DisasmNeonF3(uint32_t op, char *text) {
	sprintf(text, "NEON F3");
	return true;
}

static bool DisasmNeonF2F3(uint32_t op, char *text) {
	sprintf(text, "NEON F2");
	if (((op >> 20) & 0xFFC) == 0xF20 || ((op >> 20) & 0xFFC) == 0xF30) {
		bool quad = ((op >> 6) & 1);
		int size = (op >> 20) & 3;
		int type = (op >> 8) & 0xF;
		char r = quad ? 'q' : 'd';
		const char *opname = "(unk)";
		switch ((op >> 20) & 0xFF) {
		case 0x20:
			if (op & 0x10)
				opname = "MLA";
			else
				opname = "ADD";
			break;
		case 0x22:
			if (op & 0x10)
				opname = "MLS";
			else
				opname = "ADD";
			break;
		case 0x31:
			if (op & 0x100)
				opname = "MLS";
			else
				opname = "SUB";
			break;
		case 0x30:
			opname = "MUL";
			break;
		}
		const char *szname = GetISizeString(size);
		if (type == 0xD)
			szname = "f32";
		sprintf(text, "V%s.%s %c%i, %c%i, %c%i", opname, szname, r, GetVd(op, quad, true), r, GetVn(op, quad, true), r, GetVm(op, quad, true));
	}
	return true;
}

static bool DisasmNeon(uint32_t op, char *text) {
	switch (op >> 24) {
	case 0xF4:
		return DisasmNeonLDST(op, text);
	case 0xF2:
	case 0xF3:
		return DisasmNeonF2F3(op, text);
	}
	return false;
}



























typedef unsigned int word;
typedef unsigned int address;
typedef unsigned int addrdiff;
#define W(x) ((word*)(x))

#define declstruct(name) typedef struct name s##name, * p##name
#define defstruct(name) struct name
#define defequiv(new,old) typedef struct old s##new, * p##new

declstruct(DisOptions);
declstruct(Instruction);

typedef enum {
  target_None,		/* instruction doesn't refer to an address */
  target_Data,		/* instruction refers to address of data */
  target_FloatS,	/* instruction refers to address of single-float */
  target_FloatD,	/* instruction refers to address of double-float */
  target_FloatE,	/* blah blah extended-float */
  target_FloatP,	/* blah blah packed decimal float */
  target_Code,		/* instruction refers to address of code */
  target_Unknown	/* instruction refers to address of *something* */
} eTargetType;

defstruct(Instruction) {
  char text[128];	/* the disassembled instruction */
  int undefined;	/* non-0 iff it's an undefined instr */
  int badbits;		/* non-0 iff something reserved has the wrong value */
  int oddbits;		/* non-0 iff something unspecified isn't 0 */
  int is_SWI;		/* non-0 iff it's a SWI */
  word swinum;		/* only set for SWIs */
  address target;	/* address instr refers to */
  eTargetType target_type;	/* and what we expect to be there */
  int offset;		/* offset from register in LDR or STR or similar */
  char * addrstart;	/* start of address part of instruction, or 0 */
};

#define disopt_SWInames		1	/* use names, not &nnnn */
#define disopt_CommaSpace	2	/* put spaces after commas */
#define disopt_FIXS		4	/* bogus FIX syntax for ObjAsm */
#define disopt_ReverseBytes	8	/* byte-reverse words first */

defstruct(DisOptions) {
  word flags;
  const char * * regnames;	/* pointer to 16 |char *|s: register names */
};

static pInstruction instr_disassemble(word, address, pDisOptions);

#define INSTR_grok_v4

/* Preprocessor defs you can give to affect this stuff:
 * INSTR_grok_v4   understand ARMv4 instructions (halfword & sign-ext LDR/STR)
 * INSTR_new_msr   be prepared to produce new MSR syntax if asked
 * The first of these is supported; the second isn't.
 */

/* Some important single-bit fields. */

#define Sbit	(1<<20)	/* set condition codes (data processing) */
#define Lbit	(1<<20)	/* load, not store (data transfer) */
#define Wbit	(1<<21)	/* writeback (data transfer) */
#define Bbit	(1<<22)	/* single byte (data transfer, SWP) */
#define Ubit	(1<<23)	/* up, not down (data transfer) */
#define Pbit	(1<<24)	/* pre-, not post-, indexed (data transfer) */
#define Ibit	(1<<25)	/* non-immediate (data transfer) */
			/* immediate (data processing) */
#define SPSRbit	(1<<22)	/* SPSR, not CPSR (MRS, MSR) */

/* Some important 4-bit fields. */

#define RD(x)	((x)<<12)	/* destination register */
#define RN(x)	((x)<<16)	/* operand/base register */
#define CP(x)	((x)<<8)	/* coprocessor number */
#define RDbits	RD(15)
#define RNbits	RN(15)
#define CPbits	CP(15)
#define RD_is(x)	((instr&RDbits)==RD(x))
#define RN_is(x)	((instr&RNbits)==RN(x))
#define CP_is(x)	((instr&CPbits)==CP(x))

/* A slightly efficient way of telling whether two bits are the same
 * or not. It's assumed that a<b.
 */
#define BitsDiffer(a,b) ((instr^(instr>>(b-a)))&(1<<a))

/* op = append(op,ip) === op += sprintf(op,"%s",ip),
 * except that it's faster.
 */
static char * append(char * op, const char *ip) {
  char c;
  while ((c=*ip++)!=0) *op++=c;
  return op;
}

/* op = hex8(op,w) === op += sprintf(op,"&%08lX",w), but faster.
 */
static char * hex8(char * op, word w) {
  int i;
  *op++='&';
  for (i=28; i>=0; i-=4) *op++ = "0123456789ABCDEF"[(w>>i)&15];
  return op;
}

/* op = reg(op,'x',n) === op += sprintf(op,"x%lu",n&15).
 */
static char * reg(char * op, char c, word n) {
  *op++=c;
  n&=15;
  if (n>=10) { *op++='1'; n+='0'-10; } else n+='0';
  *op++=(char)n;
  return op;
}

/* op = num(op,n) appends n in decimal or &n in hex
 * depending on whether n<100. It's assumed that n>=0.
 */
static char * num(char * op, word w) {
  if (w>=100) {
    int i;
    word t;
    *op++='&';
    for (i=28; (t=(w>>i)&15)==0; i-=4) ;
    for (; i>=0; i-=4) *op++ = "0123456789ABCDEF"[(w>>i)&15];
  }
  else {
    /* divide by 10. You can prove this works by exhaustive search. :-) */
    word t = w-(w>>2); t=(t+(t>>4)) >> 3;
    { word u = w-10*t;
      if (u==10) { u=0; ++t; }
      if (t) *op++=(char)(t+'0');
      *op++=(char)(u+'0');
    }
  }
  return op;
}

/* instr_disassemble
 * Disassemble a single instruction.
 *
 * args:   instr   a single ARM instruction
 *         addr    the address it's presumed to have come from
 *         opts    cosmetic preferences for our output
 *
 * reqs:   opts must be filled in right. In particular, it must contain
 *         a list of register names.
 *
 * return: a pointer to a structure containing the disassembled instruction
 *         and some other information about it.
 *
 * This is basically a replacement for the SWI Debugger_Disassemble,
 * but it has the following advantages:
 *
 *   + it's 3-4 times as fast
 *   + it's better at identifying undefined instructions,
 *     and instructions not invariant under { disassemble; ObjAsm; }
 *   + it provides some other useful information as well
 *   + its output syntax is the same as ObjAsm's input syntax
 *     (where possible)
 *   + it doesn't disassemble FIX incorrectly unless you ask it to
 *   + it's more configurable in some respects
 *
 * It also has the following disadvantages:
 *
 *   - it increases the size of ObjDism
 *   - it doesn't provide so many `helpful' usage comments etc
 *   - it's less configurable in some respects
 *   - it doesn't (yet) know about ARMv4 instructions
 *
 * This function proceeds in two phases. The first is very simple:
 * it works out what sort of instruction it's looking at and sets up
 * three strings:
 *   - |mnemonic|  (the basic mnemonic: LDR or whatever)
 *   - |flagchars| (things to go after the cond code: B or whatever)
 *   - |format|    (a string describing how to display the instruction)
 * The second phase consists of interpreting |format|, character by
 * character. Some characters (e.g., letters) just mean `append this
 * character to the output string'; some mean more complicated things
 * like `append the name of the register whose number is in bits 12..15'
 * or, worse, `append a description of the <op2> field'.
 *
 * I'm afraid the magic characters in |format| are rather arbitrary.
 * One criterion in choosing them was that they should form a contiguous
 * subrange of the character set! Sorry.
 *
 * Things I still want to do:
 *
 *   - more configurability?
 *   - make it much faster, if possible
 *   - make it much smaller, if possible
 *
 * Format characters:
 *
 *   \01..\05 copro register number from nybble (\001 == nybble 0, sorry)
 *   $        SWI number
 *   %        register set for LDM/STM (takes note of bit 22 for ^)
 *   &        address for B/BL
 *   '        ! if bit 21 set, else nothing (mnemonic: half a !)
 *   (        #regs for SFM (bits 22,15 = fpn, assumed already tweaked)
 *   )        copro opcode in bits 20..23 (for CDP)
 *   *        op2 (takes note of bottom 12 bits, and bit 25)
 *   +        FP register or immediate value: bits 0..3
 *   ,        comma or comma-space
 *   -        copro extra info in bits 5..7 preceded by , omitted if 0
 *   .        address in ADR instruction
 *   /        address for LDR/STR (takes note of bit 23 & reg in bits 16..19)
 *   0..4     register number from nybble
 *   5..9     FP register number from nybble
 *   :        copro opcode in bits 21..23 (for MRC/MCR)
 *   ;        copro number in bits 8..11
 *
 *  ADDED BY HRYDGARD:
 *   ^        16-bit immediate
 *   >        5-bit immediate at 11..7 (lsb)
 *   <        5-bit immediate at 20..16 with +1 or -lsb if bit 6 set
 *
 * NB that / takes note of bit 22, too, and does its own ! when
 * appropriate.
 *
 * On typical instructions this seems to take about 100us on my ARM6;
 * that's about 3000 cycles, which seems grossly excessive. I'm not
 * sure where all those cycles are being spent. Perhaps it's possible
 * to make it much, much faster. Most of this time is spent on phase 2.
 */

extern pInstruction
instr_disassemble(word instr, address addr, pDisOptions opts) {
  static char         flagchars[4];
  static sInstruction result;
  const char * mnemonic  = 0;
  char *       flagp     = flagchars;
  const char * format    = 0;
  word         fpn;
  eTargetType  poss_tt   = target_None;
#ifdef INSTR_grok_v4
  int          is_v4     = 0;
#endif

  /* PHASE 0. Set up default values for |result|. */

  if (opts->flags & disopt_ReverseBytes) {
    instr = ((instr & 0xFF00FF00) >> 8) | ((instr & 0x00FF00FF) << 8);
    instr = (instr >> 16) | (instr << 16);
  }

  fpn = ((instr>>15)&1) + ((instr>>21)&2);

  result.undefined = 0;
  result.badbits = 0;
  result.oddbits = 0;
  result.is_SWI = 0;
  result.target_type = target_None;
  result.offset = 0x80000000;
  result.addrstart = 0;

  /* PHASE 1. Decode and classify instruction. */

  switch ((instr>>24)&15) {
    case 0:
      /* multiply or data processing, or LDRH etc */
      if ((instr&(15<<4))!=(9<<4)) goto lMaybeLDRHetc;
      /* multiply */
      if (instr&(1<<23)) {
        /* int multiply */
        mnemonic = "UMULL\0UMLAL\0SMULL\0SMLAL" + 6*((instr>>21)&3);
        format = "3,4,0,2";
      }
      else {
        if (instr&(1<<22)) goto lUndefined;	/* "class C" */
        /* short multiply */
        if (instr&(1<<21)) {
          mnemonic = "MLA";
          format   = "4,0,2,3";
        }
        else {
          mnemonic = "MUL";
          format   = "4,0,2";
        }
      }
      if (instr&Sbit) *flagp++='S';
      break;
    case 1:
			if ((instr & 0x0FFFFFF0) == ((18 << 20) | (0xFFF << 8) | (1 << 4))) {
				mnemonic = "B";
				format = "0";
				break;
			} else if ((instr & 0x0FFFFFF0) == 0x012FFF30) {
				mnemonic = "BL";
				format = "0";
				break;
			}
    case 3:
			if (instr >> 24 == 0xF3) {
				if (!DisasmNeon(instr, result.text)) {
					goto lUndefined;
					break;
				}
				result.undefined = 0;
				return &result;
			}
			/* SWP or MRS/MSR or data processing */
			// hrydgard addition: MOVW/MOVT
			if ((instr & 0x0FF00000) == 0x03000000) {
				mnemonic = "MOVW";
				format = "3,^";
				break;
			}
			else if ((instr & 0x0FF00000) == 0x03400000) {
				mnemonic = "MOVT";
				format = "3,^";
				break;
			}
			else if ((instr&0x02B00FF0)==0x00000090) {
        /* SWP */
        mnemonic = "SWP";
        format   = "3,0,[4]";
        if (instr&Bbit) *flagp++='B';
        break;
      }
      else if ((instr&0x02BF0FFF)==0x000F0000) {
        /* MRS */
        mnemonic = "MRS";
        format   = (instr&SPSRbit) ? "3,SPSR" : "3,CPSR";
        break;
      }
      else if ((instr&0x02BFFFF0)==0x0029F000) {
        /* MSR psr<P=0/1...>,Rs */
        mnemonic = "MSR";
        format   = (instr&SPSRbit) ? "SPSR,0" : "CPSR,0";
        break;
      }
      else if ((instr&0x00BFF000)==0x0028F000) {
        /* MSR {C,S}PSR_flag,op2 */
        mnemonic = "MSR";
        format   = (instr&SPSRbit) ? "SPSR_flg,*" : "CPSR_flg,*";
        if (!(instr&Ibit) && (instr&(15<<4)))
#ifdef INSTR_grok_v4
          goto lMaybeLDRHetc;
#else
          goto lUndefined;	/* shifted reg in MSR illegal */
#endif
        break;
      }
      /* fall through here */
lMaybeLDRHetc:
#ifdef INSTR_grok_v4
      if ((instr&(14<<24))==0
          && ((instr&(9<<4))==(9<<4))) {
        /* Might well be LDRH or similar. */
        if ((instr&(Wbit+Pbit))==Wbit) goto lUndefined;	/* "class E", case 1 */
        if ((instr&(Lbit+(1<<6)))==(1<<6)) goto lUndefined;	/* STRSH etc */
        mnemonic = "STR\0LDR" + ((instr&Lbit) >> 18);
        if (instr&(1<<6)) *flagp++='S';
        *flagp++ = (instr&(1<<5)) ? 'B' : 'H';
        format = "3,/";
        /* aargh: */
        if (!(instr&(1<<22))) instr |= Ibit;
        is_v4=1;
        break;
      }
#endif
    case 2:
			if (instr >> 24 == 0xF2) {
				if (!DisasmNeon(instr, result.text)) {
					goto lUndefined;
					break;
				}
				result.undefined = 0;
				return &result;
			}
			/* data processing */
      { word op21 = instr&(15<<21);
        if ((op21==(2<<21) || (op21==(4<<21)))			/* ADD or SUB */
            && ((instr&(RNbits+Ibit+Sbit))==RN(15)+Ibit)	/* imm, no S */
            /*&& ((instr&(30<<7))==0 || (instr&3))*/) {		/* normal rot */
          /* ADD ...,pc,#... or SUB ...,pc,#...: turn into ADR */
          mnemonic = "ADR";
          format   = "3,.";
          if ((instr&(30<<7))!=0 && !(instr&3)) result.oddbits=1;
          break;
        }
        mnemonic = "AND\0EOR\0SUB\0RSB\0ADD\0ADC\0SBC\0RSC\0"
                   "TST\0TEQ\0CMP\0CMN\0ORR\0MOV\0BIC\0MVN" /* \0 */
                   + (op21 >> 19);
        /* Rd needed for all but TST,TEQ,CMP,CMN (8..11) */
        /* Rn needed for all but MOV,MVN (13,15) */
             if (op21 < ( 8<<21)) format = "3,4,*";
        else if (op21 < (12<<21)) {
          format = "4,*";
          if (instr&RDbits) {
            if ((instr&Sbit) && RD_is(15))
              *flagp++='P';
            else result.oddbits=1;
          }
          if (!(instr&Sbit)) goto lUndefined;	/* CMP etc, no S bit */
        }
        else if (op21 & (1<<21)) {
          format = "3,*";
          if (instr&RNbits) result.oddbits=1;
        }
        else format = "3,4,*";
        if (instr&Sbit && (op21<(8<<21) || op21>=(12<<21))) *flagp++='S';
      }
      break;
    case 4:
			if (instr >> 24 == 0xF4) {
				if (!DisasmNeon(instr, result.text)) {
					goto lUndefined;
					break;
				}
				result.undefined = 0;
				return &result;
			}
		case 5:
    case 6:
    case 7:
      /* STR/LDR/BFI/BFC/UBFX/SBFX or undefined */
      if ((instr&Ibit) && (instr&(1<<4))) {
        switch ((instr >> 21) & 7) {
        case 5:
        case 7:
          /* SBFX/UBFX */
          if (((instr>>4) & 7) != 5) {
            goto lUndefined;
          }
          mnemonic = (instr & (1 << 22)) ? "UBFX" : "SBFX";
          format = "3,0,>,<";
          break;
        case 6:
          /* BFI/BFC */
          if (((instr>>4) & 7) != 1) {
            goto lUndefined;
          }
          if ((instr & 15) == 15) {
            mnemonic = "BFC";
            format = "3,>,<";
          } else {
            mnemonic = "BFI";
            format = "3,0,>,<";
          }
          break;
        default:
          goto lUndefined;    /* "class A" */
        }
      } else {
        mnemonic = "STR\0LDR"  + ((instr&Lbit) >> 18);
        format   = "3,/";
        if (instr&Bbit) *flagp++='B';
        if ((instr&(Wbit+Pbit))==Wbit) *flagp++='T';
        poss_tt = target_Data;
      }
      break;
    case 8:
    case 9:
      /* STM/LDM */
      mnemonic = "STM\0LDM" + ((instr&Lbit) >> 18);
      if (RN_is(13)) {
        /* r13, so treat as stack */
        word x = (instr&(3<<23)) >> 22;
        if (instr&Lbit) x^=6;
        { const char * foo = "EDEAFDFA"+x;
          *flagp++ = *foo++;
          *flagp++ = *foo;
        }
      }
      else {
        /* not r13, so don't treat as stack */
        *flagp++ = (instr&Ubit) ? 'I' : 'D';
        *flagp++ = (instr&Pbit) ? 'B' : 'A';
      }
      format = "4',%";
      break;
    case 10:
    case 11:
      /* B or BL */
      mnemonic = "B\0BL"+((instr&(1<<24))>>23);
      format   = "&";
      break;
    case 12:
    case 13:
    case 14:  // FPU
			{
				if (!DisasmVFP(instr, result.text)) {
					goto lUndefined;
					break;
				}
				result.undefined = 0;
				return &result;
			}
      break;
    case 15:
      /* SWI */
      mnemonic = "SWI";
      format   = "$";
      break;
/* Nasty hack: this is code that won't be reached in the normal
 * course of events, and after the last case of the switch is a
 * convenient place for it.
 */
lUndefined:
      strcpy(result.text, "Undefined instruction");
      result.undefined = 1;
      return &result;
  }
  *flagp=0;

  /* PHASE 2. Produce string. */

  { char * op = result.text;

    /* 2a. Mnemonic. */

    op = append(op,mnemonic);

    /* 2b. Condition code. */

    { word cond = instr>>28;
      if (cond!=14) {
        const char * ip = "EQNECSCCMIPLVSVCHILSGELTGTLEALNV"+2*cond;
        *op++ = *ip++;
        *op++ = *ip;
      }
    }

    /* 2c. Flags. */

    { const char * ip = flagchars;
      while (*ip) *op++ = *ip++;
    }

    /* 2d. A tab character. */

    *op++ = '\t';

    /* 2e. Other stuff, determined by format string. */

    { const char * ip = format;
      char c;

      const char * * regnames = opts->regnames;
      word     oflags   = opts->flags;

      while ((c=*ip++) != 0) {
        switch(c) {
					case '^':  // hrydgard addition
						{
							unsigned short imm16 = ((instr & 0x000F0000) >> 4) | (instr & 0x0FFF);
							op += sprintf(op, "%04x", imm16);
						}
						break;
          case '$':
            result.is_SWI = 1;
            result.swinum = instr&0x00FFFFFF;
            result.addrstart = op;
            op += sprintf(op, "&%X", result.swinum);
            break;
          case '%':
            *op++='{';
            { word w = instr&0xFFFF;
              int i=0;
              while (w) {
                int j;
                while (!(w&(1ul<<i))) ++i;
                for (j=i+1; w&(1ul<<j); ++j) ;
                --j;
                /* registers [i..j] */
                op = append(op, regnames[i]);
                if (j-i) {
                  *op++ = (j-i>1) ? '-' : ',';
                  op = append(op, regnames[j]);
                }
                i=j; w=(w>>(j+1))<<(j+1);
                if (w) *op++=',';
              }
            }
            *op++='}';
            if (instr&(1<<22)) *op++='^';
            break;
          case '&':
            { address target = (addr+8 + ((((int)instr)<<8)>>6)) & 0x03FFFFFC;
              result.addrstart = op;
              op = hex8(op, target);
              result.target_type = target_Code;
              result.target      = target;
            }
            break;
          case '\'':
lPling:
            if (instr&Wbit) *op++='!';
            break;
          case '(':
            *op++ = (char)('0'+fpn);
            break;
          case ')':
            { word w = (instr>>20)&15;
              if (w>=10) { *op++='1'; *op++=(char)('0'-10+w); }
              else *op++=(char)(w+'0');
            }
            break;
          case '*':
          case '.':
            if (instr&Ibit) {
              /* immediate constant */
              word imm8 = (instr&255);
              word rot  = (instr>>7)&30;
              if (rot && !(imm8&3) && c=='*') {
                /* Funny immediate const. Guaranteed not '.', btw */
                *op++='#'; *op++='&';
                *op++="0123456789ABCDEF"[imm8>>4];
                *op++="0123456789ABCDEF"[imm8&15];
                *op++=',';
                op = num(op, rot);
              }
              else {
                imm8 = (imm8>>rot) | (imm8<<(32-rot));
                if (c=='*') {
                  *op++='#';
                  if (imm8>256 && ((imm8&(imm8-1))==0)) {
                    /* only one bit set, and that later than bit 8.
                     * Represent as 1<<... .
                     */
                    op = append(op,"1<<");
                    { int n=0;
                      while (!(imm8&15)) { n+=4; imm8=imm8>>4; }
                      /* Now imm8 is 1, 2, 4 or 8. */
                      n += (0x30002010 >> 4*(imm8-1))&15;
                      op = num(op, n);
                    }
                  }
                  else {
                    if (((int)imm8)<0 && ((int)imm8)>-100) {
                      *op++='-'; imm8=-(int)imm8;
                    }
                    op = num(op, imm8);
                  }
                }
                else {
                  address a = addr+8;
                  if (instr&(1<<22)) a-=imm8; else a+=imm8;
                  result.addrstart=op;
                  op = hex8(op, a);
                  result.target=a; result.target_type=target_Unknown;
                }
              }
            }
            else {
              /* rotated register */
              const char * rot = "LSL\0LSR\0ASR\0ROR" + ((instr&(3<<5)) >> 3);
              op = append(op, regnames[instr&15]);
              if (instr&(1<<4)) {
                /* register rotation */
                if (instr&(1<<7)) goto lUndefined;
                *op++=','; if (oflags&disopt_CommaSpace) *op++=' ';
                op = append(op,rot); *op++=' ';
                op = append(op,regnames[(instr&(15<<8))>>8]);
              }
              else {
                /* constant rotation */
                word n = instr&(31<<7);
                if (!n) {
                  if (!(instr&(3<<5))) break;
                  else if ((instr&(3<<5))==(3<<5)) {
                    op = append(op, ",RRX");
                    break;
                  }
                  else n=32<<7;
                }
                *op++ = ','; if (oflags&disopt_CommaSpace) *op++=' ';
                op = num(append(append(op,rot)," #"),n>>7);
              }
            }
            break;
          case '+':
            if (instr&(1<<3)) {
              word w = instr&7;
              *op++='#';
              if (w<6) *op++=(char)('0'+w);
              else op = append(op, w==6 ? "0.5" : "10");
            }
            else {
              *op++='f';
              *op++=(char)('0'+(instr&7));
            }
            break;
          case ',':
            *op++=',';
            if (oflags&disopt_CommaSpace) *op++=' ';
            break;
          case '-':
            { word w = instr&(7<<5);
              if (w) {
                *op++=',';
                if (oflags&disopt_CommaSpace) *op++=' ';
                *op++ = (char)('0'+(w>>5));
              }
            }
            break;
          case '/':
            result.addrstart = op;
            *op++='[';
            op = append(op, regnames[(instr&RNbits)>>16]);
            if (!(instr&Pbit)) *op++=']';
            *op++=','; if (oflags&disopt_CommaSpace) *op++=' ';
            /* For following, NB that bit 25 is always 0 for LDC, SFM etc */
            if (instr&Ibit) {
              /* shifted offset */
              if (!(instr&Ubit)) *op++='-';
              /* We're going to transfer to '*', basically. The stupid
               * thing is that the meaning of bit 25 is reversed there;
               * I don't know why the designers of the ARM did that.
               */
              instr ^= Ibit;
              if (instr&(1<<4)) {
#ifdef INSTR_grok_v4
                if (is_v4 && !(instr&(15<<8))) {
                  ip = (instr&Pbit) ? "0]" : "0";
                  break;
                }
#else
                goto lUndefined;	/* LSL r3 forbidden */
#endif
              }
              /* Need a ] iff it was pre-indexed; and an optional ! iff
               * it's pre-indexed *or* a copro instruction,
               * except that FPU operations don't need the !. Bletch.
               */
              if (instr&Pbit) ip="*]'";
              else if (instr&(1<<27)) {
                if (CP_is(1) || CP_is(2)) {
                  if (!(instr&Wbit)) goto lUndefined;
                  ip="*";
                }
                else ip="*'";
              }
              else ip="*";
            }
            else {
              /* immediate offset */
              word offset;
              if (instr&(1<<27)) {
                /* LDF or LFM or similar */
                offset = (instr&255)<<2;
              }
#ifdef INSTR_grok_v4
              else if (is_v4) offset = (instr&15) + ((instr&(15<<8))>>4);
#endif
              else {
                /* LDR or STR */
                offset = instr&0xFFF;
              }
              *op++='#';
              if (!(instr&Ubit)) {
                if (offset) *op++='-';
                else result.oddbits=1;
                result.offset = -(int)offset;
              }
              else result.offset = offset;
              op = num(op, offset);
              if (RN_is(15) && (instr&Pbit)) {
                /* Immediate, pre-indexed and PC-relative. Set target. */
                result.target_type = poss_tt;
                result.target      = (instr&Ubit) ? addr+8 + offset
                                                  : addr+8 - offset;
                if (!(instr&Wbit)) {
                  /* no writeback, either. Use friendly form. */
                  op = hex8(result.addrstart, result.target);
                  break;
                }
              }
              if (instr&Pbit) { *op++=']'; goto lPling; }
              else if (instr&(1<<27)) {
                if (CP_is(1) || CP_is(2)) {
                  if (!(instr&Wbit)) goto lUndefined;
                }
                else goto lPling;
              }
            }
            break;
          case '0': case '1': case '2': case '3': case '4':
            op = append(op, regnames[(instr>>(4*(c-'0')))&15]);
            break;
          case '5': case '6': case '7': case '8': case '9':
            *op++='f';
            *op++=(char)('0' + ((instr>>(4*(c-'5')))&7));
            break;
          case ':':
            *op++ = (char)('0' + ((instr>>21)&7));
            break;
          case ';':
            op = reg(op, 'p', instr>>8);
            break;
          case '>':
            *op++='#';
            op = num(op, (instr >> 7) & 31);
            break;
          case '<':
            *op++='#';
            if (instr & (1 << 6)) {
              op = num(op, ((instr >> 16) & 31) + 1);
            } else {
              op = num(op, ((instr >> 16) & 31) + 1 - ((instr >> 7) & 31));
            }
            break;
          default:
            if (c<=5)
              op = reg(op, 'c', instr >> (4*(c-1)));
            else *op++ = c;
        }
      }
      *op=0;
    }
  }

  /* DONE! */

  return &result;
}

static const char * reg_names[16] = {
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "ip", "sp", "lr", "pc"
};

static sDisOptions options = {
  disopt_CommaSpace,
  reg_names
};

const char *ArmRegName(int r) {
	return reg_names[r];
}

void ArmDis(unsigned int addr, unsigned int w, char *output, bool includeWord) {
	pInstruction instr = instr_disassemble(w, addr, &options);
	char temp[256];
	if (includeWord) {
		sprintf(output, "%08x\t%s", w, instr->text);
	} else {
		sprintf(output, "%s", instr->text);
	}
	if (instr->undefined || instr->badbits || instr->oddbits) {
		if (instr->undefined) sprintf(output, " [undefined instr %08x]", w);
		if (instr->badbits) sprintf(output, " [illegal bits %08x]", w);

		// HUH? LDR and STR gets this a lot
		// strcat(output, " ? (extra bits)");  
		if (instr->oddbits) sprintf(temp, " [unexpected bits %08x]", w), strcat(output, temp);
	}
	// zap tabs
	while (*output) {
		if (*output == '\t')
			*output = ' ';
		output++;
	}
}

#ifdef __clang__
#pragma GCC diagnostic pop
#endif
