#include "stdafx.h"
#include "DirectivesParser.h"
#include "Core/Common.h"
#include "Commands/CDirectiveFile.h"
#include "Commands/CDirectiveData.h"
#include "Commands/CDirectiveConditional.h"
#include "Commands/CDirectiveMessage.h"
#include "Commands/CDirectiveArea.h"
#include "Commands/CAssemblerLabel.h"
#include "Commands/CommandSequence.h"
#include "Archs/MIPS/Mips.h"
#include "Archs/ARM/Arm.h"
#include "Core/Expression.h"
#include "Util/Util.h"

#include "Tokenizer.h"
#include "ExpressionParser.h"
#include <initializer_list>
#include <algorithm>
#include "Parser.h"

CAssemblerCommand* parseDirectiveOpen(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,2,3) == false)
		return nullptr;

	int64_t memoryAddress;
	std::wstring inputName, outputName;

	if (list[0].evaluateString(inputName,false) == false)
		return nullptr;

	if (list.back().evaluateInteger(memoryAddress) == false)
		return nullptr;

	if (list.size() == 3)
	{
		if (list[1].evaluateString(outputName,false) == false)
			return nullptr;
		
		CDirectiveFile* file = new CDirectiveFile();
		file->initCopy(inputName,outputName,memoryAddress);
		return file;
	} else {
		CDirectiveFile* file = new CDirectiveFile();
		file->initOpen(inputName,memoryAddress);
		return file;
	}
}

CAssemblerCommand* parseDirectiveCreate(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,2,2) == false)
		return nullptr;

	int64_t memoryAddress;
	std::wstring inputName, outputName;

	if (list[0].evaluateString(inputName,false) == false)
		return nullptr;

	if (list.back().evaluateInteger(memoryAddress) == false)
		return nullptr;

	CDirectiveFile* file = new CDirectiveFile();
	file->initCreate(inputName,memoryAddress);
	return file;
}

CAssemblerCommand* parseDirectiveClose(Parser& parser, int flags)
{
	CDirectiveFile* file = new CDirectiveFile();
	file->initClose();
	return file;
}

CAssemblerCommand* parseDirectiveIncbin(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,3) == false)
		return nullptr;
	
	std::wstring fileName;
	if (list[0].evaluateString(fileName,false) == false)
		return nullptr;

	CDirectiveIncbin* incbin = new CDirectiveIncbin(fileName);
	if (list.size() >= 2)
		incbin->setStart(list[1]);

	if (list.size() == 3)
		incbin->setSize(list[2]);

	return incbin;
}

CAssemblerCommand* parseDirectivePosition(Parser& parser, int flags)
{
	Expression exp = parser.parseExpression();
	if (exp.isLoaded() == false)
		return nullptr;

	CDirectivePosition::Type type;
	switch (flags & DIRECTIVE_USERMASK)
	{
	case DIRECTIVE_POS_PHYSICAL:
		type = CDirectivePosition::Physical;
		break;
	case DIRECTIVE_POS_VIRTUAL:
		type = CDirectivePosition::Virtual;
		break;
	default:
		return nullptr;
	}

	return new CDirectivePosition(exp,type);
}

CAssemblerCommand* parseDirectiveAlignFill(Parser& parser, int flags)
{
	CDirectiveAlignFill::Mode mode;
	switch (flags & DIRECTIVE_USERMASK)
	{
	case DIRECTIVE_FILE_ALIGN:
		mode = CDirectiveAlignFill::Align;
		break;
	case DIRECTIVE_FILE_FILL:
		mode = CDirectiveAlignFill::Fill;
		break;
	default:
		return nullptr;
	}

	if (mode == CDirectiveAlignFill::Align && parser.peekToken().type == TokenType::Separator)
		return new CDirectiveAlignFill(UINT64_C(4),mode);

	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,2) == false)
		return nullptr;

	if (list.size() == 2)
		return new CDirectiveAlignFill(list[0],list[1],mode);
	else
		return new CDirectiveAlignFill(list[0],mode);
}

