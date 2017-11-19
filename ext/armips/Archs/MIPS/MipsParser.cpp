#include "stdafx.h"
#include "MipsParser.h"
#include "Parser/Parser.h"
#include "Parser/ExpressionParser.h"
#include "Util/Util.h"
#include "Core/Common.h"
#include "PsxRelocator.h"
#include "MipsElfFile.h"
#include "Commands/CDirectiveFile.h"
#include "Parser/DirectivesParser.h"

#define CHECK(exp) if (!(exp)) return false;

const MipsRegisterDescriptor mipsRegisters[] = {
	{ L"r0", 0 },		{ L"zero", 0},		{ L"at", 1 },		{ L"r1", 1 },
	{ L"v0", 2 },		{ L"r2", 2 },		{ L"v1", 3 },		{ L"r3", 3 },
	{ L"a0", 4 },		{ L"r4", 4 },		{ L"a1", 5 },		{ L"r5", 5 },
	{ L"a2", 6 },		{ L"r6", 6 },		{ L"a3", 7 },		{ L"r7", 7 },
	{ L"t0", 8 },		{ L"r8", 8 },		{ L"t1", 9 },		{ L"r9", 9 },
	{ L"t2", 10 },		{ L"r10", 10 },		{ L"t3", 11 },		{ L"r11", 11 },
	{ L"t4", 12 },		{ L"r12", 12 },		{ L"t5", 13 },		{ L"r13", 13 },
	{ L"t6", 14 },		{ L"r14", 14 },		{ L"t7", 15 },		{ L"r15", 15 },
	{ L"s0", 16 },		{ L"r16", 16 },		{ L"s1", 17 },		{ L"r17", 17 },
	{ L"s2", 18 },		{ L"r18", 18 },		{ L"s3", 19 },		{ L"r19", 19 },
	{ L"s4", 20 },		{ L"r20", 20 },		{ L"s5", 21 },		{ L"r21", 21 },
	{ L"s6", 22 },		{ L"r22", 22 },		{ L"s7", 23 },		{ L"r23", 23 },
	{ L"t8", 24 },		{ L"r24", 24 },		{ L"t9", 25 },		{ L"r25", 25 },
	{ L"k0", 26 },		{ L"r26", 26 },		{ L"k1", 27 },		{ L"r27", 27 },
	{ L"gp", 28 },		{ L"r28", 28 },		{ L"sp", 29 },		{ L"r29", 29 },
	{ L"fp", 30 },		{ L"r30", 30 },		{ L"ra", 31 },		{ L"r31", 31 },
	{ L"s8", 30 },
};

const MipsRegisterDescriptor mipsFloatRegisters[] = {
	{ L"f0", 0 },		{ L"f1", 1 },		{ L"f2", 2 },		{ L"f3", 3 },
	{ L"f4", 4 },		{ L"f5", 5 },		{ L"f6", 6 },		{ L"f7", 7 },
	{ L"f8", 8 },		{ L"f9", 9 },		{ L"f00", 0 },		{ L"f01", 1 },
	{ L"f02", 2 },		{ L"f03", 3 },		{ L"f04", 4 },		{ L"f05", 5 },
	{ L"f06", 6 },		{ L"f07", 7 },		{ L"f08", 8 },		{ L"f09", 9 },
	{ L"f10", 10 },		{ L"f11", 11 },		{ L"f12", 12 },		{ L"f13", 13 },
	{ L"f14", 14 },		{ L"f15", 15 },		{ L"f16", 16 },		{ L"f17", 17 },
	{ L"f18", 18 },		{ L"f19", 19 },		{ L"f20", 20 },		{ L"f21", 21 },
	{ L"f22", 22 },		{ L"f23", 23 },		{ L"f24", 24 },		{ L"f25", 25 },
	{ L"f26", 26 },		{ L"f27", 27 },		{ L"f28", 28 },		{ L"f29", 29 },
	{ L"f30", 30 },		{ L"f31", 31 },
};

const MipsRegisterDescriptor mipsFpuControlRegisters[] = {
	{ L"fir", 0 },		{ L"fcr0", 0 },		{ L"fcsr", 31 },	{ L"fcr31", 31 },
};

const MipsRegisterDescriptor mipsCop0Registers[] = {
	{ L"index", 0},			{ L"random", 1 }, 		{ L"entrylo", 2 },
	{ L"entrylo0", 2 },		{ L"entrylo1", 3 },		{ L"context", 4 },
	{ L"pagemask", 5 },		{ L"wired", 6 },		{ L"badvaddr", 8 },
	{ L"count", 9 },		{ L"entryhi", 10 },		{ L"compare", 11 },
	{ L"status", 12 },		{ L"sr", 12 },			{ L"cause", 13 },
	{ L"epc", 14 },			{ L"prid", 15 },		{ L"config", 16 },
	{ L"lladdr", 17 },		{ L"watchlo", 18 },		{ L"watchhi", 19 },
	{ L"xcontext", 20 },	{ L"badpaddr", 23 },	{ L"ecc", 26 },
	{ L"perr", 26},			{ L"cacheerr", 27 },	{ L"taglo", 28 },
	{ L"taghi", 29 },		{ L"errorepc", 30 },
};

const MipsRegisterDescriptor mipsPs2Cop2FpRegisters[] = {
	{ L"vf0", 0 },		{ L"vf1", 1 },		{ L"vf2", 2 },		{ L"vf3", 3 },
	{ L"vf4", 4 },		{ L"vf5", 5 },		{ L"vf6", 6 },		{ L"vf7", 7 },
	{ L"vf8", 8 },		{ L"vf9", 9 },		{ L"vf00", 0 },		{ L"vf01", 1 },
	{ L"vf02", 2 },		{ L"vf03", 3 },		{ L"vf04", 4 },		{ L"vf05", 5 },
	{ L"vf06", 6 },		{ L"vf07", 7 },		{ L"vf08", 8 },		{ L"vf09", 9 },
	{ L"vf10", 10 },	{ L"vf11", 11 },	{ L"vf12", 12 },	{ L"vf13", 13 },
	{ L"vf14", 14 },	{ L"vf15", 15 },	{ L"vf16", 16 },	{ L"vf17", 17 },
	{ L"vf18", 18 },	{ L"vf19", 19 },	{ L"vf20", 20 },	{ L"vf21", 21 },
	{ L"vf22", 22 },	{ L"vf23", 23 },	{ L"vf24", 24 },	{ L"vf25", 25 },
	{ L"vf26", 26 },	{ L"vf27", 27 },	{ L"vf28", 28 },	{ L"vf29", 29 },
	{ L"vf30", 30 },	{ L"vf31", 31 },
};

