#pragma once
#include "Commands/CAssemblerCommand.h"

class Label;

class CommandSequence: public CAssemblerCommand
{
public:
	CommandSequence();
	virtual ~CommandSequence();
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
	void addCommand(CAssemblerCommand* cmd) { commands.push_back(cmd); }
private:
	std::vector<CAssemblerCommand*> commands;
};