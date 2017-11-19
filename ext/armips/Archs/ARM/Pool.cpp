#include "stdafx.h"
#include "Pool.h"
#include "Arm.h"
#include "Core/Common.h"
#include "Core/FileManager.h"

ArmStateCommand::ArmStateCommand(bool state)
{
	armstate = state;
}

bool ArmStateCommand::Validate()
{
	RamPos = g_fileManager->getVirtualAddress();
	return false;
}

void ArmStateCommand::writeSymData(SymbolData& symData) const
{
	// TODO: find a less ugly way to check for undefined memory positions
	if (RamPos == -1)
		return;

	if (armstate == true)
	{
		symData.addLabel(RamPos,L".arm");
	} else {
		symData.addLabel(RamPos,L".thumb");
	}
}


ArmPoolCommand::ArmPoolCommand()
{

}

bool ArmPoolCommand::Validate()
{
	position = g_fileManager->getVirtualAddress();

	size_t oldSize = values.size();
	values.clear();

	for (ArmPoolEntry& entry: Arm.getPoolContent())
	{
		size_t index = values.size();
		
		// try to filter redundant values, but only if
		// we aren't in an unordinarily long validation loop
		if (Global.validationPasses < 10)
		{
			for (size_t i = 0; i < values.size(); i++)
			{
				if (values[i] == entry.value)
				{
					index = i;
					break;
				}
			}
		}

		if (index == values.size())
			values.push_back(entry.value);

		entry.command->applyFileInfo();
		entry.command->setPoolAddress(position+index*4);
	}

	Arm.clearPoolContent();
	g_fileManager->advanceMemory(values.size()*4);

	return oldSize != values.size();
}

void ArmPoolCommand::Encode() const
{
	for (size_t i = 0; i < values.size(); i++)
	{
		int32_t value = values[i];
		g_fileManager->writeU32(value);
	}
}

void ArmPoolCommand::writeTempData(TempData& tempData) const
{
	for (size_t i = 0; i < values.size(); i++)
	{
		int32_t value = values[i];
		tempData.writeLine(position+i*4,formatString(L".word 0x%08X",value));
	}
}

void ArmPoolCommand::writeSymData(SymbolData& symData) const
{
	if (values.size() != 0)
	{
		symData.addLabel(position,L".pool");
		symData.addData(position,values.size()*4,SymbolData::Data32);
	}
}