const MipsRegisterDescriptor mipsRspCop0Registers[] = {
	{ L"sp_mem_addr", 0 },	{ L"sp_dram_addr", 1 }, { L"sp_rd_len", 2 },
	{ L"sp_wr_len", 3 },	{ L"sp_status", 4 },	{ L"sp_dma_full", 5 },
	{ L"sp_dma_busy", 6 },	{ L"sp_semaphore", 7 },	{ L"dpc_start", 8 },
	{ L"dpc_end", 9 },		{ L"dpc_current", 10 },	{ L"dpc_status", 11 },
	{ L"dpc_clock", 12 },	{ L"dpc_bufbusy", 13 },	{ L"dpc_pipebusy", 14 },
	{ L"dpc_tmem", 15 },
};

const MipsRegisterDescriptor mipsRspVectorRegisters[] = {
	{ L"v0", 0 },		{ L"v1", 1 },		{ L"v2", 2 },		{ L"v3", 3 },
	{ L"v4", 4 },		{ L"v5", 5 },		{ L"v6", 6 },		{ L"v7", 7 },
	{ L"v8", 8 },		{ L"v9", 9 },		{ L"v00", 0 },		{ L"v01", 1 },
	{ L"v02", 2 },		{ L"v03", 3 },		{ L"v04", 4 },		{ L"v05", 5 },
	{ L"v06", 6 },		{ L"v07", 7 },		{ L"v08", 8 },		{ L"v09", 9 },
	{ L"v10", 10 },		{ L"v11", 11 },		{ L"v12", 12 },		{ L"v13", 13 },
	{ L"v14", 14 },		{ L"v15", 15 },		{ L"v16", 16 },		{ L"v17", 17 },
	{ L"v18", 18 },		{ L"v19", 19 },		{ L"v20", 20 },		{ L"v21", 21 },
	{ L"v22", 22 },		{ L"v23", 23 },		{ L"v24", 24 },		{ L"v25", 25 },
	{ L"v26", 26 },		{ L"v27", 27 },		{ L"v28", 28 },		{ L"v29", 29 },
	{ L"v30", 30 },		{ L"v31", 31 },
};

CAssemblerCommand* parseDirectiveResetDelay(Parser& parser, int flags)
{
	Mips.SetIgnoreDelay(true);
	return new DummyCommand();
}

CAssemblerCommand* parseDirectiveFixLoadDelay(Parser& parser, int flags)
{
	Mips.SetFixLoadDelay(true);
	return new DummyCommand();
}

CAssemblerCommand* parseDirectiveLoadElf(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,2) == false)
		return nullptr;

	std::wstring inputName, outputName;
	if (list[0].evaluateString(inputName,true) == false)
		return nullptr;

	if (list.size() == 2)
	{
		if (list[1].evaluateString(outputName,true) == false)
			return nullptr;
		return new DirectiveLoadMipsElf(inputName,outputName);
	} else {
		return new DirectiveLoadMipsElf(inputName);
	}
}

CAssemblerCommand* parseDirectiveImportObj(Parser& parser, int flags)
{
	const Token& start = parser.peekToken();

	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,2) == false)
		return nullptr;

	std::wstring inputName;
	if (list[0].evaluateString(inputName,true) == false)
		return nullptr;
	
	if (list.size() == 2)
	{
		std::wstring ctorName;
		if (list[1].evaluateIdentifier(ctorName) == false)
			return nullptr;
		
		if (Mips.GetVersion() == MARCH_PSX)
		{
			parser.printError(start,L"Constructor not supported for PSX libraries");
			return new InvalidCommand();
		}

		return new DirectiveObjImport(inputName,ctorName);
	}

	if (Mips.GetVersion() == MARCH_PSX)
		return new DirectivePsxObjImport(inputName);
	else
		return new DirectiveObjImport(inputName);
}

const DirectiveMap mipsDirectives = {
	{ L".resetdelay",		{ &parseDirectiveResetDelay,	0 } },
	{ L".fixloaddelay",		{ &parseDirectiveFixLoadDelay,	0 } },
	{ L".loadelf",			{ &parseDirectiveLoadElf,		0 } },
	{ L".importobj",		{ &parseDirectiveImportObj,		0 } },
	{ L".importlib",		{ &parseDirectiveImportObj,		0 } },
};

CAssemblerCommand* MipsParser::parseDirective(Parser& parser)
{
	return parser.parseDirective(mipsDirectives);
}

bool MipsParser::parseRegisterNumber(Parser& parser, MipsRegisterValue& dest, int numValues)
{
	// check for $0 and $1
	if (parser.peekToken().type == TokenType::Dollar)
	{
		const Token& number = parser.peekToken(1);
		if (number.type == TokenType::Integer && number.intValue < numValues)
		{
			dest.name = formatString(L"$%d", number.intValue);
			dest.num = (int) number.intValue;

			parser.eatTokens(2);
			return true;
		}
	}

	return false;
}

bool MipsParser::parseRegisterTable(Parser& parser, MipsRegisterValue& dest, const MipsRegisterDescriptor* table, size_t count)
{
	int offset = 0;
	bool hasDollar = parser.peekToken().type == TokenType::Dollar;
	if (hasDollar)
		offset = 1;

	const Token &token = parser.peekToken(offset);

	if (token.type != TokenType::Identifier)
		return false;

	const std::wstring stringValue = token.getStringValue();
	for (size_t i = 0; i < count; i++)
	{
		if (stringValue == table[i].name)
		{
			dest.name = stringValue;
			dest.num = table[i].num;
			parser.eatTokens(hasDollar ? 2 : 1);
			return true;
		}
	}

	return false;
}

bool MipsParser::parseRegister(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::Normal;

	if (parseRegisterNumber(parser, dest, 32))
		return true;

	return parseRegisterTable(parser,dest,mipsRegisters,ARRAY_SIZE(mipsRegisters));
}

bool MipsParser::parseFpuRegister(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::Float;

	if (parseRegisterNumber(parser, dest, 32))
		return true;

	return parseRegisterTable(parser,dest,mipsFloatRegisters,ARRAY_SIZE(mipsFloatRegisters));
}

bool MipsParser::parseFpuControlRegister(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::FpuControl;

	if (parseRegisterNumber(parser, dest, 32))
		return true;

	return parseRegisterTable(parser,dest,mipsFpuControlRegisters,ARRAY_SIZE(mipsFpuControlRegisters));
}

bool MipsParser::parseCop0Register(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::Cop0;

	if (parseRegisterNumber(parser, dest, 32))
		return true;

	return parseRegisterTable(parser,dest,mipsCop0Registers,ARRAY_SIZE(mipsCop0Registers));
}

