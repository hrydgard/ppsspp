#include "stdafx.h"
#include "ArmRelocator.h"
#include "Util/Util.h"
#include "Arm.h"
#include "Core/Common.h"
#include <algorithm>

inline int signExtend(int value, int bitsLength)
{
	return (value << (32-bitsLength)) >> (32-bitsLength);
}

/*
	S = symbol address
	T = 1 if symbol is a thumb mode function, 0 otherwise
	P = offset of opcode
	A = addend
*/

bool ArmElfRelocator::relocateOpcode(int type, RelocationData& data)
{
	int t = (data.targetSymbolType == STT_FUNC && data.targetSymbolInfo != 0) ? 1 : 0;
	int p = (int) data.opcodeOffset;
	int s = (int) data.relocationBase;

	switch (type)
	{
	case R_ARM_ABS32:		// (S + A) | T
	case R_ARM_TARGET1:
		data.opcode = (int) (data.opcode + data.relocationBase) | t;
		break;
	case R_ARM_THM_CALL:	// ((S + A) | T) – P
		{
			unsigned short first = data.opcode & 0xFFFF;
			unsigned short second = (data.opcode >> 16) & 0xFFFF;
			int opField = ((first & 0x7FF) << 11) | (second & 0x7FF);
			int a = signExtend(opField << 1,23);
			int value = (s+a) - p;

			first &= ~0x7FF;
			second &= ~0x7FF;

			if (t == 1)
			{
				if (data.relocationBase % 2)
				{
					data.errorMessage = L"Branch target must be halfword aligned";
					return false;
				}
			} else {
				if (arm9 == false)
				{
					data.errorMessage = L"Cannot call ARM function from THUMB code without stub";
					return false;
				}

				if (data.relocationBase % 4)
				{
					data.errorMessage = L"Branch target must be word aligned";
					return false;
				}
				
				second = 0xE800;
			}

			if (abs(value) >= 0x400000)
			{
				data.errorMessage = formatString(L"Branch target %08X out of range",data.relocationBase);
				return false;
			}

			value >>= 1;
			first |= (value >> 11) & 0x7FF;
			second |= value & 0x7FF;
			data.opcode = first | (second << 16);
		}
		break;
	case R_ARM_CALL:		// ((S + A) | T) – P
	case R_ARM_JUMP24:		// ((S + A) | T) – P
		{
			int condField = (data.opcode >> 28) & 0xF;
			int opField = (data.opcode & 0xFFFFFF) << 2;
			data.opcode &= ~0xFFFFFF;

			int a = signExtend(opField,26);
			int value = (s+a) - p;

			if (t == 1)
			{
				if (data.relocationBase % 2)
				{
					data.errorMessage = L"Branch target must be halfword aligned";
					return false;
				}

				if (type == R_ARM_JUMP24)
				{
					data.errorMessage = L"Cannot jump from ARM to THUMB without link";
					return false;
				}

				if (arm9 == false)
				{
					data.errorMessage = L"Cannot call THUMB function from ARM code without stub";
					return false;
				}

				if (condField != 0xE)
				{
					data.errorMessage = L"Cannot convert conditional bl into blx";
					return false;
				}

				data.opcode = 0xFA000000;
				if (value & 2)
					data.opcode |= (1 << 24);
			} else {
				if (data.relocationBase % 4)
				{
					data.errorMessage = L"Branch target must be word aligned";
					return false;
				}
			}
			
			if (abs(value) >= 0x2000000)
			{
				data.errorMessage = formatString(L"Branch target %08X out of range",data.relocationBase);
				return false;
			}

			data.opcode |= (value >> 2) & 0xFFFFFF;
		}
		break;
	default:
		data.errorMessage = formatString(L"Unknown ARM relocation type %d",type);
		return false;
	}

	return true;
}

void ArmElfRelocator::setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType)
{
	if (symbolType == STT_FUNC)
	{
		data.targetSymbolInfo = symbolAddress & 1;
		symbolAddress &= ~1;
	}

	data.symbolAddress = symbolAddress;
	data.targetSymbolType = symbolType;
}

const wchar_t* ctorTemplate =
	L"push	r4-r7,r14\n"
	L"ldr	r4,=%ctorTable%\n"
	L"ldr	r5,=%ctorTable%+%ctorTableSize%\n"
	L"%outerLoopLabel%:\n"
	L"ldr	r6,[r4]\n"
	L"ldr	r7,[r4,4]\n"
	L"add	r4,8\n"
	L"%innerLoopLabel%:\n"
	L"ldr	r0,[r6]\n"
	L"add	r6,4\n"
	L".if %simpleMode%\n"
	L"	blx	r0\n"
	L".else\n"
	L"	bl	%stubName%\n"
	L".endif\n"
	L"cmp	r6,r7\n"
	L"blt	%innerLooplabel%\n"
	L"cmp	r4,r5\n"
	L"blt	%outerLoopLabel%\n"
	L".if %simpleMode%\n"
	L"	pop	r4-r7,r15\n"
	L".else\n"
	L"	pop	r4-r7\n"
	L"	pop	r0\n"
	L"	%stubName%:\n"
	L"	bx	r0\n"
	L".endif\n"
	L".pool\n"
	L"%ctorTable%:\n"
	L".word %ctorContent%"
;

CAssemblerCommand* ArmElfRelocator::generateCtorStub(std::vector<ElfRelocatorCtor>& ctors)
{
	std::wstring ctorText;

	Parser parser;
	if (ctors.size() != 0)
	{
		bool simpleMode = arm9 == false && Arm.GetThumbMode();

		// create constructor table
		std::wstring table;
		for (size_t i = 0; i < ctors.size(); i++)
		{
			if (i != 0)
				table += ',';
			table += formatString(L"%s,%s+0x%08X",ctors[i].symbolName,ctors[i].symbolName,ctors[i].size);
		}

		return parser.parseTemplate(ctorTemplate,{
			{ L"%ctorTable%",		Global.symbolTable.getUniqueLabelName() },
			{ L"%ctorTableSize%",	formatString(L"%d",ctors.size()*8) },
			{ L"%outerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%innerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%stubName%",		Global.symbolTable.getUniqueLabelName() },
			{ L"%simpleMode%",		simpleMode ? L"1" : L"0" },
			{ L"%ctorContent%",		table },
		});
	} else {
		return parser.parseTemplate(L"bx r14");
	}
}
