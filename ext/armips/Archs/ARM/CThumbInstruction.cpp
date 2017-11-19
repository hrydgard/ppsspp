#include "stdafx.h"
#include "CThumbInstruction.h"
#include "Core/Common.h"
#include "Arm.h"
#include "ThumbOpcodes.h"
#include "Core/FileManager.h"

bool CThumbInstruction::Load(char* Name, char* Params)
{
	return false;
}

CThumbInstruction::CThumbInstruction(const tThumbOpcode& sourceOpcode, ThumbOpcodeVariables& vars)
{
	this->Opcode = sourceOpcode;
	this->Vars = vars;
	
	OpcodeSize = Opcode.flags & THUMB_LONG ? 4 : 2;
}

void CThumbInstruction::setPoolAddress(int64_t address)
{
	int pos = (int) address-((RamPos+4) & 0xFFFFFFFD);
	if (pos < 0 || pos > 1020)
	{
		Logger::queueError(Logger::Error,L"Literal pool out of range");
		return;
	}

	Vars.Immediate = pos >> 2;
}

bool CThumbInstruction::Validate()
{
	RamPos = g_fileManager->getVirtualAddress();

	if (RamPos & 1)
	{
		Logger::queueError(Logger::Warning,L"Opcode not halfword aligned");
	}

	if (Opcode.flags & THUMB_DS)
	{
		Vars.rs = Vars.rd;
	}

	bool memoryAdvanced = false;
	if (Opcode.flags & THUMB_IMMEDIATE)
	{
		ExpressionValue value = Vars.ImmediateExpression.evaluate();

		switch (value.type)
		{
		case ExpressionValueType::Integer:
			Vars.Immediate = (int) value.intValue;
			break;
		case ExpressionValueType::Float:
			if (!(Opcode.flags & THUMB_POOL))
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
	
		g_fileManager->advanceMemory(OpcodeSize);
		memoryAdvanced = true;

		if (Opcode.flags & THUMB_BRANCH)
		{
			if (Opcode.flags & THUMB_EXCHANGE)
			{
				if (Vars.Immediate & 3)
				{
					Logger::queueError(Logger::Error,L"Branch target must be word aligned");
					return false;
				}
			} else {
				if (Vars.Immediate & 1)
				{
					Logger::queueError(Logger::Error,L"Branch target must be halfword aligned");
					return false;
				}
			}

			int num = (int) (Vars.Immediate-RamPos-4);
			
			if (num >= (1 << Vars.ImmediateBitLen) || num < (0-(1 << Vars.ImmediateBitLen)))
			{
				Logger::queueError(Logger::Error,L"Branch target %08X out of range",Vars.Immediate);
				return false;
			}

			Vars.Immediate = num >> 1;
			if (Opcode.flags & THUMB_EXCHANGE)
			{
				Vars.Immediate += Vars.Immediate&1;
			}
		} else if (Opcode.flags & THUMB_WORD)
		{
			if (Vars.Immediate & 3)	// not allowed
			{
				Logger::queueError(Logger::Error,L"Immediate value must be a multiple of 4");
				return false;
			}
			Vars.Immediate >>= 2;
		} else if (Opcode.flags & THUMB_HALFWORD)
		{
			if (Vars.Immediate & 1)	// not allowed
			{
				Logger::queueError(Logger::Error,L"Immediate value must be a multiple of 2");
				return false;
			}
			Vars.Immediate >>= 1;
		} else if (Opcode.flags & THUMB_POOL)
		{
			Arm.addPoolValue(this,Vars.Immediate);
		} else if (Opcode.flags & THUMB_PCR)
		{
			if (Vars.Immediate & 3)
			{
				Logger::queueError(Logger::Error,L"PC relative address must be word aligned");
				return false;
			}

			int pos = Vars.Immediate-((RamPos+4) & 0xFFFFFFFD);
			if (pos < 0 || pos > 1020)
			{
				Logger::queueError(Logger::Error,L"PC relative address out of range");
				return false;
			}
			Vars.Immediate = pos >> 2;
		}

		if (Vars.ImmediateBitLen != 32)
		{
			if (abs(Vars.Immediate) >= (1 << Vars.ImmediateBitLen))
			{
				Logger::queueError(Logger::Error,L"Immediate value %X out of range",Vars.Immediate);
				return false;
			}
			Vars.Immediate &= (1 << Vars.ImmediateBitLen)-1;
		}
	}
	
	if (!memoryAdvanced)
		g_fileManager->advanceMemory(OpcodeSize);

	return false;
}

void CThumbInstruction::WriteInstruction(unsigned short encoding) const
{
	g_fileManager->writeU16(encoding);
}

void CThumbInstruction::Encode() const
{
	unsigned int encoding = Opcode.encoding;;
	int immediate;

	if (Opcode.type == THUMB_TYPE19)	// THUMB.19: long branch with link
	{
		if (Opcode.flags & THUMB_LONG)
		{
			encoding |= ((Vars.Immediate >> 11) & 0x7FF);
			WriteInstruction(encoding);

			if (Opcode.flags & THUMB_EXCHANGE)
			{
				WriteInstruction(0xE800 | (Vars.Immediate & 0x7FF));
			} else {
				WriteInstruction(0xF800 | (Vars.Immediate & 0x7FF));
			}
		} else {
			if (Opcode.flags & THUMB_IMMEDIATE)
				encoding |= Vars.Immediate & 0x7FF;
			WriteInstruction(encoding);
		}
	} else {
		switch (Opcode.type)
		{
		case THUMB_TYPE1:	// THUMB.1: move shifted register
			encoding |= (Vars.Immediate << 6);
			encoding |= (Vars.rs.num << 3);
			encoding |= (Vars.rd.num << 0);
			break;
		case THUMB_TYPE2:	// THUMB.2: add/subtract
			if (Opcode.flags & THUMB_IMMEDIATE)
			{
				encoding |= (Vars.Immediate << 6);
			} else if (Opcode.flags & THUMB_REGISTER)
			{
				encoding |= (Vars.rn.num << 6);
			}
			encoding |= (Vars.rs.num << 3);
			encoding |= (Vars.rd.num << 0);
			break;
		case THUMB_TYPE3:	// THUMB.3: move/compare/add/subtract immediate
			encoding |= (Vars.rd.num << 8);
			encoding |= (Vars.Immediate << 0);
			break;
		case THUMB_TYPE4:	// THUMB.4: ALU operations
			encoding |= (Vars.rs.num << 3);
			encoding |= (Vars.rd.num << 0);
			break;
		case THUMB_TYPE5:	// THUMB.5: Hi register operations/branch exchange
			if (Opcode.flags & THUMB_D)
			{
				if (Vars.rd.num > 0x7) encoding |= (1 << 7);
				encoding |= (Vars.rd.num & 0x7);
			}
			if (Opcode.flags & THUMB_S)
			{
				if (Vars.rs.num > 0x7) encoding |= (1 << 6);
				encoding |= ((Vars.rs.num & 0x7) << 3);
			}
			break;
		case THUMB_TYPE6:	// THUMB.6: load PC-relative
			encoding |= (Vars.rd.num << 8);
			encoding |= (Vars.Immediate << 0);
			break;
		case THUMB_TYPE7:	// THUMB.7: load/store with register offset
		case THUMB_TYPE8:	// THUMB.8: load/store sign-extended byte/halfword
			encoding |= (Vars.ro.num << 6);
			encoding |= (Vars.rs.num << 3);
			encoding |= (Vars.rd.num << 0);
			break;
		case THUMB_TYPE9:	// THUMB.9: load/store with immediate offset
		case THUMB_TYPE10:	// THUMB.10: load/store halfword
			if (Opcode.flags & THUMB_IMMEDIATE) encoding |= (Vars.Immediate << 6);
			encoding |= (Vars.rs.num << 3);
			encoding |= (Vars.rd.num << 0);
			break;
		case THUMB_TYPE11:	// THUMB.11: load/store SP-relative
			encoding |= (Vars.rd.num << 8);
			if (Opcode.flags & THUMB_IMMEDIATE) encoding |= (Vars.Immediate << 0);
			break;
		case THUMB_TYPE12:	// THUMB.12: get relative address
			encoding |= (Vars.rd.num << 8);
			encoding |= (Vars.Immediate << 0);
			break;
		case THUMB_TYPE13:	// THUMB.13: add offset to stack pointer
			immediate = Vars.Immediate;
			if (Opcode.flags & THUMB_NEGATIVE_IMMEDIATE) 
				immediate = (unsigned char)(0-immediate);
			if (immediate & 0x80)	// sub
			{
				encoding |= 1 << 7;
				immediate = 0x100-immediate;
			}
			encoding |= (immediate << 0);
			break;
		case THUMB_TYPE14:	// THUMB.14: push/pop registers
			if (Vars.rlist & 0xC000) encoding |= (1 << 8); // r14 oder r15
			encoding |= (Vars.rlist & 0xFF);
			break;
		case THUMB_TYPE15:	// THUMB.15: multiple load/store
			encoding |= (Vars.rd.num << 8);
			encoding |= (Vars.rlist & 0xFF);
			break;
		case THUMB_TYPE16:	// THUMB.16: conditional branch
		case THUMB_TYPE17:	// THUMB.17: software interrupt and breakpoint
		case THUMB_TYPE18:	// THUMB.18: unconditional branch
			encoding |= (Vars.Immediate << 0);
			break;
		}
		WriteInstruction(encoding);
	}
}

void CThumbInstruction::FormatInstruction(const char* encoding, char* dest) const
{
/*	while (*encoding != 0)
	{
		switch (*encoding)
		{
		case 'D':
		case 'd':
			dest += sprintf(dest,"%s",Vars.rd.Name);
			encoding++;
			break;
		case 'S':
		case 's':
			dest += sprintf(dest,"%s",Vars.rs.Name);
			encoding++;
			break;
		case 'n':
			dest += sprintf(dest,"%s",Vars.rn.Name);
			encoding++;
			break;
		case 'o':
			dest += sprintf(dest,"%s",Vars.ro.Name);
			encoding++;
			break;
		case 'I':
			dest += sprintf(dest,"0x%0*X",(Vars.ImmediateBitLen+3)>>2,Vars.OriginalImmediate);
			encoding += 2;
			break;
		case 'i':
			dest += sprintf(dest,"0x%0*X",(Vars.ImmediateBitLen+3)>>2,Vars.OriginalImmediate & ((1 << Vars.ImmediateBitLen)-1));
			encoding += 2;
			break;
		case 'r':	// forced register
			dest += sprintf(dest,"r%d",*(encoding+1));
			encoding += 2;
			break;
		case 'R':
			dest += sprintf(dest,"%s",Vars.RlistStr);
			encoding += 3;
			break;
		case '/':	// optional
			encoding += 2;
			break;
		default:
			*dest++ = *encoding++;
			break;
		}
	}
	*dest = 0;*/
}

void CThumbInstruction::writeTempData(TempData& tempData) const
{
	char str[256];

	int pos = sprintf(str,"   %s",Opcode.name);
	while (pos < 11) str[pos++] = ' ';
	str[pos] = 0;
	FormatInstruction(Opcode.mask,&str[pos]);

	tempData.writeLine(RamPos,convertUtf8ToWString(str));
}
