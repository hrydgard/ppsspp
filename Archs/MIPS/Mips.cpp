#include "stdafx.h"
#include "Mips.h"
#include "CMipsInstruction.h"
#include "Core/Common.h"
#include "MipsMacros.h"
#include "Core/FileManager.h"
#include "MipsElfFile.h"
#include "Commands/CDirectiveFile.h"
#include "PsxRelocator.h"
#include "MipsParser.h"
#include "Parser/Parser.h"

CMipsArchitecture Mips;

bool MipsElfRelocator::relocateOpcode(int type, RelocationData& data)
{
	unsigned int p;

	unsigned int op = data.opcode;
	switch (type)
	{
	case R_MIPS_26: //j, jal
		op = (op & 0xFC000000) | (((op&0x03FFFFFF)+(data.relocationBase>>2))&0x03FFFFFF);
		break;
	case R_MIPS_32:
		op += (int) data.relocationBase;
		break;	
	case R_MIPS_HI16:
		p = (op & 0xFFFF) + (int) data.relocationBase;
		op = (op&0xffff0000) | (((p >> 16) + ((p & 0x8000) != 0)) & 0xFFFF);
		break;
	case R_MIPS_LO16:
		op = (op&0xffff0000) | (((op&0xffff)+data.relocationBase)&0xffff);
		break;
	default:
		data.errorMessage = formatString(L"Unknown MIPS relocation type %d",type);
		return false;
	}

	data.opcode = op;
	return true;
}

void MipsElfRelocator::setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType)
{
	data.symbolAddress = symbolAddress;
	data.targetSymbolType = symbolType;
}

const wchar_t* mipsCtorTemplate = LR"(
	addiu	sp,-32
	sw		ra,0(sp)
	sw		s0,4(sp)
	sw		s1,8(sp)
	sw		s2,12(sp)
	sw		s3,16(sp)
	li		s0,%ctorTable%
	li		s1,%ctorTable%+%ctorTableSize%
	%outerLoopLabel%:
	lw		s2,(s0)
	lw		s3,4(s0)
	addiu	s0,8
	%innerLoopLabel%:
	lw		a0,(s2)
	jalr	a0
	addiu	s2,4h
	bne		s2,s3,%innerLoopLabel%
	nop
	bne		s0,s1,%outerLoopLabel%
	nop
	lw		ra,0(sp)
	lw		s0,4(sp)
	lw		s1,8(sp)
	lw		s2,12(sp)
	lw		s3,16(sp)
	jr		ra
	addiu	sp,32
	%ctorTable%:
	.word	%ctorContent%
)";

CAssemblerCommand* MipsElfRelocator::generateCtorStub(std::vector<ElfRelocatorCtor>& ctors)
{
	Parser parser;
	if (ctors.size() != 0)
	{
		// create constructor table
		std::wstring table;
		for (size_t i = 0; i < ctors.size(); i++)
		{
			if (i != 0)
				table += ',';
			table += formatString(L"%s,%s+0x%08X",ctors[i].symbolName,ctors[i].symbolName,ctors[i].size);
		}

		return parser.parseTemplate(mipsCtorTemplate,{
			{ L"%ctorTable%",		Global.symbolTable.getUniqueLabelName() },
			{ L"%ctorTableSize%",	formatString(L"%d",ctors.size()*8) },
			{ L"%outerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%innerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%ctorContent%",		table },
		});
	} else {
		return parser.parseTemplate(L"jr ra :: nop");
	}
}

CMipsArchitecture::CMipsArchitecture()
{
	FixLoadDelay = false;
	IgnoreLoadDelay = false;
	LoadDelay = false;
	LoadDelayRegister = 0;
	DelaySlot = false;
	Version = MARCH_INVALID;
}

CAssemblerCommand* CMipsArchitecture::parseDirective(Parser& parser)
{
	MipsParser mipsParser;
	return mipsParser.parseDirective(parser);
}

CAssemblerCommand* CMipsArchitecture::parseOpcode(Parser& parser)
{
	MipsParser mipsParser;

	CAssemblerCommand* macro = mipsParser.parseMacro(parser);
	if (macro != nullptr)
		return macro;

	return mipsParser.parseOpcode(parser);
}

void CMipsArchitecture::NextSection()
{
	LoadDelay = false;
	LoadDelayRegister = 0;
	DelaySlot = false;
}

void CMipsArchitecture::Revalidate()
{
	LoadDelay = false;
	LoadDelayRegister = 0;
	DelaySlot = false;
}

IElfRelocator* CMipsArchitecture::getElfRelocator()
{
	switch (Version)
	{
	case MARCH_PS2:
	case MARCH_PSP:
	case MARCH_N64:
		return new MipsElfRelocator();
	case MARCH_PSX:
	case MARCH_RSP:
	default:
		return NULL;
	}
}

void CMipsArchitecture::SetLoadDelay(bool Delay, int Register)
{
	LoadDelay = Delay;
	LoadDelayRegister = Register;
}
