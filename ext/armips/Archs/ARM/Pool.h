#pragma once
#include "Commands/CAssemblerCommand.h"
#include "../Architecture.h"

class ArmStateCommand: public CAssemblerCommand
{
public:
	ArmStateCommand(bool state);
	virtual bool Validate();
	virtual void Encode() const { };
	virtual void writeTempData(TempData& tempData) const { };
	virtual void writeSymData(SymbolData& symData) const;
private:
	int64_t RamPos;
	bool armstate;
};

class ArmOpcodeCommand;

struct ArmPoolEntry
{
	ArmOpcodeCommand* command;
	int32_t value;
};

class ArmPoolCommand: public CAssemblerCommand
{
public:
	ArmPoolCommand();
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	int64_t position;
	std::vector<int32_t> values;
};