CAssemblerCommand* parseDirectiveSkip(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,1) == false)
		return nullptr;

	return new CDirectiveSkip(list[0]);
}

CAssemblerCommand* parseDirectiveHeaderSize(Parser& parser, int flags)
{
	Expression exp = parser.parseExpression();
	if (exp.isLoaded() == false)
		return nullptr;

	return new CDirectiveHeaderSize(exp);
}

CAssemblerCommand* parseDirectiveObjImport(Parser& parser, int flags)
{
	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,2) == false)
		return nullptr;

	std::wstring fileName;
	if (list[0].evaluateString(fileName,true) == false)
		return nullptr;

	if (list.size() == 2)
	{
		std::wstring ctorName;
		if (list[1].evaluateIdentifier(ctorName) == false)
			return nullptr;

		return new DirectiveObjImport(fileName,ctorName);
	}
	
	return new DirectiveObjImport(fileName);
}

CAssemblerCommand* parseDirectiveConditional(Parser& parser, int flags)
{
	ConditionType type;
	std::wstring name;
	Expression exp;

	const Token& start = parser.peekToken();
	ConditionalResult condResult = ConditionalResult::Unknown;
	switch (flags)
	{
	case DIRECTIVE_COND_IF:
		type = ConditionType::IF;
		exp = parser.parseExpression();
		if (exp.isLoaded() == false)
		{
			parser.printError(start,L"Invalid condition");
			return new DummyCommand();
		}

		if (exp.isConstExpression())
		{
			ExpressionValue result = exp.evaluate();
			if (result.isInt())
				condResult = result.intValue != 0 ? ConditionalResult::True : ConditionalResult::False;
		}
		break;
	case DIRECTIVE_COND_IFDEF:
		type = ConditionType::IFDEF;
		if (parser.parseIdentifier(name) == false)
			return nullptr;		
		break;
	case DIRECTIVE_COND_IFNDEF:
		type = ConditionType::IFNDEF;
		if (parser.parseIdentifier(name) == false)
			return nullptr;
		break;
	}

	if(parser.nextToken().type != TokenType::Separator)
	{
		parser.printError(start,L"Directive not terminated");
		return nullptr;
	}

	parser.pushConditionalResult(condResult);
	CAssemblerCommand* ifBlock = parser.parseCommandSequence(L'.', {L".else", L".elseif", L".elseifdef", L".elseifndef", L".endif"});
	parser.popConditionalResult();

	// update the file info so that else commands get the right line number
	parser.updateFileInfo();

	CAssemblerCommand* elseBlock = nullptr;
	const Token &next = parser.nextToken();
	const std::wstring stringValue = next.getStringValue();

	ConditionalResult elseResult = condResult;
	switch (condResult)
	{
	case ConditionalResult::True:
		elseResult = ConditionalResult::False;
		break;
	case ConditionalResult::False:
		elseResult = ConditionalResult::True;
		break;
	}

	parser.pushConditionalResult(elseResult);
	if (stringValue == L".else")
	{
		elseBlock = parser.parseCommandSequence(L'.', {L".endif"});

		parser.eatToken();	// eat .endif
	} else if (stringValue == L".elseif")
	{
		elseBlock = parseDirectiveConditional(parser,DIRECTIVE_COND_IF);
	} else if (stringValue == L".elseifdef")
	{
		elseBlock = parseDirectiveConditional(parser,DIRECTIVE_COND_IFDEF);
	} else if (stringValue == L".elseifndef")
	{
		elseBlock = parseDirectiveConditional(parser,DIRECTIVE_COND_IFNDEF);
	} else if (stringValue != L".endif")
	{
		parser.popConditionalResult();
		return nullptr;
	}

	parser.popConditionalResult();

	// for true or false blocks, there's no need to create a conditional command
	if (condResult == ConditionalResult::True)
	{
		delete elseBlock;
		return ifBlock;
	} 
	
	if (condResult == ConditionalResult::False)
	{
		delete ifBlock;
		if (elseBlock != nullptr)
			return elseBlock;
		else
			return new DummyCommand();
	}

	CDirectiveConditional* cond;
	if (exp.isLoaded())
		cond = new CDirectiveConditional(type,exp);
	else if (name.size() != 0)
		cond = new CDirectiveConditional(type,name);
	else
		cond = new CDirectiveConditional(type);

	cond->setContent(ifBlock,elseBlock);
	return cond;
}

