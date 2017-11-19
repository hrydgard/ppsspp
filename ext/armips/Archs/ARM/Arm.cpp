#include "stdafx.h"
#include "Arm.h"
#include "Core/Common.h"
#include "ArmRelocator.h"
#include "ArmParser.h"

CArmArchitecture Arm;

CArmArchitecture::CArmArchitecture()
{
	clear();
}

CArmArchitecture::~CArmArchitecture()
{
	clear();
}

CAssemblerCommand* CArmArchitecture::parseDirective(Parser& parser)
{
	ArmParser armParser;

	return armParser.parseDirective(parser);
}

CAssemblerCommand* CArmArchitecture::parseOpcode(Parser& parser)
{
	ArmParser armParser;

	if (thumb)
		return armParser.parseThumbOpcode(parser);
	else
		return armParser.parseArmOpcode(parser);
}

void CArmArchitecture::clear()
{
	currentPoolContent.clear();
	thumb = false;
}

void CArmArchitecture::Pass2()
{
	currentPoolContent.clear();
}

void CArmArchitecture::Revalidate()
{
	for (ArmPoolEntry& entry: currentPoolContent)
	{
		entry.command->applyFileInfo();
		Logger::queueError(Logger::Error,L"Unable to find literal pool");
	}

	currentPoolContent.clear();
}

void CArmArchitecture::NextSection()
{

}

IElfRelocator* CArmArchitecture::getElfRelocator()
{
	return new ArmElfRelocator(version != AARCH_GBA);
}

void CArmArchitecture::addPoolValue(ArmOpcodeCommand* command, int32_t value)
{
	ArmPoolEntry entry;
	entry.command = command;
	entry.value = value;

	currentPoolContent.push_back(entry);
}
