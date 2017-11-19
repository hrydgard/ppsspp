#include "stdafx.h"
#include "CArmInstruction.h"
#include "Core/Common.h"
#include "Arm.h"
#include "Core/FileManager.h"
#include <cstddef>

const char ArmConditions[16][3] = {
	"eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le","","nv"
};
const char ArmAddressingModes[4][3] = { "da","ia","db","ib"};
const char ArmShiftModes[4][4] = { "lsl", "lsr", "asr", "ror" };

const char ArmPsrModes[16][5] = {
	"_???",	"_ctl",	"_x",	"_xc",	"_s",	"_sc",	"_sx",	"_sxc",
	"_flg",	"_fc",	"_fx",	"_fxc",	"_fs",	"_fsc",	"_fsx",	""
};

bool CArmInstruction::Load(char *Name, char *Params)
{
	return false;
}

CArmInstruction::CArmInstruction(const tArmOpcode& sourceOpcode, ArmOpcodeVariables& vars)
{
	this->Opcode = sourceOpcode;
	this->Vars = vars;
	arch = Arm.getVersion();
}

int CArmInstruction::getShiftedImmediate(unsigned int num, int& ShiftAmount)
{
	for (int i = 0; i < 32; i+=2)
	{
		unsigned int andval = (0xFFFFFF00 >> i) | (0xFFFFFF00 << (-i & 31));

		if ((num & andval) == 0)	// found it
		{
			ShiftAmount = i;
			return (num << i) | (num >> (-i & 31));
		}
	}
	return -1;
}

void CArmInstruction::setPoolAddress(int64_t address)
{
	int pos = (int) (address-((RamPos+8) & 0xFFFFFFFD));
	if (abs(pos) > 4095)
	{
		Logger::queueError(Logger::Error,L"Literal pool out of range");
		return;
	}

	Vars.Immediate = pos;
}

