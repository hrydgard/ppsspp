#pragma once
#include <unordered_map>
#include "Parser/DirectivesParser.h"
#include "Parser/Tokenizer.h"
#include "CThumbInstruction.h"
#include "CArmInstruction.h"

struct ArmRegisterDescriptor {
	const wchar_t* name;
	int num;
};

class ArmParser
{
public:
	CAssemblerCommand* parseDirective(Parser& parser);
	CArmInstruction* parseArmOpcode(Parser& parser);
	CThumbInstruction* parseThumbOpcode(Parser& parser);
private:
	bool parseRegisterTable(Parser& parser, ArmRegisterValue& dest, const ArmRegisterDescriptor* table, size_t count);
	bool parseRegister(Parser& parser, ArmRegisterValue& dest, int max = 15);
	bool parseCopRegister(Parser& parser, ArmRegisterValue& dest);
	bool parseCopNumber(Parser& parser, ArmRegisterValue& dest);
	bool parseRegisterList(Parser& parser, int& dest, int validMask);
	bool parseImmediate(Parser& parser, Expression& dest);
	bool parseShift(Parser& parser, ArmOpcodeVariables& vars, bool immediateOnly);
	bool parsePseudoShift(Parser& parser, ArmOpcodeVariables& vars, int type);
	void parseWriteback(Parser& parser, bool& dest);
	void parsePsr(Parser& parser, bool& dest);
	void parseSign(Parser& parser, bool& dest);
	bool parsePsrTransfer(Parser& parser, ArmOpcodeVariables& vars, bool shortVersion);
	
	bool matchSymbol(Parser& parser, wchar_t symbol, bool optional);
	
	int decodeCondition(const std::wstring& text, size_t& pos);
	bool decodeAddressingMode(const std::wstring& text, size_t& pos, unsigned char& dest);
	bool decodeXY(const std::wstring& text, size_t& pos, bool& dest);
	void decodeS(const std::wstring& text, size_t& pos, bool& dest);
	bool decodeArmOpcode(const std::wstring& name, const tArmOpcode& opcode, ArmOpcodeVariables& vars);
	
	bool parseArmParameters(Parser& parser, const tArmOpcode& opcode, ArmOpcodeVariables& vars);
	bool parseThumbParameters(Parser& parser, const tThumbOpcode& opcode, ThumbOpcodeVariables& vars);
};