bool MipsParser::parsePs2Cop2Register(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::Ps2Cop2;
	return parseRegisterTable(parser,dest,mipsPs2Cop2FpRegisters,ARRAY_SIZE(mipsPs2Cop2FpRegisters));
}

bool MipsParser::parseRspCop0Register(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::RspCop0;

	if (parseRegisterNumber(parser, dest, 32))
		return true;

	return parseRegisterTable(parser,dest,mipsRspCop0Registers,ARRAY_SIZE(mipsRspCop0Registers));
}

bool MipsParser::parseRspVectorRegister(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::RspVector;
	return parseRegisterTable(parser,dest,mipsRspVectorRegisters,ARRAY_SIZE(mipsRspVectorRegisters));
}

bool MipsParser::parseRspBroadcastElement(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::RspBroadcastElement;

	if (parser.peekToken().type == TokenType::LBrack)
	{
		static const MipsRegisterDescriptor rspElementNames[] = {
			{ L"0q", 2 },		{ L"1q", 3 },		{ L"0h", 4 },		{ L"1h", 5 },
			{ L"2h", 6 },		{ L"3h", 7 },		{ L"0w", 8 },		{ L"0", 8 },
			{ L"1w", 9 },		{ L"1", 9 },		{ L"2w", 10 },		{ L"2", 10 },
			{ L"3w", 11 },		{ L"3", 11 },		{ L"4w", 12 },		{ L"4", 12 },
			{ L"5w", 13 },		{ L"5", 13 },		{ L"6w", 14 },		{ L"6", 14 },
			{ L"7w", 15 },		{ L"7", 15 },
		};

		parser.eatToken();

		if (parseRegisterNumber(parser, dest, 16))
			return parser.nextToken().type == TokenType::RBrack;

		const Token& token = parser.nextToken();

		if (token.type != TokenType::Integer && token.type != TokenType::NumberString)
			return false;

		//ignore the numerical values, just use the original text as an identifier
		std::wstring stringValue = token.getOriginalText();
		if (std::any_of(stringValue.begin(), stringValue.end(), iswupper))
		{
			std::transform(stringValue.begin(), stringValue.end(), stringValue.begin(), towlower);
		}

		for (size_t i = 0; i < ARRAY_SIZE(rspElementNames); i++)
		{
			if (stringValue == rspElementNames[i].name)
			{
				dest.num = rspElementNames[i].num;
				dest.name = rspElementNames[i].name;

				return parser.nextToken().type == TokenType::RBrack;
			}
		}

		return false;
	}

	dest.num = 0;
	dest.name = L"";

	return true;

}

bool MipsParser::parseRspScalarElement(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::RspScalarElement;

	if (parser.nextToken().type != TokenType::LBrack)
		return false;

	const Token &token = parser.nextToken();

	if (token.type != TokenType::Integer || token.intValue >= 8)
		return false;

	dest.name = formatString(L"%d", token.intValue);
	dest.num = token.intValue;

	return parser.nextToken().type == TokenType::RBrack;
}

bool MipsParser::parseRspOffsetElement(Parser& parser, MipsRegisterValue& dest)
{
	dest.type = MipsRegisterType::RspOffsetElement;

	if (parser.peekToken().type == TokenType::LBrack)
	{
		parser.eatToken();

		const Token &token = parser.nextToken();

		if (token.type != TokenType::Integer || token.intValue >= 16)
			return false;

		dest.name = formatString(L"%d", token.intValue);
		dest.num = token.intValue;

		return parser.nextToken().type == TokenType::RBrack;
	}

	dest.num = 0;
	dest.name = L"";

	return true;
}

static bool decodeDigit(wchar_t digit, int& dest)
{
	if (digit >= '0' && digit <= '9')
	{
		dest = digit-'0';
		return true;
	}
	return false;
}

bool MipsParser::parseVfpuRegister(Parser& parser, MipsRegisterValue& reg, int size)
{
	const Token& token = parser.peekToken();
	const std::wstring stringValue = token.getStringValue();
	if (token.type != TokenType::Identifier || stringValue.size() != 4)
		return false;

	int mtx,col,row;
	if (decodeDigit(stringValue[1],mtx) == false) return false;
	if (decodeDigit(stringValue[2],col) == false) return false;
	if (decodeDigit(stringValue[3],row) == false) return false;
	wchar_t mode = towlower(stringValue[0]);

	if (size < 0 || size > 3)
		return false;

	if (row > 3 || col > 3 || mtx > 7)
		return false;

	reg.num = 0;
	switch (mode)
	{
	case 'r':					// transposed vector
		reg.num |= (1 << 5);
		std::swap(col,row);		// fallthrough
	case 'c':					// vector	
		reg.type = MipsRegisterType::VfpuVector;

		switch (size)
		{
		case 1:	// pair
		case 3: // quad
			if (row & 1)
				return false;
			break;
		case 2:	// triple
			if (row & 2)
				return false;
			row <<= 1;
			break;
		default:
			return false;
		}
		break;
	case 's':					// single
		reg.type = MipsRegisterType::VfpuVector;

		if (size != 0)
			return false;
		break;
	case 'e':					// transposed matrix
		reg.num |= (1 << 5);	// fallthrough
	case 'm':					// matrix
		reg.type = MipsRegisterType::VfpuMatrix;

		// check size
		switch (size)
		{
		case 1:	// 2x2
		case 3:	// 4x4
			if (row & 1)
				return false;
			break;
		case 2:	// 3x3
			if (row & ~1)
				return false;
			row <<= 1;
			break;
		default:
			return false;
		}
		break;
	default:
		return false;
	}

	reg.num |= mtx << 2;
	reg.num |= col;
	reg.num |= row << 5;

	reg.name = stringValue;
	parser.eatToken();
	return true;
}

bool MipsParser::parseVfpuControlRegister(Parser& parser, MipsRegisterValue& reg)
{
	static const wchar_t* vfpuCtrlNames[16] = {
		L"spfx",	L"tpfx",	L"dpfx",	L"cc",
		L"inf4",	L"rsv5",	L"rsv6",	L"rev",
		L"rcx0",	L"rcx1",	L"rcx2",	L"rcx3",
		L"rcx4",	L"rcx5",	L"rcx6",	L"rcx7",
	};

	const Token& token = parser.peekToken();
	const std::wstring stringValue = token.getStringValue();

	if (token.type == TokenType::Identifier)
	{
		for (int i = 0; i < 16; i++)
		{
			if (stringValue == vfpuCtrlNames[i])
			{
				reg.num = i;
				reg.name = vfpuCtrlNames[i];

				parser.eatToken();
				return true;
			}
		}
	} else if (token.type == TokenType::Integer && token.intValue <= 15)
	{
		reg.num = (int) token.intValue;
		reg.name = vfpuCtrlNames[reg.num];

		parser.eatToken();
		return true;
	}

	return false;
}

