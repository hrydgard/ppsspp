#include "stdafx.h"
#include "Commands/CDirectiveArea.h"
#include "Core/Common.h"
#include "Core/FileManager.h"
#include <algorithm>

CDirectiveArea::CDirectiveArea(Expression& size)
{
	this->areaSize = 0;
	this->contentSize = 0;
	this->fillValue = 0;

	this->sizeExpression = size;
	this->content = nullptr;
}

CDirectiveArea::~CDirectiveArea()
{
	delete content;
}

void CDirectiveArea::setFillExpression(Expression& exp)
{
	fillExpression = exp;
}

bool CDirectiveArea::Validate()
{
	size_t oldAreaSize = areaSize;
	size_t oldContentSize = contentSize;

	position = g_fileManager->getVirtualAddress();

	if (sizeExpression.evaluateInteger(areaSize) == false)
	{
		Logger::queueError(Logger::Error,L"Invalid size expression");
		return false;
	}

	if (fillExpression.isLoaded())
	{
		if (fillExpression.evaluateInteger(fillValue) == false)
		{
			Logger::queueError(Logger::Error,L"Invalid fill expression");
			return false;
		}
	}

	content->applyFileInfo();
	bool result = content->Validate();
	contentSize = g_fileManager->getVirtualAddress()-position;

	// restore info of this command
	applyFileInfo();

	if (areaSize < contentSize)
	{
		Logger::queueError(Logger::Error,L"Area overflowed");
	}

	if (fillExpression.isLoaded())
		g_fileManager->advanceMemory(areaSize-contentSize);

	if (areaSize != oldAreaSize || contentSize != oldContentSize)
		result = true;

	return result;
}

void CDirectiveArea::Encode() const
{
	content->Encode();

	if (fillExpression.isLoaded())
	{
		unsigned char buffer[64];
		memset(buffer,fillValue,64);
		
		size_t writeSize = areaSize-contentSize;
		while (writeSize > 0)
		{
			size_t part = std::min<size_t>(64,writeSize);
			g_fileManager->write(buffer,part);
			writeSize -= part;
		}
	}
}

void CDirectiveArea::writeTempData(TempData& tempData) const
{
	tempData.writeLine(position,formatString(L".area 0x%08X",areaSize));
	content->applyFileInfo();
	content->writeTempData(tempData);

	if (fillExpression.isLoaded())
	{
		std::wstring fillString = formatString(L".fill 0x%08X,0x%02X",areaSize-contentSize,fillValue);
		tempData.writeLine(position+contentSize,fillString);
		tempData.writeLine(position+areaSize,L".endarea");
	} else {
		tempData.writeLine(position+contentSize,L".endarea");
	}
}

void CDirectiveArea::writeSymData(SymbolData& symData) const
{
	content->writeSymData(symData);

	if (fillExpression.isLoaded())
		symData.addData(position+contentSize,areaSize-contentSize,SymbolData::Data8);
}
