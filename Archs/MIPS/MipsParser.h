#pragma once
#include <unordered_map>
#include "Parser/DirectivesParser.h"
#include "Parser/Tokenizer.h"
#include "CMipsInstruction.h"
#include "MipsMacros.h"

struct MipsRegisterDescriptor {
	const wchar_t* name;
	int num;
};

class MipsParser
{
public:
	CAssemblerCommand* parseDirective(Parser& parser);
	CMipsInstruction* parseOpcode(Parser& parser);
	CAssemblerCommand* parseMacro(Parser& parser);
private:
	bool parseRegisterNumber(Parser& parser, MipsRegisterValue& dest, int numValues);
	bool parseRegisterTable(Parser& parser, MipsRegisterValue& dest, const MipsRegisterDescriptor* table, size_t count);
	bool parseRegister(Parser& parser, MipsRegisterValue& dest);
	bool parseFpuRegister(Parser& parser, MipsRegisterValue& dest);
	bool parseFpuControlRegister(Parser& parser, MipsRegisterValue& dest);
	bool parseCop0Register(Parser& parser, MipsRegisterValue& dest);
	bool parsePs2Cop2Register(Parser& parser, MipsRegisterValue& dest);
	bool parseRspCop0Register(Parser& parser, MipsRegisterValue& dest);
	bool parseRspVectorRegister(Parser& parser, MipsRegisterValue& dest);
	bool parseRspBroadcastElement(Parser& parser, MipsRegisterValue& dest);
	bool parseRspScalarElement(Parser& parser, MipsRegisterValue& dest);
	bool parseRspOffsetElement(Parser& parser, MipsRegisterValue& dest);
	bool parseVfpuRegister(Parser& parser, MipsRegisterValue& reg, int size);
	bool parseVfpuControlRegister(Parser& parser, MipsRegisterValue& reg);
	bool parseImmediate(Parser& parser, Expression& dest);
	bool parseVcstParameter(Parser& parser, int& result);
	bool parseVfpuVrot(Parser& parser, int& result, int size);
	bool parseVfpuCondition(Parser& parser, int& result);
	bool parseVpfxsParameter(Parser& parser, int& result);
	bool parseVpfxdParameter(Parser& parser, int& result);
	bool parseCop2BranchCondition(Parser& parser, int& result);
	bool parseWb(Parser& parser);

	bool decodeCop2BranchCondition(const std::wstring& text, size_t& pos, int& result);
	bool decodeVfpuType(const std::wstring& name, size_t& pos, int& dest);
	bool decodeOpcode(const std::wstring& name, const tMipsOpcode& opcode);

	void setOmittedRegisters(const tMipsOpcode& opcode);
	bool matchSymbol(Parser& parser, wchar_t symbol);
	bool parseParameters(Parser& parser, const tMipsOpcode& opcode);
	bool parseMacroParameters(Parser& parser, const MipsMacroDefinition& macro);

	MipsRegisterData registers;
	MipsImmediateData immediate;
	MipsOpcodeData opcodeData;
	bool hasFixedSecondaryImmediate;
};

class MipsOpcodeFormatter
{
public:
	const std::wstring& formatOpcode(const MipsOpcodeData& opData, const MipsRegisterData& regData,
		const MipsImmediateData& immData);
private:
	void handleOpcodeName(const MipsOpcodeData& opData);	
	void handleOpcodeParameters(const MipsOpcodeData& opData, const MipsRegisterData& regData,
		const MipsImmediateData& immData);
	void handleImmediate(MipsImmediateType type, unsigned int originalValue, unsigned int opcodeFlags);

	std::wstring buffer;
};