bool MipsParser::parseImmediate(Parser& parser, Expression& dest)
{
	// check for (reg) or reg sequence
	TokenizerPosition pos = parser.getTokenizer()->getPosition();

	bool hasParen = parser.peekToken().type == TokenType::LParen;
	if (hasParen)
		parser.eatToken();

	MipsRegisterValue tempValue;
	bool isRegister = parseRegister(parser,tempValue);
	parser.getTokenizer()->setPosition(pos);

	if (isRegister)
		return false;

	dest = parser.parseExpression();
	return dest.isLoaded();
}

bool MipsParser::matchSymbol(Parser& parser, wchar_t symbol)
{
	switch (symbol)
	{
	case '(':
		return parser.matchToken(TokenType::LParen);
	case ')':
		return parser.matchToken(TokenType::RParen);
	case ',':
		return parser.matchToken(TokenType::Comma);
	}

	return false;
}

bool MipsParser::parseVcstParameter(Parser& parser, int& result)
{
	static TokenSequenceParser sequenceParser;

	// initialize on first use
	if (sequenceParser.getEntryCount() == 0)
	{
		// maxfloat
		sequenceParser.addEntry(1,
			{TokenType::Identifier},
			{L"maxfloat"}
		);
		// sqrt(2)
		sequenceParser.addEntry(2,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen},
			{L"sqrt", INT64_C(2)}
		);
		// sqrt(1/2)
		sequenceParser.addEntry(3,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::Div, TokenType::Integer, TokenType::RParen},
			{L"sqrt", INT64_C(1), INT64_C(2)}
		);
		// sqrt(0.5)
		sequenceParser.addEntry(3,
			{TokenType::Identifier, TokenType::LParen, TokenType::Float, TokenType::RParen},
			{L"sqrt", 0.5}
		);
		// 2/sqrt(pi)
		sequenceParser.addEntry(4,
			{TokenType::Integer, TokenType::Div, TokenType::Identifier, TokenType::LParen, TokenType::Identifier, TokenType::RParen},
			{INT64_C(2), L"sqrt", L"pi"}
		);
		// 2/pi
		sequenceParser.addEntry(5,
			{TokenType::Integer, TokenType::Div, TokenType::Identifier},
			{INT64_C(2), L"pi"}
		);
		// 1/pi
		sequenceParser.addEntry(6,
			{TokenType::Integer, TokenType::Div, TokenType::Identifier},
			{INT64_C(1), L"pi"}
		);
		// pi/4
		sequenceParser.addEntry(7,
			{TokenType::Identifier, TokenType::Div, TokenType::Integer},
			{L"pi", INT64_C(4)}
		);
		// pi/2
		sequenceParser.addEntry(8,
			{TokenType::Identifier, TokenType::Div, TokenType::Integer},
			{L"pi", INT64_C(2)}
		);
		// pi/6 - early because "pi" is a prefix of it
		sequenceParser.addEntry(16,
			{TokenType::Identifier, TokenType::Div, TokenType::Integer},
			{L"pi", INT64_C(6)}
		);
		// pi
		sequenceParser.addEntry(9,
			{TokenType::Identifier},
			{L"pi"}
		);
		// e
		sequenceParser.addEntry(10,
			{TokenType::Identifier},
			{L"e"}
		);
		// log2(e)
		sequenceParser.addEntry(11,
			{TokenType::Identifier, TokenType::LParen, TokenType::Identifier, TokenType::RParen},
			{L"log2", L"e"}
		);
		// log10(e)
		sequenceParser.addEntry(12,
			{TokenType::Identifier, TokenType::LParen, TokenType::Identifier, TokenType::RParen},
			{L"log10", L"e"}
		);
		// ln(2)
		sequenceParser.addEntry(13,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen},
			{L"ln", INT64_C(2)}
		);
		// ln(10)
		sequenceParser.addEntry(14,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen},
			{L"ln", INT64_C(10)}
		);
		// 2*pi
		sequenceParser.addEntry(15,
			{TokenType::Integer, TokenType::Mult, TokenType::Identifier},
			{INT64_C(2), L"pi"}
		);
		// log10(2)
		sequenceParser.addEntry(17,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen},
			{L"log10", INT64_C(2)}
		);
		// log2(10)
		sequenceParser.addEntry(18,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen},
			{L"log2", INT64_C(10)}
		);
		// sqrt(3)/2
		sequenceParser.addEntry(19,
			{TokenType::Identifier, TokenType::LParen, TokenType::Integer, TokenType::RParen, TokenType::Div, TokenType::Integer},
			{L"sqrt", INT64_C(3), INT64_C(2)}
		);
	}

	return sequenceParser.parse(parser,result);
}

bool MipsParser::parseVfpuVrot(Parser& parser, int& result, int size)
{
	int sin = -1;
	int cos = -1;
	bool negSine = false;
	int sineCount = 0;

	if (parser.nextToken().type != TokenType::LBrack)
		return false;
	
	int numElems = size+1;
	for (int i = 0; i < numElems; i++)
	{
		const Token* tokenFinder = &parser.nextToken();
		
		if (i != 0)
		{
			if (tokenFinder->type != TokenType::Comma)
				return false;

			tokenFinder = &parser.nextToken();
		}

		bool isNeg = tokenFinder->type == TokenType::Minus;
		if (isNeg)
			tokenFinder = &parser.nextToken();

		const Token& token = *tokenFinder;

		const std::wstring stringValue = token.getStringValue();
		if (token.type != TokenType::Identifier || stringValue.size() != 1)
			return false;

		switch (stringValue[0])
		{
		case 's':
			// if one is negative, all have to be
			if ((!isNeg && negSine) || (isNeg && !negSine && sineCount > 0))
				return false;

			negSine = negSine || isNeg;
			sin = i;
			sineCount++;
			break;
		case 'c':
			// can't be negative, or happen twice
			if (isNeg || cos != -1)
				return false;
			cos = i;
			break;
		case '0':
			if (isNeg)
				return false;
			break;
		default:
			return false;
		}
	}
	
	if (parser.nextToken().type != TokenType::RBrack)
		return false;
	
	result = negSine ? 0x10 : 0;

	if (sin == -1 && cos == -1)
	{
		return false;
	} else if (sin == -1)
	{
		if (numElems == 4)
			return false;
		
		result |= cos;
		result |= ((size+1) << 2);
	} else if (cos == -1)
	{
		if (numElems == 4)
			return false;

		if (sineCount == 1)
		{
			result |= (size+1);
			result |= (sin << 2);
		} else if (sineCount == numElems)
		{
			result |= (size+1);
			result |= ((size+1) << 2);
		} else {
			return false;
		}
	} else {
		if (sineCount > 1)
		{
			if (sineCount+1 != numElems)
				return false;
			
			result |= cos;
			result |= (cos << 2);
		} else {
			result |= cos;
			result |= (sin << 2);
		}
	}

	return true;
}

