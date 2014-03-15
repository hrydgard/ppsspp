#ifdef _WIN32
#include "stdafx.h"
#endif
#include "MIPSAsm.h"
#include <cstdarg>
#include <cstring>

namespace MIPSAsm
{
	
char errorMessage[512];

void SetAssembleError(const char* format, ...)
{
	va_list args;

	va_start(args,format);
	vsprintf(errorMessage,format,args);
	va_end (args);
}

char* GetAssembleError()
{
	return errorMessage;
}

void SplitLine(const char* Line, char* Name, char* Arguments)
{
	while (*Line == ' ' || *Line == '\t') Line++;
	while (*Line != ' ' && *Line != '\t')
	{
		if (*Line  == 0)
		{
			*Name = 0;
			*Arguments = 0;
			return;
		}
		*Name++ = *Line++;
	}
	*Name = 0;
	
	while (*Line == ' ' || *Line == '\t') Line++;

	while (*Line != 0)
	{
		*Arguments++ = *Line++;
	}
	*Arguments = 0;
}

bool MipsAssembleOpcode(const char* line, DebugInterface* cpu, u32 address, u32& dest)
{
	char name[64],args[256];
	SplitLine(line,name,args);

	CMipsInstruction Opcode(cpu);
	if (cpu == NULL || Opcode.Load(name,args,(int)address) == false)
	{
		return false;
	}
	if (Opcode.Validate() == false)
	{
		return false;
	}

	Opcode.Encode();
	dest = Opcode.getEncoding();

	return true;
}

int MipsGetRegister(const char* source, int& RetLen)
{
	for (int z = 0; MipsRegister[z].name != NULL; z++)
	{
		int len = MipsRegister[z].len;
		if (strncmp(MipsRegister[z].name,source,len) == 0)	// fine so far
		{
			if (source[len] == ',' || source[len] == '\n'  || source[len] == 0
				|| source[len] == ')'  || source[len] == '(' || source[len] == '-')	// one of these HAS TO come after a register
			{
				RetLen = len;
				return MipsRegister[z].num;
			}
		}
	}
	return -1;
}

int MipsGetFloatRegister(const char* source, int& RetLen)
{
	for (int z = 0; MipsFloatRegister[z].name != NULL; z++)
	{
		int len = MipsFloatRegister[z].len;
		if (strncmp(MipsFloatRegister[z].name,source,len) == 0)	// fine so far
		{
			if (source[len] == ',' || source[len] == '\n'  || source[len] == 0
				|| source[len] == ')'  || source[len] == '(' || source[len] == '-')	// one of these HAS TO come after a register
			{
				RetLen = len;
				return MipsFloatRegister[z].num;
			}
		}
	}
	return -1;
}

bool MipsCheckImmediate(const char* Source, char* Dest, int& RetLen)
{
	int BufferPos = 0;
	int l;

	if (MipsGetRegister(Source,l) != -1)	//  there's a register -> no immediate
	{
		return false;
	}

	int SourceLen = 0;

	while (true)
	{
		if (*Source == '\'' && *(Source+2) == '\'')
		{
			Dest[BufferPos++] = *Source++;
			Dest[BufferPos++] = *Source++;
			Dest[BufferPos++] = *Source++;
			SourceLen+=3;
			continue;
		}

		if (*Source == 0 || *Source == '\n' || *Source == ',')
		{
			Dest[BufferPos] = 0;
			break;
		}
		if ( *Source == ' ' || *Source == '\t')
		{
			Source++;
			SourceLen++;
			continue;
		}


		if (*Source == '(')	// could also be part of the opcode, ie (r4)
		{
			if (MipsGetRegister(Source+1,l) != -1)	// stop if it is
			{
				Dest[BufferPos] = 0;
				break;
			}
		}
		Dest[BufferPos++] = *Source++;
		SourceLen++;
	}

	if (BufferPos == 0) return false;

	RetLen = SourceLen;
	return true;
}



bool CMipsInstruction::Load(char* Name, char* Params, int RamPos)
{
	bool paramfail = false;
	NoCheckError = false;
	this->RamPos = RamPos;

	for (int z = 0; MipsOpcodes[z].name != NULL; z++)
	{
		if (strcmp(Name,MipsOpcodes[z].name) == 0)
		{
			if (LoadEncoding(MipsOpcodes[z],Params) == true)
			{
				Loaded = true;
				return true;
			}
			paramfail = true;
		}
	}

	if (NoCheckError == false)
	{
		if (paramfail == true)
		{
			SetAssembleError("Parameter failure \"%s\"",Params);
		} else {
			SetAssembleError("Invalid opcode \"%s\"",Name);
		}
	}
	return false;
}

bool CMipsInstruction::LoadEncoding(const tMipsOpcode& SourceOpcode, char* Line)
{
	char ImmediateBuffer[512];

	int RetLen;
	bool Immediate = false;

	const char *SourceEncoding = SourceOpcode.encoding;
	char* OriginalLine = Line;

	while (*Line == ' ' || *Line == '\t') Line++;

	if (!(*SourceEncoding == 0 && *Line == 0))
	{
		while (*SourceEncoding != '\0')
		{
			while (*Line == ' ' || *Line == '\t') Line++;
			if (*Line == 0) return false;

			switch (*SourceEncoding)
			{
			case 'T':	// float reg
				if ((Vars.rt = MipsGetFloatRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 'D':	// float reg
				if ((Vars.rd = MipsGetFloatRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 'S':	// float reg
				if ((Vars.rs = MipsGetFloatRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 't':
				if ((Vars.rt = MipsGetRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 'd':
				if ((Vars.rd = MipsGetRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 's':
				if ((Vars.rs = MipsGetRegister(Line,RetLen)) == -1) return false;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 'i':	// 16 bit immediate
			case 'I':	// 32 bit immediate
			case 'a':	// 5 bit immediate
			case 'b':	// 20 bit immediate
				if (MipsCheckImmediate(Line,ImmediateBuffer,RetLen) == false) return false;
				Immediate = true;
				Line += RetLen;
				SourceEncoding++;
				break;
			case 'r':	// forced register
				if (MipsGetRegister(Line,RetLen) != *(SourceEncoding+1)) return false;
				Line += RetLen;
				SourceEncoding += 2;
				break;
			default:	// everything else
				if (*SourceEncoding++ != *Line++) return false;
				break;
			}
		}
	}

	while (*Line == ' ' || *Line == '\t') Line++;
	if (*Line != 0)	return false;	// there's something else at the end, bad

	// the opcode is fine - now set all remaining flags
	Opcode = SourceOpcode;

	if (Immediate == true)
	{
		u32 imm;
		PostfixExpression postfix;
		if (cpu->initExpression(ImmediateBuffer,postfix) == false) return false;
		if (cpu->parseExpression(postfix,imm) == false) return false;

		Vars.Immediate = (int) imm;
		if (Opcode.flags & O_I5)
		{
			Vars.ImmediateType = MIPS_IMMEDIATE5;
		} else if (Opcode.flags & O_I16)
		{
			Vars.ImmediateType = MIPS_IMMEDIATE16;
		} else if (Opcode.flags & O_I20)
		{
			Vars.ImmediateType = MIPS_IMMEDIATE20;
		} else if (Opcode.flags & O_I26)
		{
			Vars.ImmediateType = MIPS_IMMEDIATE26;
		}
	} else {
		Vars.ImmediateType = MIPS_NOIMMEDIATE;
	}

	return true;
}

bool CMipsInstruction::Validate()
{
	if (RamPos % 4)
	{
		return false;
	}

	// check immediates
	if (Vars.ImmediateType != MIPS_NOIMMEDIATE)
	{
		Vars.OriginalImmediate = Vars.Immediate;
 
		if (Opcode.flags & O_IPCA)	// absolute value >> 2)
		{
			Vars.Immediate = (Vars.Immediate >> 2) & 0x03FFFFFF;
		} else if (Opcode.flags & O_IPCR)	// relative 16 bit value
		{
			int num = (Vars.Immediate-RamPos-4);
			
			if (num > 0x20000 || num < (-0x20000))
			{
				SetAssembleError("Branch target %08X out of range",Vars.Immediate);
				return false;
			}
			Vars.Immediate = num >> 2;
		}

		switch (Vars.ImmediateType)
		{
		case MIPS_IMMEDIATE5:
			if (Vars.Immediate > 0x1F)
			{
				SetAssembleError("Immediate value %02X out of range",Vars.OriginalImmediate);
				return false;
			}
			Vars.Immediate &= 0x1F;
			break;
		case MIPS_IMMEDIATE16:
			if (abs(Vars.Immediate) > 0x0000FFFF)
			{
				SetAssembleError("Immediate value %04X out of range",Vars.OriginalImmediate);
				return false;
			}
			Vars.Immediate &= 0x0000FFFF;
			break;
		case MIPS_IMMEDIATE20:
			if (abs(Vars.Immediate) > 0x000FFFFF)
			{
				SetAssembleError("Immediate value %08X out of range",Vars.OriginalImmediate);
				return false;
			}
			Vars.Immediate &= 0x000FFFFF;
			break;
		case MIPS_IMMEDIATE26:
			if (abs(Vars.Immediate) > 0x03FFFFFF)
			{
				SetAssembleError("Immediate value %08X out of range",Vars.OriginalImmediate);
				return false;
			}
			Vars.Immediate &= 0x03FFFFFF;
			break;
		case MIPS_NOIMMEDIATE:
			break;
		}
	}

	return true;
}

void CMipsInstruction::Encode()
{
	encoding = (u32) Opcode.destencoding;

	if (Opcode.flags & O_RS) encoding |= (Vars.rs << 21);	// source reg
	if (Opcode.flags & O_RT) encoding |= (Vars.rt << 16);	// target reg
	if (Opcode.flags & O_RD) encoding |= (Vars.rd << 11);	// dest reg
	if (Opcode.flags & O_RSD)	// s = d & s
	{
		encoding |= (Vars.rs << 21);
		encoding |= (Vars.rs << 11);
	}
	if (Opcode.flags & O_RST)	// s = t & s
	{
		encoding |= (Vars.rs << 21);
		encoding |= (Vars.rs << 16);
	}
	if (Opcode.flags & O_RDT)	// d = t & d
	{
		encoding |= (Vars.rd << 16);
		encoding |= (Vars.rd << 11);
	}

	if (Opcode.flags & MO_FRT) encoding |= (Vars.rt << 16);	// float target
	if (Opcode.flags & MO_FRS) encoding |= (Vars.rs << 11);	// float source
	if (Opcode.flags & MO_FRD) encoding |= (Vars.rd << 6);	// float target
	if (Opcode.flags & MO_FRSD)	// s = d & s
	{
		encoding |= (Vars.rs << 6);
		encoding |= (Vars.rs << 11);
	}
	if (Opcode.flags & MO_FRST)	// s = t & s
	{
		encoding |= (Vars.rs << 11);
		encoding |= (Vars.rs << 16);
	}
	if (Opcode.flags & MO_FRDT)	// d = t & d
	{
		encoding |= (Vars.rd << 6);
		encoding |= (Vars.rd << 16);
	}

	switch (Vars.ImmediateType)
	{
	case MIPS_IMMEDIATE5:
		encoding |= (Vars.Immediate & 0x0000001F) << 6;
		break;
	case MIPS_IMMEDIATE16:
		encoding |= (Vars.Immediate & 0x0000FFFF);
		break;
	case MIPS_IMMEDIATE20:
		encoding |= (Vars.Immediate & 0x000FFFFF) << 6;
		break;
	case MIPS_IMMEDIATE26:
		encoding |= (Vars.Immediate & 0x03FFFFFF);
		break;
	case MIPS_NOIMMEDIATE:
		break;
	}
}

}