CAssemblerCommand* parseDirectiveTable(Parser& parser, int flags)
{
	const Token& start = parser.peekToken();

	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,2) == false)
		return nullptr;

	std::wstring fileName;
	if (list[0].evaluateString(fileName,true) == false)
	{
		parser.printError(start,L"Invalid file name");
		return nullptr;
	}

	TextFile::Encoding encoding = TextFile::GUESS;
	if (list.size() == 2)
	{
		std::wstring encodingName;
		if (list[1].evaluateString(encodingName,true) == false)
		{
			parser.printError(start,L"Invalid encoding name");
			return nullptr;
		}

		encoding = getEncodingFromString(encodingName);
	}

	return new TableCommand(fileName,encoding);
}

CAssemblerCommand* parseDirectiveData(Parser& parser, int flags)
{
	bool terminate = false;
	if (flags & DIRECTIVE_DATA_TERMINATION)
	{
		terminate = true;
		flags &= ~DIRECTIVE_DATA_TERMINATION;
	}

	std::vector<Expression> list;
	if (parser.parseExpressionList(list,1,-1) == false)
		return nullptr;
	
	CDirectiveData* data = new CDirectiveData();
	switch (flags & DIRECTIVE_USERMASK)
	{
	case DIRECTIVE_DATA_8:
		data->setNormal(list,1);
		break;
	case DIRECTIVE_DATA_16:
		data->setNormal(list,2);
		break;
	case DIRECTIVE_DATA_32:
		data->setNormal(list,4);
		break;
	case DIRECTIVE_DATA_64:
		data->setNormal(list,8);
		break;
	case DIRECTIVE_DATA_ASCII:
		data->setAscii(list,terminate);
		break;
	case DIRECTIVE_DATA_SJIS:
		data->setSjis(list,terminate);
		break;
	case DIRECTIVE_DATA_CUSTOM:
		data->setCustom(list,terminate);
		break;
	case DIRECTIVE_DATA_FLOAT:
		data->setFloat(list);
		break;
	case DIRECTIVE_DATA_DOUBLE:
		data->setDouble(list);
		break;
	}
	
	return data;
}