bool MipsParser::parseVfpuCondition(Parser& parser, int& result)
{
	static const wchar_t* conditions[] = {
		L"fl", L"eq", L"lt", L"le", L"tr", L"ne", L"ge", L"gt",
		L"ez", L"en", L"ei", L"es", L"nz", L"nn", L"ni", L"ns"
	};

	const Token& token = parser.nextToken();
	if (token.type != TokenType::Identifier)
		return false;

	const std::wstring stringValue = token.getStringValue();
	for (size_t i = 0; i < ARRAY_SIZE(conditions); i++)
	{
		if (stringValue == conditions[i])
		{
			result = i;
			return true;
		}
	}

	return false;
}

bool MipsParser::parseVpfxsParameter(Parser& parser, int& result)
{
	static TokenSequenceParser sequenceParser;

	// initialize on first use
	if (sequenceParser.getEntryCount() == 0)
	{
		// 0
		sequenceParser.addEntry(0, {TokenType::Integer}, {INT64_C(0)} );
		// 1
		sequenceParser.addEntry(1, {TokenType::Integer}, {INT64_C(1)} );
		// 2
		sequenceParser.addEntry(2, {TokenType::Integer}, {INT64_C(2)} );
		// 1/2
		sequenceParser.addEntry(3, {TokenType::Integer, TokenType::Div, TokenType::Integer}, {INT64_C(1), INT64_C(2)} );
		// 3
		sequenceParser.addEntry(4, {TokenType::Integer}, {INT64_C(3)} );
		// 1/3
		sequenceParser.addEntry(5, {TokenType::Integer, TokenType::Div, TokenType::Integer}, {INT64_C(1), INT64_C(3)} );
		// 1/4
		sequenceParser.addEntry(6, {TokenType::Integer, TokenType::Div, TokenType::Integer}, {INT64_C(1), INT64_C(4)} );
		// 1/6
		sequenceParser.addEntry(7, {TokenType::Integer, TokenType::Div, TokenType::Integer}, {INT64_C(1), INT64_C(6)} );
	}

	if (parser.nextToken().type != TokenType::LBrack)
		return false;
	
	for (int i = 0; i < 4; i++)
	{
		const Token *tokenFinder = &parser.nextToken();

		if (i != 0)
		{
			if (tokenFinder->type != TokenType::Comma)
				return false;

			tokenFinder = &parser.nextToken();
		}
		
		// negation
		if (tokenFinder->type == TokenType::Minus)
		{
			result |= 1 << (16+i);
			tokenFinder = &parser.nextToken();
		}

		// abs
		bool abs = false;
		if (tokenFinder->type == TokenType::BitOr)
		{
			result |= 1 << (8+i);
			abs = true;
			tokenFinder = &parser.nextToken();
		}

		const Token& token = *tokenFinder;
		
		// check for register
		const wchar_t* reg;
		static const wchar_t* vpfxstRegisters = L"xyzw";
		const std::wstring stringValue = token.getStringValue();
		if (stringValue.size() == 1 && (reg = wcschr(vpfxstRegisters,stringValue[0])) != nullptr)
		{
			result |= (reg-vpfxstRegisters) << (i*2);

			if (abs && parser.nextToken().type != TokenType::BitOr)
				return false;

			continue;
		}
		
		// abs is invalid with constants
		if (abs)
			return false;

		result |= 1 << (12+i);

		int constNum = -1;
		if (sequenceParser.parse(parser,constNum) == false)
			return false;
		
		result |= (constNum & 3) << (i*2);
		if (constNum & 4)
			result |= 1 << (8+i);
	}

	return parser.nextToken().type == TokenType::RBrack;
}

bool MipsParser::parseVpfxdParameter(Parser& parser, int& result)
{
	static TokenSequenceParser sequenceParser;

	// initialize on first use
	if (sequenceParser.getEntryCount() == 0)
	{
		// 0-1
		sequenceParser.addEntry(1,
			{TokenType::Integer, TokenType::Minus, TokenType::Integer},
			{INT64_C(0), INT64_C(1)} );
		// 0-1
		sequenceParser.addEntry(-1,
			{TokenType::Integer, TokenType::Minus, TokenType::NumberString},
			{INT64_C(0), L"1m"} );
		// 0:1
		sequenceParser.addEntry(1,
			{TokenType::Integer, TokenType::Colon, TokenType::Integer},
			{INT64_C(0), INT64_C(1)} );
		// 0:1
		sequenceParser.addEntry(-1,
			{TokenType::Integer, TokenType::Colon, TokenType::NumberString},
			{INT64_C(0), L"1m"} );
		// -1-1
		sequenceParser.addEntry(3,
			{TokenType::Minus, TokenType::Integer, TokenType::Minus, TokenType::Integer},
			{INT64_C(1), INT64_C(1)} );
		// -1-1m
		sequenceParser.addEntry(-3,
			{TokenType::Minus, TokenType::Integer, TokenType::Minus, TokenType::NumberString},
			{INT64_C(1), L"1m"} );
		// -1:1
		sequenceParser.addEntry(3,
			{TokenType::Minus, TokenType::Integer, TokenType::Colon, TokenType::Integer},
			{INT64_C(1), INT64_C(1)} );
		// -1:1m
		sequenceParser.addEntry(-3,
			{TokenType::Minus, TokenType::Integer, TokenType::Colon, TokenType::NumberString},
			{INT64_C(1), L"1m"} );
	}

	for (int i = 0; i < 4; i++)
	{
		if (i != 0)
		{
			if (parser.nextToken().type != TokenType::Comma)
				return false;
		}

		parser.eatToken();
		
		int num = 0;
		if (sequenceParser.parse(parser,num) == false)
			return false;

		// m versions
		if (num < 0)
		{
			result |= 1 << (8+i);
			num = abs(num);
		}

		result |= num << (2*i);
	}
	
	return parser.nextToken().type == TokenType::RBrack;
}