bool CArmInstruction::Validate()
{
	RamPos = g_fileManager->getVirtualAddress();

	Vars.Opcode.UseNewEncoding = false;
	Vars.Opcode.UseNewType = false;


	if (RamPos & 3)
	{
		Logger::queueError(Logger::Warning,L"Opcode not word aligned");
	}

	if (Vars.Shift.UseShift == true && Vars.Shift.ShiftByRegister == false)
	{
		if (Vars.Shift.ShiftExpression.evaluateInteger(Vars.Shift.ShiftAmount) == false)
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			return false;
		}

		int mode = Vars.Shift.Type;
		int num = Vars.Shift.ShiftAmount;

		if (num == 0 && mode == ARM_SHIFT_LSR) mode = ARM_SHIFT_LSL;
		else if (num == 0 && mode == ARM_SHIFT_ASR) mode = ARM_SHIFT_LSL;
		else if (num == 0 && mode == ARM_SHIFT_ROR) mode = ARM_SHIFT_LSL;
		else if (num == 32 && mode == ARM_SHIFT_LSR) num = 0;
		else if (num == 32 && mode == ARM_SHIFT_ASR) num = 0;
		else if (num == 32 && mode == ARM_SHIFT_LSL)
		{
			Logger::queueError(Logger::Error,L"Invalid shift mode");
			return false;
		} else if (mode == ARM_SHIFT_RRX)
		{
			if (num != 1)
			{
				Logger::queueError(Logger::Error,L"Invalid shift mode");
				return false;
			}
			mode = ARM_SHIFT_ROR;
			num = 0;
		}

		if (num > 32 || num < 0)
		{
			Logger::queueError(Logger::Error,L"Shift amount out of range");
			return false;
		}

		Vars.Shift.FinalType = mode;
		Vars.Shift.FinalShiftAmount = num;
		Vars.Shift.UseFinal = true;
	}

	if (Opcode.flags & ARM_COPOP)
	{
		if (Vars.CopData.CpopExpression.evaluateInteger(Vars.CopData.Cpop) == false)
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			return false;
		}

		if (Vars.CopData.Cpop > 15)
		{
			Logger::queueError(Logger::Error,L"CP Opc number %02X too big",Vars.CopData.Cpop);
			return false;
		}
	}

	if (Opcode.flags & ARM_COPINF)
	{
		if (Vars.CopData.CpinfExpression.evaluateInteger(Vars.CopData.Cpinf) == false)
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			return false;
		}

		if (Vars.CopData.Cpinf > 7)
		{
			Logger::queueError(Logger::Error,L"CP Inf number %02X too big",Vars.CopData.Cpinf);
			return false;
		}
	}

	if (Opcode.flags & ARM_DN)
	{
		Vars.rn = Vars.rd;
	}

	if (Opcode.flags & ARM_DM)
	{
		Vars.rm = Vars.rd;
	}

	if (Opcode.flags & ARM_RDEVEN)
	{
		if (Vars.rd.num & 1)
		{
			Logger::queueError(Logger::Error,L"rd must be even");
			return false;
		}
	}

	bool memoryAdvanced = false;
	if (Opcode.flags & ARM_IMMEDIATE)
	{
		ExpressionValue value = Vars.ImmediateExpression.evaluate();

		switch (value.type)
		{
		case ExpressionValueType::Integer:
			Vars.Immediate = (int) value.intValue;
			break;
		case ExpressionValueType::Float:
			if (!(Opcode.flags & ARM_SHIFT) && !(Opcode.flags & ARM_POOL))
			{
				Logger::queueError(Logger::Error,L"Invalid expression type");
				return false;
			}

			Vars.Immediate = (int) getFloatBits((float)value.floatValue);
			break;
		default:
			Logger::queueError(Logger::Error,L"Invalid expression type");
			return false;
		}

		Vars.OriginalImmediate = Vars.Immediate;
		Vars.negative = false;
		
		g_fileManager->advanceMemory(4);
		memoryAdvanced = true;

		if (Opcode.flags & ARM_SHIFT)	// shifted immediate, eg 4000h
		{
			int temp;
			Vars.negative = false;
			if ((Opcode.flags & ARM_ABS) && Vars.Immediate < 0)
			{
				Vars.Immediate = abs(Vars.Immediate);
				Vars.negative = true;
			}

			if (Opcode.flags & ARM_PCR)
			{
				Vars.Immediate = Vars.Immediate - ((RamPos+8) & ~3);
				if (Vars.Immediate < 0)
				{
					Vars.Opcode.NewEncoding = Opcode.encoding ^ 0xC00000;
					Vars.Opcode.UseNewEncoding = true;
					Vars.Immediate = abs(Vars.Immediate);
				}
			}

			if ((temp = getShiftedImmediate(Vars.Immediate,Vars.Shift.ShiftAmount)) == -1)
			{
				// mov/mvn -> mvn/mov
				if ((Opcode.flags & ARM_OPTIMIZE) && (temp = getShiftedImmediate(~Vars.Immediate,Vars.Shift.ShiftAmount)) != -1)
				{
					if (Opcode.flags & ARM_OPMOVMVN) Vars.Opcode.NewEncoding = Opcode.encoding ^ 0x0400000;
					if (Opcode.flags & ARM_OPANDBIC) Vars.Opcode.NewEncoding = Opcode.encoding ^ 0x1C00000;
					if (Opcode.flags & ARM_OPCMPCMN) Vars.Opcode.NewEncoding = Opcode.encoding ^ 0x0200000;
					Vars.Opcode.UseNewEncoding = true;
				} else {
					Logger::queueError(Logger::Error,L"Invalid shifted immediate %X",Vars.OriginalImmediate);
					return false;
				}
			}
			Vars.Immediate = temp;
		} else if (Opcode.flags & ARM_POOL)
		{
			int temp;

			if ((temp = getShiftedImmediate(Vars.Immediate,Vars.Shift.ShiftAmount)) != -1)
			{
				// interpete ldr= as mov
				Vars.Opcode.NewEncoding = 0x03A00000;
				Vars.Opcode.UseNewEncoding = true;
				Vars.Opcode.NewType = ARM_TYPE5;
				Vars.Opcode.UseNewType = true;
				Vars.Immediate = temp;
			} else if ((temp = getShiftedImmediate(~Vars.Immediate,Vars.Shift.ShiftAmount)) != -1) 
			{
				// interprete ldr= as mvn
				Vars.Opcode.NewEncoding = 0x03E00000;
				Vars.Opcode.UseNewEncoding = true;
				Vars.Opcode.NewType = ARM_TYPE5;
				Vars.Opcode.UseNewType = true;
				Vars.Immediate = temp;
			} else {
				Arm.addPoolValue(this,Vars.Immediate);
			}
		} else if (Opcode.flags & ARM_BRANCH)
		{
			if (Opcode.flags & ARM_HALFWORD)
			{
				if (Vars.Immediate & 1)
				{
					Logger::queueError(Logger::Error,L"Branch target must be halfword aligned");
					return false;
				}
			} else {
				if (Vars.Immediate & 3)
				{
					Logger::queueError(Logger::Error,L"Branch target must be word aligned");
					return false;
				}
			}

			Vars.Immediate = (int) (Vars.Immediate-RamPos-8);
			if (abs(Vars.Immediate) >= 0x2000000)
			{
				Logger::queueError(Logger::Error,L"Branch target %08X out of range",Vars.OriginalImmediate);
				return false;
			}
		} else if (Opcode.flags & ARM_ABSIMM)	// ldr r0,[I]
		{
			Vars.Immediate = (int) (Vars.Immediate-RamPos-8);
			if (abs(Vars.Immediate) >= (1 << Vars.ImmediateBitLen))
			{
				Logger::queueError(Logger::Error,L"Load target %08X out of range",Vars.OriginalImmediate);
				return false;
			}
		} else if (Opcode.flags & ARM_SWI)	// it's an interrupt, may need to shift it
		{
			bool needsShift = arch == AARCH_GBA || arch == AARCH_NDS;
			if (needsShift && Vars.Immediate < 0xFF)
			{
				Vars.Immediate <<= 16;
				Vars.OriginalImmediate = Vars.Immediate;
			}
		} else if ((Opcode.flags & ARM_ABS) && Vars.Immediate < 0)
		{
			Vars.Immediate = abs(Vars.Immediate);
			Vars.negative = true;
		}

		if (Vars.ImmediateBitLen != 32 && !(Opcode.flags & ARM_ABSIMM))
		{
			unsigned int check = Opcode.flags & ARM_ABS ? abs(Vars.Immediate) : Vars.Immediate;
			if (check >= (unsigned int)(1 << Vars.ImmediateBitLen))
			{
				Logger::queueError(Logger::Error,L"Immediate value %X out of range",Vars.Immediate);
				return false;
			}
		}
	}
	
	if (!memoryAdvanced)
		g_fileManager->advanceMemory(4);

	return false;
}

