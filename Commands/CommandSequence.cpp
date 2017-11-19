#include "stdafx.h"
#include "CommandSequence.h"

CommandSequence::CommandSequence()
	: CAssemblerCommand()
{

}

CommandSequence::~CommandSequence()
{
	for (CAssemblerCommand* cmd: commands)
	{
		delete cmd;
	}
}

bool CommandSequence::Validate()
{
	bool result = false;
	
	for (CAssemblerCommand* cmd: commands)
	{
		cmd->applyFileInfo();
		if (cmd->Validate())
			result = true;
	}

	return result;
}

void CommandSequence::Encode() const
{
	for (CAssemblerCommand* cmd: commands)
	{
		cmd->Encode();
	}
}

void CommandSequence::writeTempData(TempData& tempData) const
{
	for (CAssemblerCommand* cmd: commands)
	{
		cmd->applyFileInfo();
		cmd->writeTempData(tempData);
	}
}

void CommandSequence::writeSymData(SymbolData& symData) const
{
	for (CAssemblerCommand* cmd: commands)
	{
		cmd->writeSymData(symData);
	}
}