bool MipsParser::decodeCop2BranchCondition(const std::wstring& text, size_t& pos, int& result)
{
	if (pos+3 == text.size())
	{
		if (startsWith(text,L"any",pos))
		{
			result = 4;
			pos += 3;
			return true;
		}
		if (startsWith(text,L"all",pos))
		{
			result = 5;
			pos += 3;
			return true;
		}
	} else if (pos+1 == text.size())
	{
		switch (text[pos++])
		{
		case 'x':
		case '0':
			result = 0;
			return true;
		case 'y':
		case '1':
			result = 1;
			return true;
		case 'z':
		case '2':
			result = 2;
			return true;
		case 'w':
		case '3':
			result = 3;
			return true;
		case '4':
			result = 4;
			return true;
		case '5':
			result = 5;
			return true;
		}

		// didn't match it
		pos--;
	}

	return false;
}

bool MipsParser::parseCop2BranchCondition(Parser& parser, int& result)
{
	const Token& token = parser.nextToken();

	if (token.type == TokenType::Integer)
	{
		result = (int) token.intValue;
		return token.intValue <= 5;
	}

	if (token.type != TokenType::Identifier)
		return false;

	size_t pos = 0;
	return decodeCop2BranchCondition(token.getStringValue(),pos,result);
}

bool MipsParser::parseWb(Parser& parser)
{
	const Token& token = parser.nextToken();
	if (token.type != TokenType::Identifier)
		return false;

	return token.getStringValue() == L"wb";
}

static bool decodeImmediateSize(const char*& encoding, MipsImmediateType& dest)
{
	if (*encoding == 'h')	// half float
	{
		encoding++;
		dest = MipsImmediateType::ImmediateHalfFloat;
	} else {
		int num = 0;
		while (*encoding >= '0' && *encoding <= '9')
		{
			num = num*10 + *encoding-'0';
			encoding++;
		}

		switch (num)
		{
		case 5:
			dest = MipsImmediateType::Immediate5;
			break;
		case 7:
			dest = MipsImmediateType::Immediate7;
			break;
		case 10:
			dest = MipsImmediateType::Immediate10;
			break;
		case 16:
			dest = MipsImmediateType::Immediate16;
			break;
		case 20:
			dest = MipsImmediateType::Immediate20;
			break;
		case 26:
			dest = MipsImmediateType::Immediate26;
			break;
		default:
			return false;
		}
	}

	return true;
}

bool MipsParser::decodeVfpuType(const std::wstring& name, size_t& pos, int& dest)
{
	if (pos >= name.size())
		return false;

	switch (name[pos++])
	{
	case 's':
		dest = 0;
		return true;
	case 'p':
		dest = 1;
		return true;
	case 't':
		dest = 2;
		return true;
	case 'q':
		dest = 3;
		return true;
	}

	pos--;
	return false;
}

bool MipsParser::decodeOpcode(const std::wstring& name, const tMipsOpcode& opcode)
{
	const char* encoding = opcode.name;
	size_t pos = 0;

	registers.reset();
	immediate.reset();
	opcodeData.reset();
	hasFixedSecondaryImmediate = false;

	while (*encoding != 0)
	{
		switch (*encoding++)
		{
		case 'S':
			CHECK(decodeVfpuType(name,pos,opcodeData.vfpuSize));
			break;
		case 'B':
			CHECK(decodeCop2BranchCondition(name,pos,immediate.secondary.originalValue));
			immediate.secondary.type = MipsImmediateType::Cop2BranchType;
			immediate.secondary.value = immediate.secondary.originalValue;
			hasFixedSecondaryImmediate = true;
			break;
		default:
			CHECK(pos < name.size());
			CHECK(*(encoding-1) == name[pos++]);
			break;
		}
	}

	return pos >= name.size();
}

void MipsParser::setOmittedRegisters(const tMipsOpcode& opcode)
{
	// copy over omitted registers
	if (opcode.flags & MO_RSD)
		registers.grd = registers.grs;

	if (opcode.flags & MO_RST)
		registers.grt = registers.grs;

	if (opcode.flags & MO_RDT)
		registers.grt = registers.grd;

	if (opcode.flags & MO_FRSD)
		registers.frd = registers.frs;

	if (opcode.flags & MO_RSPVRSD)
		registers.rspvrd = registers.rspvrs;
}