void CArmInstruction::FormatOpcode(char* Dest, const char* Source) const
{
	while (*Source != 0)
	{
		switch (*Source)
		{
		case 'C':	// condition
			Dest += sprintf(Dest,"%s",ArmConditions[Vars.Opcode.c]);
			Source++;
			break;
		case 'S':	// set flag
			if (Vars.Opcode.s == true) *Dest++ = 's';
			Source++;
			break;
		case 'A':	// addressing mode
			if (Opcode.flags & ARM_LOAD)
			{
				Dest += sprintf(Dest,"%s",ArmAddressingModes[LdmModes[Vars.Opcode.a]]);
			} else {
				Dest += sprintf(Dest,"%s",ArmAddressingModes[StmModes[Vars.Opcode.a]]);
			}
			Source++;
			break;
		case 'X':	// x flag
			if (Vars.Opcode.x == false) *Dest++ = 'b';
			else *Dest++ = 't';
			Source++;
			break;
		case 'Y':	// y flag
			if (Vars.Opcode.y == false) *Dest++ = 'b';
			else *Dest++ = 't';
			Source++;
			break;
		default:
			*Dest++ = *Source++;
			break;
		}
	}
	*Dest = 0;
}

void CArmInstruction::FormatInstruction(const char* encoding, char* dest) const
{
/*	while (*encoding != 0)
	{
		switch (*encoding)
		{
		case 's':	//  reg
			dest += sprintf(dest,"%s",Vars.rs.Name);
			encoding += 2;
			break;
		case 'd':	//  reg
			dest += sprintf(dest,"%s",Vars.rd.Name);
			encoding += 2;
			break;
		case 'n':	//  reg
			dest += sprintf(dest,"%s",Vars.rn.Name);
			encoding += 2;
			break;
		case 'm':	//  reg
			dest += sprintf(dest,"%s",Vars.rm.Name);
			encoding += 2;
			break;
		case 'W':	// writeback
			if (Vars.writeback == true) *dest++ = '!';
			encoding++;
			break;
		case 'p':	// psr
			if (Vars.psr == true) *dest++ = '^';
			encoding++;
			break;
		case 'P':	// mrs/msr psr
			dest += sprintf(dest,"%s",Vars.PsrData.spsr == true ? "spsr" : "cpsr");
			if (encoding[1] == '0') dest += sprintf(dest,"%s",ArmPsrModes[Vars.PsrData.field]);
			encoding += 2;
			break;
		case 'S':	// shifts
			if (Vars.Shift.UseShift == true)
			{
				*dest++ = ',';
				dest += sprintf(dest,"%s ",ArmShiftModes[Vars.Shift.Type]);
				if (Vars.Shift.ShiftByRegister == true)
				{
					dest += sprintf(dest,"%s",Vars.Shift.reg.Name);
				} else {
					dest += sprintf(dest,"0x%X",Vars.Shift.ShiftAmount);
				}
			}
			encoding += 2;
			break;
		case 'R':	// rlist
			dest += sprintf(dest,"%s",Vars.RlistStr);
			encoding++;
			break;
		case 'I':
		case 'i':
			dest += sprintf(dest,"0x%08X",Vars.OriginalImmediate);
			encoding++;
			break;
		case 'j':
			dest += sprintf(dest,"0x%0*X",(Vars.ImmediateBitLen+3)>>2,Vars.OriginalImmediate & ((1 << Vars.ImmediateBitLen)-1));
			encoding+=2;
			break;
		case 'D':	// cop reg
			dest += sprintf(dest,"%s",Vars.CopData.cd.Name);
			encoding++;
			break;
		case 'N':	// cop reg
			dest += sprintf(dest,"%s",Vars.CopData.cn.Name);
			encoding++;
			break;
		case 'M':	// cop reg
			dest += sprintf(dest,"%s",Vars.CopData.cm.Name);
			encoding++;
			break;
		case 'X':	// cop number
			dest += sprintf(dest,"%s",Vars.CopData.pn.Name);
			encoding++;
			break;
		case 'Y':	// cop opcode
			dest += sprintf(dest,"0x%02X",Vars.CopData.Cpop);
			encoding++;
			break;
		case 'Z':	// cop inf
			dest += sprintf(dest,"0x%02X",Vars.CopData.Cpinf);
			encoding++;
			break;
		case 'v':	// sign
			if (Vars.SignPlus == false) dest += sprintf(dest,"-");
			encoding++;
			break;
		case '/':
			encoding += 2;
			break;
		default:
			*dest++ = *encoding++;
			break;
		}
	}
	*dest = 0;*/
}

