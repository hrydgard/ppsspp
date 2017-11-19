#pragma once
#include "Commands/CAssemblerCommand.h"
#include "ThumbOpcodes.h"
#include "Arm.h"
#include "Core/Expression.h"
#include "ThumbOpcodes.h"

struct ThumbOpcodeVariables {
	ArmRegisterValue rd;
	ArmRegisterValue rs;
	ArmRegisterValue rn;
	ArmRegisterValue ro;
	Expression ImmediateExpression;
	int Immediate;
	int ImmediateBitLen;
	int OriginalImmediate;
	int rlist;
	char RlistStr[32];
} ;

class CThumbInstruction: public ArmOpcodeCommand
{
public:
	CThumbInstruction(const tThumbOpcode& sourceOpcode, ThumbOpcodeVariables& vars);
//	~CThumbInstruction();
	bool Load(char* Name, char* Params);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	size_t GetSize() { return OpcodeSize; };
	virtual void setPoolAddress(int64_t address);
private:
	void FormatInstruction(const char* encoding, char* dest) const;
	void WriteInstruction(unsigned short encoding) const;
	ThumbOpcodeVariables Vars;
	tThumbOpcode Opcode;
	size_t OpcodeSize;
	int64_t RamPos;
};