bool MipsParser::parseParameters(Parser& parser, const tMipsOpcode& opcode)
{
	const char* encoding = opcode.encoding;

	// initialize opcode variables
	immediate.primary.type = MipsImmediateType::None;
	if (!hasFixedSecondaryImmediate)
		immediate.secondary.type = MipsImmediateType::None;

	if (opcodeData.vfpuSize == -1)
	{
		if (opcode.flags & MO_VFPU_SINGLE)
			opcodeData.vfpuSize = 0;
		else if (opcode.flags & MO_VFPU_PAIR)
			opcodeData.vfpuSize = 1;
		else if (opcode.flags & MO_VFPU_TRIPLE)
			opcodeData.vfpuSize = 2;
		else if (opcode.flags & MO_VFPU_QUAD)
			opcodeData.vfpuSize = 3;
	}

	// parse parameters
	MipsRegisterValue tempRegister;
	int actualSize = opcodeData.vfpuSize;

	while (*encoding != 0)
	{
		switch (*encoding++)
		{
		case 't':	// register
			CHECK(parseRegister(parser,registers.grt));
			break;
		case 'd':	// register
			CHECK(parseRegister(parser,registers.grd));
			break;
		case 's':	// register
			CHECK(parseRegister(parser,registers.grs));
			break;
		case 'T':	// float register
			CHECK(parseFpuRegister(parser,registers.frt));
			break;
		case 'D':	// float register
			CHECK(parseFpuRegister(parser,registers.frd));
			break;
		case 'S':	// float register
			CHECK(parseFpuRegister(parser,registers.frs));
			break;
		case 'f':	// fpu control register
			CHECK(parseFpuControlRegister(parser,registers.frs));
			break;
		case 'z':	// cop0 register
			CHECK(parseCop0Register(parser,registers.grd));
			break;
		case 'v':	// psp vfpu reg
			if (*encoding == 'S')
			{
				encoding++;
				actualSize = 0;
			}

			switch (*encoding++)
			{
			case 's':
				CHECK(parseVfpuRegister(parser,registers.vrs,actualSize));
				CHECK(registers.vrs.type == MipsRegisterType::VfpuVector);
				if (opcode.flags & MO_VFPU_6BIT) CHECK(!(registers.vrs.num & 0x40));
				break;
			case 't':
				CHECK(parseVfpuRegister(parser,registers.vrt,actualSize));
				CHECK(registers.vrt.type == MipsRegisterType::VfpuVector);
				if (opcode.flags & MO_VFPU_6BIT) CHECK(!(registers.vrt.num & 0x40));
				break;
			case 'd':
				CHECK(parseVfpuRegister(parser,registers.vrd,actualSize));
				CHECK(registers.vrd.type == MipsRegisterType::VfpuVector);
				if (opcode.flags & MO_VFPU_6BIT) CHECK(!(registers.vrd.num & 0x40));
				break;
			case 'c':
				CHECK(parseVfpuControlRegister(parser,registers.vrd));
				break;
			default:
				return false;
			}
			break;
		case 'm':	// vfpu matrix register
			switch (*encoding++)
			{
			case 's':
				CHECK(parseVfpuRegister(parser,registers.vrs,opcodeData.vfpuSize));
				CHECK(registers.vrs.type == MipsRegisterType::VfpuMatrix);
				if (opcode.flags & MO_TRANSPOSE_VS)
					registers.vrs.num ^= 0x20;
				break;
			case 't':
				CHECK(parseVfpuRegister(parser,registers.vrt,opcodeData.vfpuSize));
				CHECK(registers.vrt.type == MipsRegisterType::VfpuMatrix);
				break;
			case 'd':
				CHECK(parseVfpuRegister(parser,registers.vrd,opcodeData.vfpuSize));
				CHECK(registers.vrd.type == MipsRegisterType::VfpuMatrix);
				break;
			default:
				return false;
			}
			break;
		case 'V':	// ps2 vector reg
			switch (*encoding++)
			{
			case 't':	// register
				CHECK(parsePs2Cop2Register(parser,registers.ps2vrt));
				break;
			case 'd':	// register
				CHECK(parsePs2Cop2Register(parser,registers.ps2vrd));
				break;
			case 's':	// register
				CHECK(parsePs2Cop2Register(parser,registers.ps2vrs));
				break;
			default:
				return false;
			}
			break;
		case 'r':	// forced register
			CHECK(parseRegister(parser,tempRegister));
			CHECK(tempRegister.num == *encoding++);
			break;
		case 'R':	// rsp register
			switch (*encoding++)
			{
			case 'z':	// cop0 register
				CHECK(parseRspCop0Register(parser,registers.grd));
				break;
			case 't':	// vector register
				CHECK(parseRspVectorRegister(parser,registers.rspvrt));
				break;
			case 'd':	// vector register
				CHECK(parseRspVectorRegister(parser,registers.rspvrd));
				break;
			case 's':	// vector register
				CHECK(parseRspVectorRegister(parser,registers.rspvrs));
				break;
			case 'e':	// vector broadcast element
				CHECK(parseRspBroadcastElement(parser,registers.rspve));
				break;
			case 'l':	// vector scalar element
				CHECK(parseRspScalarElement(parser,registers.rspve));
				break;
			case 'm':	// vector scalar destination element
				CHECK(parseRspScalarElement(parser,registers.rspvde));
				break;
			case 'o':	// vector byte offset element
				CHECK(parseRspOffsetElement(parser,registers.rspvealt));
				break;
			default:
				return false;
			}
			break;
		case 'i':	// primary immediate
			CHECK(parseImmediate(parser,immediate.primary.expression));
			allowFunctionCallExpression(*encoding != '(');
			CHECK(decodeImmediateSize(encoding,immediate.primary.type));
			allowFunctionCallExpression(true);
			break;
		case 'j':	// secondary immediate
			switch (*encoding++)
			{
			case 'c':
				CHECK(parseImmediate(parser,immediate.secondary.expression));
				immediate.secondary.type = MipsImmediateType::CacheOp;
				break;
			case 'e':
				CHECK(parseImmediate(parser,immediate.secondary.expression));
				immediate.secondary.type = MipsImmediateType::Ext;
				break;
			case 'i':
				CHECK(parseImmediate(parser,immediate.secondary.expression));
				immediate.secondary.type = MipsImmediateType::Ins;
				break;
			case 'b':
				CHECK(parseCop2BranchCondition(parser,immediate.secondary.originalValue));
				immediate.secondary.type = MipsImmediateType::Cop2BranchType;
				immediate.secondary.value = immediate.secondary.originalValue;
				break;
			default:
				return false;
			}
			break;
		case 'C':	// vfpu condition
			CHECK(parseVfpuCondition(parser,opcodeData.vectorCondition));
			break;
		case 'W':	// vfpu argument
			switch (*encoding++)
			{
			case 's':
				CHECK(parseVpfxsParameter(parser,immediate.primary.originalValue));
				immediate.primary.value = immediate.primary.originalValue;
				immediate.primary.type = MipsImmediateType::Immediate20_0;
				break;
			case 'd':
				CHECK(parseVpfxdParameter(parser,immediate.primary.originalValue));
				immediate.primary.value = immediate.primary.originalValue;
				immediate.primary.type = MipsImmediateType::Immediate16;
				break;
			case 'c':
				CHECK(parseVcstParameter(parser,immediate.primary.originalValue));
				immediate.primary.value = immediate.primary.originalValue;
				immediate.primary.type = MipsImmediateType::Immediate5;
				break;
			case 'r':
				CHECK(parseVfpuVrot(parser,immediate.primary.originalValue,opcodeData.vfpuSize));
				immediate.primary.value = immediate.primary.originalValue;
				immediate.primary.type = MipsImmediateType::Immediate5;
				break;
			default:
				return false;
			}
			break;
		case 'w':	// 'wb' characters
			CHECK(parseWb(parser));
			break;
		default:
			CHECK(matchSymbol(parser,*(encoding-1)));
			break;
		}
	}

	opcodeData.opcode = opcode;
	setOmittedRegisters(opcode);

	// the next token has to be a separator, else the parameters aren't
	// completely parsed

	return parser.nextToken().type == TokenType::Separator;

}

CMipsInstruction* MipsParser::parseOpcode(Parser& parser)
{
	if (parser.peekToken().type != TokenType::Identifier)
		return nullptr;

	const Token &token = parser.nextToken();

	bool paramFail = false;
	const MipsArchDefinition& arch = mipsArchs[Mips.GetVersion()];
	const std::wstring stringValue = token.getStringValue();

	for (int z = 0; MipsOpcodes[z].name != NULL; z++)
	{
		if ((MipsOpcodes[z].archs & arch.supportSets) == 0)
			continue;
		if ((MipsOpcodes[z].archs & arch.excludeMask) != 0)
			continue;

		if ((MipsOpcodes[z].flags & MO_64BIT) && !(arch.flags & MO_64BIT))
			continue;
		if ((MipsOpcodes[z].flags & MO_FPU) && !(arch.flags & MO_FPU))
			continue;
		if ((MipsOpcodes[z].flags & MO_DFPU) && !(arch.flags & MO_DFPU))
			continue;

		if (decodeOpcode(stringValue,MipsOpcodes[z]) == true)
		{
			TokenizerPosition tokenPos = parser.getTokenizer()->getPosition();

			if (parseParameters(parser,MipsOpcodes[z]) == true)
			{
				// success, return opcode
				return new CMipsInstruction(opcodeData,immediate,registers);
			}

			parser.getTokenizer()->setPosition(tokenPos);
			paramFail = true;
		}
	}

	if (paramFail == true)
		parser.printError(token,L"MIPS parameter failure");
	else
		parser.printError(token,L"Invalid MIPS opcode '%s'",stringValue);

	return nullptr;
}