CAssemblerCommand* parseDirectiveMipsArch(Parser& parser, int flags)
{
	Arch = &Mips;
	Mips.SetLoadDelay(false, 0);

	switch (flags)
	{
	case DIRECTIVE_MIPS_PSX:
		Mips.SetVersion(MARCH_PSX);
		return new ArchitectureCommand(L".psx", L"");
	case DIRECTIVE_MIPS_PS2:
		Mips.SetVersion(MARCH_PS2);
		return new ArchitectureCommand(L".ps2", L"");
	case DIRECTIVE_MIPS_PSP:
		Mips.SetVersion(MARCH_PSP);
		return new ArchitectureCommand(L".psp", L"");
	case DIRECTIVE_MIPS_N64:
		Mips.SetVersion(MARCH_N64);
		return new ArchitectureCommand(L".n64", L"");
	case DIRECTIVE_MIPS_RSP:
		Mips.SetVersion(MARCH_RSP);
		return new ArchitectureCommand(L".rsp", L"");
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveArmArch(Parser& parser, int flags)
{
	Arch = &Arm;

	switch (flags)
	{
	case DIRECTIVE_ARM_GBA:
		Arm.SetThumbMode(true);
		Arm.setVersion(AARCH_GBA);
		return new ArchitectureCommand(L".gba\n.thumb", L".thumb");
	case DIRECTIVE_ARM_NDS:
		Arm.SetThumbMode(false);
		Arm.setVersion(AARCH_NDS);
		return new ArchitectureCommand(L".nds\n.arm", L".arm");
	case DIRECTIVE_ARM_3DS:
		Arm.SetThumbMode(false);
		Arm.setVersion(AARCH_3DS);
		return new ArchitectureCommand(L".3ds\n.arm", L".arm");
	case DIRECTIVE_ARM_BIG:
		Arm.SetThumbMode(false);
		Arm.setVersion(AARCH_BIG);
		return new ArchitectureCommand(L".arm.big\n.arm", L".arm");
	case DIRECTIVE_ARM_LITTLE:
		Arm.SetThumbMode(false);
		Arm.setVersion(AARCH_LITTLE);
		return new ArchitectureCommand(L".arm.little\n.arm", L".arm");
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveArea(Parser& parser, int flags)
{
	std::vector<Expression> parameters;
	if (parser.parseExpressionList(parameters,1,2) == false)
		return nullptr;
	
	CDirectiveArea* area = new CDirectiveArea(parameters[0]);
	if (parameters.size() == 2)
		area->setFillExpression(parameters[1]);

	CAssemblerCommand* content = parser.parseCommandSequence(L'.', {L".endarea"});
	parser.eatToken();

	area->setContent(content);
	return area;
}

CAssemblerCommand* parseDirectiveErrorWarning(Parser& parser, int flags)
{
	const Token &tok = parser.nextToken();

	if (tok.type != TokenType::Identifier && tok.type != TokenType::String)
		return nullptr;

	std::wstring stringValue = tok.getStringValue();
	std::transform(stringValue.begin(),stringValue.end(),stringValue.begin(),::towlower);

	if (stringValue == L"on")
	{	
		Logger::setErrorOnWarning(true);
		return new DummyCommand();
	} else if (stringValue == L"off")
	{
		Logger::setErrorOnWarning(false);
		return new DummyCommand();
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveRelativeInclude(Parser& parser, int flags)
{
	const Token &tok = parser.nextToken();

	if (tok.type != TokenType::Identifier && tok.type != TokenType::String)
		return nullptr;

	std::wstring stringValue = tok.getStringValue();
	std::transform(stringValue.begin(),stringValue.end(),stringValue.begin(),::towlower);

	if (stringValue == L"on")
	{	
		Global.relativeInclude = true;
		return new DummyCommand();
	} else if (stringValue == L"off")
	{
		Global.relativeInclude = false;
		return new DummyCommand();
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveNocash(Parser& parser, int flags)
{
	const Token &tok = parser.nextToken();

	if (tok.type != TokenType::Identifier && tok.type != TokenType::String)
		return nullptr;

	std::wstring stringValue = tok.getStringValue();
	std::transform(stringValue.begin(),stringValue.end(),stringValue.begin(),::towlower);

	if (stringValue == L"on")
	{	
		Global.nocash = true;
		return new DummyCommand();
	} else if (stringValue == L"off")
	{
		Global.nocash = false;
		return new DummyCommand();
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveSym(Parser& parser, int flags)
{
	const Token &tok = parser.nextToken();

	if (tok.type != TokenType::Identifier && tok.type != TokenType::String)
		return nullptr;

	std::wstring stringValue = tok.getStringValue();
	std::transform(stringValue.begin(),stringValue.end(),stringValue.begin(),::towlower);

	if (stringValue == L"on")
		return new CDirectiveSym(true);
	else if (stringValue == L"off")
		return new CDirectiveSym(false);
	else
		return nullptr;
}

CAssemblerCommand* parseDirectiveDefineLabel(Parser& parser, int flags)
{
	const Token& tok = parser.nextToken();
	if (tok.type != TokenType::Identifier)
		return nullptr;

	if (parser.nextToken().type != TokenType::Comma)
		return nullptr;

	Expression value = parser.parseExpression();
	if (value.isLoaded() == false)
		return nullptr;

	const std::wstring stringValue = tok.getStringValue();
	if (Global.symbolTable.isValidSymbolName(stringValue) == false)
	{
		parser.printError(tok,L"Invalid label name \"%s\"",stringValue);
		return nullptr;
	}

	return new CAssemblerLabel(stringValue,tok.getOriginalText(),value);
}

CAssemblerCommand* parseDirectiveFunction(Parser& parser, int flags)
{
	const Token& tok = parser.nextToken();
	if (tok.type != TokenType::Identifier)
		return nullptr;

	if (parser.nextToken().type != TokenType::Separator)
	{
		parser.printError(tok,L"Directive not terminated");
		return nullptr;
	}

	CDirectiveFunction* func = new CDirectiveFunction(tok.getStringValue(),tok.getOriginalText());
	CAssemblerCommand* seq = parser.parseCommandSequence(L'.', {L".endfunc",L".endfunction",L".func",L".function"});

	const std::wstring stringValue = parser.peekToken().getStringValue();
	if (stringValue == L".endfunc" ||
		stringValue == L".endfunction")
	{
		parser.eatToken();
		if(parser.nextToken().type != TokenType::Separator)
		{
			parser.printError(tok,L"Directive not terminated");
			return nullptr;
		}
	}

	func->setContent(seq);
	return func;
}

CAssemblerCommand* parseDirectiveMessage(Parser& parser, int flags)
{
	Expression exp = parser.parseExpression();

	switch (flags)
	{
	case DIRECTIVE_MSG_WARNING:
		return new CDirectiveMessage(CDirectiveMessage::Type::Warning,exp);
	case DIRECTIVE_MSG_ERROR:
		return new CDirectiveMessage(CDirectiveMessage::Type::Error,exp);
	case DIRECTIVE_MSG_NOTICE:
		return new CDirectiveMessage(CDirectiveMessage::Type::Notice,exp);
	}

	return nullptr;
}

CAssemblerCommand* parseDirectiveInclude(Parser& parser, int flags)
{
	const Token& start = parser.peekToken();

	std::vector<Expression> parameters;
	if (parser.parseExpressionList(parameters,1,2) == false)
		return nullptr;

	std::wstring fileName;
	if (parameters[0].evaluateString(fileName,true) == false)
		return nullptr;

	fileName = getFullPathName(fileName);

	TextFile::Encoding encoding = TextFile::GUESS;
	if (parameters.size() == 2)
	{
		std::wstring encodingName;
		if (parameters[1].evaluateString(encodingName,true) == false
			&& parameters[1].evaluateIdentifier(encodingName) == false)
			return nullptr;
		
		encoding = getEncodingFromString(encodingName);
	}

	// don't include the file if it's inside a false block
	if (parser.isInsideTrueBlock() == false)
		return new DummyCommand();

	if (fileExists(fileName) == false)
	{
		parser.printError(start,L"Included file \"%s\" does not exist",fileName);
		return nullptr;
	}

	TextFile f;
	if (f.open(fileName,TextFile::Read,encoding) == false)
	{
		parser.printError(start,L"Could not open included file \"%s\"",fileName);
		return nullptr;
	}

	return parser.parseFile(f);
}

const DirectiveMap directives = {
	{ L".open",				{ &parseDirectiveOpen,				DIRECTIVE_NOTINMEMORY } },
	{ L".openfile",			{ &parseDirectiveOpen,				DIRECTIVE_NOTINMEMORY } },
	{ L".create",			{ &parseDirectiveCreate,			DIRECTIVE_NOTINMEMORY } },
	{ L".createfile",		{ &parseDirectiveCreate,			DIRECTIVE_NOTINMEMORY } },
	{ L".close",			{ &parseDirectiveClose,				DIRECTIVE_NOTINMEMORY } },
	{ L".closefile",		{ &parseDirectiveClose,				DIRECTIVE_NOTINMEMORY } },
	{ L".incbin",			{ &parseDirectiveIncbin,			0 } },
	{ L".import",			{ &parseDirectiveIncbin,			0 } },
	{ L".org",				{ &parseDirectivePosition,			DIRECTIVE_POS_VIRTUAL } },
	{ L"org",				{ &parseDirectivePosition,			DIRECTIVE_POS_VIRTUAL } },
	{ L".orga",				{ &parseDirectivePosition,			DIRECTIVE_POS_PHYSICAL } },
	{ L"orga",				{ &parseDirectivePosition,			DIRECTIVE_POS_PHYSICAL } },
	{ L".headersize",		{ &parseDirectiveHeaderSize,		0 } },
	{ L".align",			{ &parseDirectiveAlignFill,			DIRECTIVE_FILE_ALIGN } },
	{ L".fill",				{ &parseDirectiveAlignFill,			DIRECTIVE_FILE_FILL } },
	{ L"defs",				{ &parseDirectiveAlignFill,			DIRECTIVE_FILE_FILL } },
	{ L".skip",				{ &parseDirectiveSkip,				0 } },

	{ L".if",				{ &parseDirectiveConditional,		DIRECTIVE_COND_IF } },
	{ L".ifdef",			{ &parseDirectiveConditional,		DIRECTIVE_COND_IFDEF } },
	{ L".ifndef",			{ &parseDirectiveConditional,		DIRECTIVE_COND_IFNDEF } },

	{ L".loadtable",		{ &parseDirectiveTable,				0 } },
	{ L".table",			{ &parseDirectiveTable,				0 } },
	{ L".byte",				{ &parseDirectiveData,				DIRECTIVE_DATA_8 } },
	{ L".halfword",			{ &parseDirectiveData,				DIRECTIVE_DATA_16 } },
	{ L".word",				{ &parseDirectiveData,				DIRECTIVE_DATA_32 } },
	{ L".doubleword",		{ &parseDirectiveData,				DIRECTIVE_DATA_64 } },
	{ L".db",				{ &parseDirectiveData,				DIRECTIVE_DATA_8 } },
	{ L".dh",				{ &parseDirectiveData,				DIRECTIVE_DATA_16|DIRECTIVE_NOCASHOFF } },
	{ L".dw",				{ &parseDirectiveData,				DIRECTIVE_DATA_32|DIRECTIVE_NOCASHOFF } },
	{ L".dd",				{ &parseDirectiveData,				DIRECTIVE_DATA_64|DIRECTIVE_NOCASHOFF } },
	{ L".dw",				{ &parseDirectiveData,				DIRECTIVE_DATA_16|DIRECTIVE_NOCASHON } },
	{ L".dd",				{ &parseDirectiveData,				DIRECTIVE_DATA_32|DIRECTIVE_NOCASHON } },
	{ L".dcb",				{ &parseDirectiveData,				DIRECTIVE_DATA_8 } },
	{ L".dcw",				{ &parseDirectiveData,				DIRECTIVE_DATA_16 } },
	{ L".dcd",				{ &parseDirectiveData,				DIRECTIVE_DATA_32 } },
	{ L".dcq",				{ &parseDirectiveData,				DIRECTIVE_DATA_64 } },
	{ L"db",				{ &parseDirectiveData,				DIRECTIVE_DATA_8 } },
	{ L"dh",				{ &parseDirectiveData,				DIRECTIVE_DATA_16|DIRECTIVE_NOCASHOFF } },
	{ L"dw",				{ &parseDirectiveData,				DIRECTIVE_DATA_32|DIRECTIVE_NOCASHOFF } },
	{ L"dd",				{ &parseDirectiveData,				DIRECTIVE_DATA_64|DIRECTIVE_NOCASHOFF } },
	{ L"dw",				{ &parseDirectiveData,				DIRECTIVE_DATA_16|DIRECTIVE_NOCASHON } },
	{ L"dd",				{ &parseDirectiveData,				DIRECTIVE_DATA_32|DIRECTIVE_NOCASHON } },
	{ L"dcb",				{ &parseDirectiveData,				DIRECTIVE_DATA_8 } },
	{ L"dcw",				{ &parseDirectiveData,				DIRECTIVE_DATA_16 } },
	{ L"dcd",				{ &parseDirectiveData,				DIRECTIVE_DATA_32 } },
	{ L"dcq",				{ &parseDirectiveData,				DIRECTIVE_DATA_64 } },
	{ L".float",			{ &parseDirectiveData,				DIRECTIVE_DATA_FLOAT } },
	{ L".double",			{ &parseDirectiveData,				DIRECTIVE_DATA_DOUBLE } },
	{ L".ascii",			{ &parseDirectiveData,				DIRECTIVE_DATA_ASCII } },
	{ L".asciiz",			{ &parseDirectiveData,				DIRECTIVE_DATA_ASCII|DIRECTIVE_DATA_TERMINATION } },
	{ L".string",			{ &parseDirectiveData,				DIRECTIVE_DATA_CUSTOM|DIRECTIVE_DATA_TERMINATION } },
	{ L".str",				{ &parseDirectiveData,				DIRECTIVE_DATA_CUSTOM|DIRECTIVE_DATA_TERMINATION } },
	{ L".stringn",			{ &parseDirectiveData,				DIRECTIVE_DATA_CUSTOM } },
	{ L".strn",				{ &parseDirectiveData,				DIRECTIVE_DATA_CUSTOM } },
	{ L".sjis",				{ &parseDirectiveData,				DIRECTIVE_DATA_SJIS|DIRECTIVE_DATA_TERMINATION } },
	{ L".sjisn",			{ &parseDirectiveData,				DIRECTIVE_DATA_SJIS } },

	{ L".psx",				{ &parseDirectiveMipsArch,			DIRECTIVE_MIPS_PSX } },
	{ L".ps2",				{ &parseDirectiveMipsArch,			DIRECTIVE_MIPS_PS2 } },
	{ L".psp",				{ &parseDirectiveMipsArch,			DIRECTIVE_MIPS_PSP } },
	{ L".n64",				{ &parseDirectiveMipsArch,			DIRECTIVE_MIPS_N64 } },
	{ L".rsp",				{ &parseDirectiveMipsArch,			DIRECTIVE_MIPS_RSP } },

	{ L".gba",				{ &parseDirectiveArmArch,			DIRECTIVE_ARM_GBA } },
	{ L".nds",				{ &parseDirectiveArmArch,			DIRECTIVE_ARM_NDS } },
	{ L".3ds",				{ &parseDirectiveArmArch,			DIRECTIVE_ARM_3DS } },
	{ L".arm.big",			{ &parseDirectiveArmArch,			DIRECTIVE_ARM_BIG } },
	{ L".arm.little",		{ &parseDirectiveArmArch,			DIRECTIVE_ARM_LITTLE } },
	
	{ L".area",				{ &parseDirectiveArea,				0 } },

	{ L".importobj",		{ &parseDirectiveObjImport,			0 } },
	{ L".importlib",		{ &parseDirectiveObjImport,			0 } },

	{ L".erroronwarning",	{ &parseDirectiveErrorWarning,		0 } },
	{ L".relativeinclude",	{ &parseDirectiveRelativeInclude,	0 } },
	{ L".nocash",			{ &parseDirectiveNocash,			0 } },
	{ L".sym",				{ &parseDirectiveSym,				0 } },
	
	{ L".definelabel",		{ &parseDirectiveDefineLabel,		0 } },
	{ L".function",			{ &parseDirectiveFunction,			DIRECTIVE_MANUALSEPARATOR } },
	{ L".func",				{ &parseDirectiveFunction,			DIRECTIVE_MANUALSEPARATOR } },
	
	{ L".warning",			{ &parseDirectiveMessage,			DIRECTIVE_MSG_WARNING } },
	{ L".error",			{ &parseDirectiveMessage,			DIRECTIVE_MSG_ERROR } },
	{ L".notice",			{ &parseDirectiveMessage,			DIRECTIVE_MSG_NOTICE } },

	{ L".include",			{ &parseDirectiveInclude,			0 } },
};
