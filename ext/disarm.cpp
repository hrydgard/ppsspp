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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
			} else if ((instr & 0x0FFFFFF0) == 0x12FFF30) {
				mnemonic = "BL";
				format = "0";
				break;
			}
    case 3:
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
    case 5:
    case 6:
    case 7:
      /* undefined or STR/LDR */
      if ((instr&Ibit) && (instr&(1<<4))) goto lUndefined;	/* "class A" */
      mnemonic = "STR\0LDR"  + ((instr&Lbit) >> 18);
      format   = "3,/";
      if (instr&Bbit) *flagp++='B';
      if ((instr&(Wbit+Pbit))==Wbit) *flagp++='T';
      poss_tt = target_Data;
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
      /* STC or LDC */
      if (CP_is(1)) {
        /* copro 1: FPU. This is STF or LDF. */
        mnemonic = "STF\0LDF" + ((instr&Lbit) >> 18);
        format   = "8,/";
        *flagp++ = "SDEP"[fpn];
        poss_tt = (eTargetType)(target_FloatS+fpn);
      }
      else if (CP_is(2)) {
        /* copro 2: this is LFM or SFM. */
        mnemonic = "SFM\0LFM" + ((instr&Lbit) >> 18);
        if (!fpn) fpn=4;
        if (RN_is(13) && BitsDiffer(23,24)) {
          if ((instr&255)!=fpn) goto lNonStackLFM;
          /* r13 and U!=P, so treat as stack */
          if (BitsDiffer(20,24)) {
            /* L != P, so FD */
            *flagp++ = 'F'; *flagp++ = 'D';
          }
          else {
            /* L == P, so EA */
            *flagp++ = 'E'; *flagp++ = 'A';
          }
          format = "8,(,[4]'";
        }
        else {
lNonStackLFM:
          /* not r13 or U=P or wrong offset, so don't treat as stack */
          format = "8,(,/";
          poss_tt = target_FloatE;
        }
      }
      else {
        /* some other copro number: STC or LDC. */
        mnemonic = "STC\0LDC" + ((instr&Lbit) >> 18);
        format   = ";,\004,/";
        if (instr&(1<<22)) *flagp++ = 'L';
        poss_tt = target_Unknown;
      }
      break;
    case 14:
      /* CDP or MRC/MCR */
      if (instr&(1<<4)) {
        /* MRC/MCR. */
        if (CP_is(1)) {
          /* copro 1: FPU. */
          if ((instr&Lbit) && RD_is(15)) {
            /* MCR in FPU with Rd=r15: comparison (ugh) */
            if (!(instr&(1<<23))) goto lUndefined;	/* unused operation */
            mnemonic = "CMF\0\0CNF\0\0CMFE\0CNFE" + (5*(instr&(3<<21)) >> 21);
            format   = "9,+";
            if (instr&((1<<19)+(7<<5)))
              result.badbits=1;	/* size,rmode reseved */
          }
          else {
            /* normal FPU MCR/MRC */
            word op20 = instr&(15<<20);
            if (op20>=6<<20) goto lUndefined;
            mnemonic = "FLT\0FIX\0WFS\0RFS\0WFC\0RFC" + (op20>>18);
            if (op20==0) {
              /* FLT instruction */
              format = "9,3";
              { char c = "SDE*"[((instr>>7)&1) + ((instr>>18)&2)];
                if (c=='*') goto lUndefined; else *flagp++=c;
              }
              if (instr&15) result.oddbits=1;	/* Fm and const flag unused */
            }
            else {
              /* not FLT instruction */
              if (instr&((1<<7)+(1<<19)))
                result.badbits=1;	/* size bits reserved */
              if (op20==1<<20) {
                /* FIX instruction */
                format = "3,+";
                if (opts->flags&disopt_FIXS)
                  *flagp++ = "SDEP"[((instr>>7)&1) + ((instr>>18)&2)];
                *flagp++ = "\0PMZ"[(instr&(3<<5))>>5];
                if (instr&(7<<15)) result.oddbits=1;	/* Fn unused */
                if (instr&(1<<3)) result.badbits=1;	/* no immediate consts */
              }
              else {
                /* neither FLT nor FIX */
                format = "3";
                if (instr&(3<<5)) result.badbits=1;	/* rmode reserved */
                if (instr&(15+(7<<15))) result.oddbits=1;/* iFm, Fn unused */
              }
            }
          }
        }
        else {
          /* some other copro number. Not FPU. */
          /* NB that ObjAsm documentation gets MCR and MRC the wrong way round!
           */
          mnemonic = "MCR\0MRC";
          mnemonic += (instr&Lbit) >> 18;
          format = ";,:,3,\005,\001-";
        }
      }
      else {
        /* CDP. */
        if (CP_is(1)) {
          /* copro 1: FPU. */
          mnemonic = /* dyadics: */
                     "ADF\0MUF\0SUF\0RSF\0"
                     "DVF\0RDF\0POW\0RPW\0"
                     "RMF\0FML\0FDV\0FRD\0"
                     "POL\0***\0***\0***\0"
                     /* monadics: */
                     "MVF\0MNF\0ABS\0RND\0"
                     "SQT\0LOG\0LGN\0EXP\0"
                     "SIN\0COS\0TAN\0ASN\0"
                     "ACS\0ATN\0URD\0NRM\0"
                     + ((instr&(15<<20)) >> 18)	/* opcode   -> bits 5432 */
                     + ((instr&(1<<15)) >> 9);	/* monadicP -> bit 6 */
          format = (instr&(1<<15)) ? "8,+" : "8,9,+";
          *flagp++ = "SDE*"[((instr>>7)&1) + ((instr>>18)&2)];
          *flagp++ = "\0PMZ"[(instr&(3<<5))>>5];
          /* NB that foregoing relies on this being the last flag! */
          if (*mnemonic=='*' || *flagchars=='*') goto lUndefined;
        }
        else {
          /* some other copro number. Not FPU. */
          mnemonic = "CDP";
          format   = ";,),\004,\005,\001-";
        }
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
void ArmDis(unsigned int addr, unsigned int w, char *output) {
	pInstruction instr = instr_disassemble(w, addr, &options);
	char temp[256];
	sprintf(output, "%08x\t%s", w, instr->text);
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