bool MipsParser::parseMacroParameters(Parser& parser, const MipsMacroDefinition& macro)
{
	const wchar_t* encoding = (const wchar_t*) macro.args;

	while (*encoding != 0)
	{
		switch (*encoding++)
		{
		case 't':	// register
			CHECK(parseRegister(parser,registers.grt));
			break;
		case 'd':	// register
			CHECK(parseRegister(parser,registers.grd));
			break;
		case 's':	// register
			CHECK(parseRegister(parser,registers.grs));
			break;
		case 'S':	// register
			CHECK(parseFpuRegister(parser,registers.frs));
			break;
		case 'i':	// primary immediate
			allowFunctionCallExpression(*encoding != '(');
			CHECK(parseImmediate(parser,immediate.primary.expression));
			allowFunctionCallExpression(true);
			break;
		case 'I':	// secondary immediate
			allowFunctionCallExpression(*encoding != '(');
			CHECK(parseImmediate(parser,immediate.secondary.expression));
			allowFunctionCallExpression(true);
			break;
		default:
			CHECK(matchSymbol(parser,*(encoding-1)));
			break;
		}
	}

	// lw rx,imm is a prefix of lw rx,imm(ry)
	if (parser.peekToken().type == TokenType::LParen)
		return false;

	// the next token has to be a separator, else the parameters aren't
	// completely parsed
	return parser.nextToken().type == TokenType::Separator;
}

CAssemblerCommand* MipsParser::parseMacro(Parser& parser)
{
	TokenizerPosition startPos = parser.getTokenizer()->getPosition();

	// Cannot be a reference (we eat below.)
	const Token token = parser.peekToken();
	if (token.type != TokenType::Identifier)
		return nullptr;
	
	parser.eatToken();
	const std::wstring stringValue = token.getStringValue();
	for (int z = 0; mipsMacros[z].name != NULL; z++)
	{
		if (stringValue == mipsMacros[z].name)
		{
			TokenizerPosition tokenPos = parser.getTokenizer()->getPosition();

			if (parseMacroParameters(parser,mipsMacros[z]) == true)
			{
				return mipsMacros[z].function(parser,registers,immediate,mipsMacros[z].flags);
			}

			parser.getTokenizer()->setPosition(tokenPos);
		}
	}

	// no matching macro found, restore state
	parser.getTokenizer()->setPosition(startPos);
	return nullptr;
}

void MipsOpcodeFormatter::handleOpcodeName(const MipsOpcodeData& opData)
{
	const char* encoding = opData.opcode.name;

	while (*encoding != 0)
	{
		switch (*encoding++)
		{
		case 'S':
			buffer += "sptq"[opData.vfpuSize];
			break;
		case 'B':
			// TODO
			break;
		default:
			buffer += *(encoding-1);
			break;
		}
	}
}

void MipsOpcodeFormatter::handleImmediate(MipsImmediateType type, unsigned int originalValue, unsigned int opcodeFlags)
{
	switch (type)
	{
	case MipsImmediateType::ImmediateHalfFloat:
		buffer += formatString(L"%f",*((float*)&originalValue));
		break;
	case MipsImmediateType::Immediate16:
		if (!(opcodeFlags & MO_IPCR) && originalValue & 0x8000)
			buffer += formatString(L"-0x%X", 0x10000-(originalValue & 0xFFFF));
		else
			buffer += formatString(L"0x%X", originalValue);
		break;
	default:
		buffer += formatString(L"0x%X", originalValue);
		break;
	}
}

void MipsOpcodeFormatter::handleOpcodeParameters(const MipsOpcodeData& opData, const MipsRegisterData& regData,
	const MipsImmediateData& immData)
{
	const char* encoding = opData.opcode.encoding;

	MipsImmediateType type;
	while (*encoding != 0)
	{
		switch (*encoding++)
		{
		case 'r':	// forced register
			buffer += formatString(L"r%d",*encoding);
			encoding += 1;
			break;
		case 's':	// register
			buffer += regData.grs.name;
			break;
		case 'd':	// register
			buffer += regData.grd.name;
			break;
		case 't':	// register
			buffer += regData.grt.name;
			break;
		case 'S':	// fpu register
			buffer += regData.frs.name;
			break;
		case 'D':	// fpu register
			buffer += regData.frd.name;
			break;
		case 'T':	// fpu register
			buffer += regData.frt.name;
			break;
		case 'v':	// psp vfpu reg
		case 'm':	// vfpu matrix register
			switch (*encoding++)
			{
			case 'd':
				buffer += regData.vrd.name;
				break;
			case 's':
				buffer += regData.vrs.name;
				break;
			case 't':
				buffer += regData.vrt.name;
				break;
			}
			break;
		case 'V':	// ps2 vector reg
			switch (*encoding++)
			{
			case 'd':
				buffer += regData.ps2vrd.name;
				break;
			case 's':
				buffer += regData.ps2vrs.name;
				break;
			case 't':
				buffer += regData.ps2vrt.name;
				break;
			}
			break;
		case 'i':	// primary immediate
			decodeImmediateSize(encoding,type);
			handleImmediate(immData.primary.type,immData.primary.originalValue,opData.opcode.flags);
			break;
		case 'j':	// secondary immediate
			handleImmediate(immData.secondary.type,immData.secondary.originalValue, opData.opcode.flags);
			encoding++;
			break;
		case 'C':	// vfpu condition
		case 'W':	// vfpu argument
			// TODO
			break;
		case 'w':	// 'wb' characters
			buffer += L"wb";
			break;
		default:
			buffer += *(encoding-1);
			break;
		}
	}
}

const std::wstring& MipsOpcodeFormatter::formatOpcode(const MipsOpcodeData& opData, const MipsRegisterData& regData,
	const MipsImmediateData& immData)
{
	buffer = L"   ";
	handleOpcodeName(opData);

	while (buffer.size() < 11)
		buffer += ' ';

	handleOpcodeParameters(opData,regData,immData);
	return buffer;
}