void CArmInstruction::writeTempData(TempData& tempData) const
{
	char OpcodeName[32];
	char str[256];

	FormatOpcode(OpcodeName,Opcode.name);
	int pos = sprintf(str,"   %s",OpcodeName);
	while (pos < 11) str[pos++] = ' ';
	str[pos] = 0;
	FormatInstruction(Opcode.mask,&str[pos]);

	tempData.writeLine(RamPos,convertUtf8ToWString(str));
}

void CArmInstruction::Encode() const
{
	unsigned int encoding = Vars.Opcode.UseNewEncoding == true ? Vars.Opcode.NewEncoding : Opcode.encoding;

	if ((Opcode.flags & ARM_UNCOND) == 0) encoding |= Vars.Opcode.c << 28;
	if (Vars.Opcode.s == true) encoding |= (1 << 20);

	unsigned char shiftType;
	int shiftAmount;
	if (Vars.Shift.UseFinal == true)
	{
		shiftType = Vars.Shift.FinalType;
		shiftAmount = Vars.Shift.FinalShiftAmount;
	} else {
		shiftType = Vars.Shift.Type;
		shiftAmount = Vars.Shift.ShiftAmount;
	}

	switch (Vars.Opcode.UseNewType == true ? Vars.Opcode.NewType : Opcode.type)
	{
	case ARM_TYPE3:		// ARM.3: Branch and Exchange (BX, BLX)
		encoding |= (Vars.rn.num << 0);
		break;
	case ARM_TYPE4:		// ARM.4: Branch and Branch with Link (B, BL, BLX)
		if ((Opcode.flags & ARM_HALFWORD) && (Vars.Immediate & 2)) encoding |= 1 << 24;
		encoding |= (Vars.Immediate >> 2) & 0xFFFFFF;
		break;
	case ARM_TYPE5:		// ARM.5: Data Processing
		if (Opcode.flags & ARM_N) encoding |= (Vars.rn.num << 16);
		if (Opcode.flags & ARM_D) encoding |= (Vars.rd.num << 12);

		if (Opcode.flags & ARM_IMMEDIATE)	// immediate als op2
		{
			encoding |= (shiftAmount << 7);
			encoding |= Vars.Immediate;
		} else if (Opcode.flags & ARM_REGISTER) {	// shifted register als op2
			if (Vars.Shift.UseShift == true)
			{
				if (Vars.Shift.ShiftByRegister == true)
				{
					encoding |= (Vars.Shift.reg.num << 8);
					encoding |= (1 << 4);
				} else {	// shiftbyimmediate
					encoding |= (shiftAmount << 7);
				}
				encoding |= (shiftType << 5);
			}
			encoding |= (Vars.rm.num << 0);
		}
		break;
	case ARM_TYPE6:		// ARM.6: PSR Transfer (MRS, MSR)
		if (Opcode.flags & ARM_MRS) //  MRS{cond} Rd,Psr          ;Rd = Psr
		{
			if (Vars.PsrData.spsr == true) encoding |= (1 << 22);
			encoding |= (Vars.rd.num << 12);
		} else {					//  MSR{cond} Psr{_field},Op  ;Psr[field] = Op
			if (Vars.PsrData.spsr == true) encoding |= (1 << 22);
			encoding |= (Vars.PsrData.field << 16);

			if (Opcode.flags & ARM_REGISTER)
			{
				encoding |= (Vars.rm.num << 0);
			} else if (Opcode.flags & ARM_IMMEDIATE)
			{
				encoding |= (shiftAmount << 7);
				encoding |= Vars.Immediate;
			}
		}
		break;
	case ARM_TYPE7:		// ARM.7: Multiply and Multiply-Accumulate (MUL,MLA)
		encoding |= (Vars.rd.num << 16);
		if (Opcode.flags & ARM_N) encoding |= (Vars.rn.num << 12);
		encoding |= (Vars.rs.num << 8);
		if ((Opcode.flags & ARM_Y) && Vars.Opcode.y == true) encoding |= (1 << 6);
		if ((Opcode.flags & ARM_X) && Vars.Opcode.x == true) encoding |= (1 << 5);
		encoding |= (Vars.rm.num << 0);
		break;
	case ARM_TYPE9:		// ARM.9: Single Data Transfer (LDR, STR, PLD)
		if (Vars.writeback == true) encoding |= (1 << 21);
		if (Opcode.flags & ARM_N) encoding |= (Vars.rn.num << 16);
		if (Opcode.flags & ARM_D) encoding |= (Vars.rd.num << 12);
		if ((Opcode.flags & ARM_SIGN) && Vars.SignPlus == false) encoding &= ~(1 << 23);
		if ((Opcode.flags & ARM_ABS) && Vars.negative == true) encoding &= ~(1 << 23);
		if (Opcode.flags & ARM_IMMEDIATE)
		{
			int immediate = Vars.Immediate;
			if (immediate < 0)
			{
				encoding &= ~(1 << 23);
				immediate = abs(immediate);
			}
			encoding |= (immediate << 0);
		} else if (Opcode.flags & ARM_REGISTER)	// ... heißt der opcode nutzt shifts, mit immediates
		{
			if (Vars.Shift.UseShift == true)
			{
				encoding |= (shiftAmount << 7);
				encoding |= (shiftType << 5);
			}
			encoding |= (Vars.rm.num << 0);
		}
		break;
	case ARM_TYPE10:	// ARM.10: Halfword, Doubleword, and Signed Data Transfer
		if (Vars.writeback == true) encoding |= (1 << 21);
		encoding |= (Vars.rn.num << 16);
		encoding |= (Vars.rd.num << 12);
		if ((Opcode.flags & ARM_SIGN) && Vars.SignPlus == false) encoding &= ~(1 << 23);
		if ((Opcode.flags & ARM_ABS) && Vars.negative == true) encoding &= ~(1 << 23);
		if (Opcode.flags & ARM_IMMEDIATE)
		{
			int immediate = Vars.Immediate;
			if (immediate < 0)
			{
				encoding &= ~(1 << 23);
				immediate = abs(immediate);
			}
			encoding |= ((immediate & 0xF0) << 4);
			encoding |= (immediate & 0xF);
		} else if (Opcode.flags & ARM_REGISTER)
		{
			encoding |= (Vars.rm.num << 0);
		}
		break;
	case ARM_TYPE11:	// ARM.11: Block Data Transfer (LDM,STM)
		if (Opcode.flags & ARM_LOAD) encoding |= (LdmModes[Vars.Opcode.a] << 23);
		else if (Opcode.flags & ARM_STORE) encoding |= (StmModes[Vars.Opcode.a] << 23);
		if (Vars.psr == true) encoding |= (1 << 22);
		if (Vars.writeback == true) encoding |= (1 << 21);
		if (Opcode.flags & ARM_N) encoding |= (Vars.rn.num << 16);
		encoding |= (Vars.rlist);
		break;
	case ARM_TYPE12:	// ARM.12: Single Data Swap (SWP)
	case ARM_MISC:		// ARM.X: Count Leading Zeros
		encoding |= (Vars.rm.num << 0);
		encoding |= (Vars.rd.num << 12);
		if (Opcode.flags & ARM_N) encoding |= (Vars.rn.num << 16);
		break;
	case ARM_TYPE13:	// ARM.13: Software Interrupt (SWI,BKPT)
		if (Opcode.flags & ARM_SWI)
		{
			encoding |= Vars.Immediate;
		} else {
			encoding |= (Vars.Immediate & 0xF);
			encoding |= (Vars.Immediate >> 4) << 8;
		}
		break;
	case ARM_TYPE14:	// ARM.14: Coprocessor Data Operations (CDP)
		if (Opcode.flags & ARM_COPOP) encoding |= (Vars.CopData.Cpop << 20);
		encoding |= (Vars.CopData.cn.num << 16);
		encoding |= (Vars.CopData.cd.num << 12);
		encoding |= (Vars.CopData.pn.num << 8);
		if (Opcode.flags & ARM_COPINF) encoding |= (Vars.CopData.Cpinf << 5);
		encoding |= (Vars.CopData.cm.num << 0);
		break;
	case ARM_TYPE16:	// ARM.16: Coprocessor Register Transfers (MRC, MCR)
		if (Opcode.flags & ARM_COPOP) encoding |= (Vars.CopData.Cpop << 21);
		encoding |= (Vars.CopData.cn.num << 16);
		encoding |= (Vars.rd.num << 12);
		encoding |= (Vars.CopData.pn.num << 8);
		if (Opcode.flags & ARM_COPINF) encoding |= (Vars.CopData.Cpinf << 5);
		encoding |= (Vars.CopData.cm.num << 0);
		break;
	case ARM_TYPE17:	// ARM.X: Coprocessor Double-Register Transfer (MCRR,MRRC)
		encoding |= (Vars.rn.num << 16);
		encoding |= (Vars.rd.num << 12);
		encoding |= (Vars.CopData.pn.num << 8);
		encoding |= (Vars.CopData.Cpop << 4);
		encoding |= (Vars.CopData.cm.num << 0);
		break;
	default:
		printf("doh");
	}

	g_fileManager->writeU32((uint32_t)encoding);
}
