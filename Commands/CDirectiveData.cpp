#include "stdafx.h"
#include "Commands/CDirectiveData.h"
#include "Core/Common.h"
#include "Core/FileManager.h"

//
// TableCommand
//

TableCommand::TableCommand(const std::wstring& fileName, TextFile::Encoding encoding)
{
	if (fileExists(fileName) == false)
	{
		Logger::printError(Logger::Error,L"Table file \"%s\" does not exist",fileName);
		return;
	}

	if (table.load(fileName,encoding) == false)
	{
		Logger::printError(Logger::Error,L"Invalid table file \"%s\"",fileName);
		return;
	}
}

bool TableCommand::Validate()
{
	Global.Table = table;
	return false;
}


//
// CDirectiveData
//

CDirectiveData::CDirectiveData()
{
	mode = EncodingMode::Invalid;
	writeTermination = false;
	endianness = Arch->getEndianness();
}

CDirectiveData::~CDirectiveData()
{

}

void CDirectiveData::setNormal(std::vector<Expression>& entries, size_t unitSize)
{
	switch (unitSize)
	{
	case 1:
		this->mode = EncodingMode::U8;
		break;
	case 2:
		this->mode = EncodingMode::U16;
		break;
	case 4:
		this->mode = EncodingMode::U32;
		break;
	case 8:
		this->mode = EncodingMode::U64;
		break;
	default:
		Logger::printError(Logger::Error,L"Invalid data unit size %d",unitSize);
		return;
	}
	
	this->entries = entries;
	this->writeTermination = false;
	normalData.reserve(entries.size());
}

void CDirectiveData::setFloat(std::vector<Expression>& entries)
{
	this->mode = EncodingMode::Float;
	this->entries = entries;
	this->writeTermination = false;
}

void CDirectiveData::setDouble(std::vector<Expression>& entries)
{
	this->mode = EncodingMode::Double;
	this->entries = entries;
	this->writeTermination = false;
}

void CDirectiveData::setAscii(std::vector<Expression>& entries, bool terminate)
{
	this->mode = EncodingMode::Ascii;
	this->entries = entries;
	this->writeTermination = terminate;
}

void CDirectiveData::setSjis(std::vector<Expression>& entries, bool terminate)
{
	this->mode = EncodingMode::Sjis;
	this->entries = entries;
	this->writeTermination = terminate;
}

void CDirectiveData::setCustom(std::vector<Expression>& entries, bool terminate)
{
	this->mode = EncodingMode::Custom;
	this->entries = entries;
	this->writeTermination = terminate;
}

size_t CDirectiveData::getUnitSize() const
{
	switch (mode)
	{
	case EncodingMode::U8:
	case EncodingMode::Ascii:
	case EncodingMode::Sjis:
	case EncodingMode::Custom:
		return 1;
	case EncodingMode::U16:
		return 2;
	case EncodingMode::U32:
	case EncodingMode::Float:
		return 4;
	case EncodingMode::U64:
	case EncodingMode::Double:
		return 8;
	}

	return 0;
}

size_t CDirectiveData::getDataSize() const
{
	switch (mode)
	{
	case EncodingMode::Sjis:
	case EncodingMode::Custom:
		return customData.size();
	case EncodingMode::U8:
	case EncodingMode::Ascii:
	case EncodingMode::U16:
	case EncodingMode::U32:
	case EncodingMode::U64:
	case EncodingMode::Float:
	case EncodingMode::Double:
		return normalData.size()*getUnitSize();
	}

	return 0;
}

void CDirectiveData::encodeCustom(EncodingTable& table)
{
	customData.clear();
	for (size_t i = 0; i < entries.size(); i++)
	{
		ExpressionValue value = entries[i].evaluate();
		if (!value.isValid())
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			continue;
		}
		
		if (value.isInt())
		{
			customData.appendByte(value.intValue);
		} else if (value.isString())
		{
			ByteArray encoded = table.encodeString(value.strValue,false);
			if (encoded.size() == 0 && value.strValue.size() > 0)
			{
				Logger::queueError(Logger::Error,L"Failed to encode \"%s\"",value.strValue);
			}
			customData.append(encoded);
		} else {
			Logger::queueError(Logger::Error,L"Invalid expression type");
		}
	}

	if (writeTermination)
	{
		ByteArray encoded = table.encodeTermination();
		customData.append(encoded);
	}
}

void CDirectiveData::encodeSjis()
{
	static EncodingTable sjisTable;
	if (sjisTable.isLoaded() == false)
	{
		unsigned char hexBuffer[2];
		
		sjisTable.setTerminationEntry((unsigned char*)"\0",1);

		for (unsigned short SJISValue = 0x0001; SJISValue < 0x0100; SJISValue++)
		{
			wchar_t unicodeValue = sjisToUnicode(SJISValue);
			if (unicodeValue != 0xFFFF)
			{
				hexBuffer[0] = SJISValue & 0xFF;
				sjisTable.addEntry(hexBuffer, 1, unicodeValue);
			}
		}
		for (unsigned short SJISValue = 0x8100; SJISValue < 0xEF00; SJISValue++)
		{
			wchar_t unicodeValue = sjisToUnicode(SJISValue);
			if (unicodeValue != 0xFFFF)
			{
				hexBuffer[0] = (SJISValue >> 8) & 0xFF;
				hexBuffer[1] = SJISValue & 0xFF;
				sjisTable.addEntry(hexBuffer, 2, unicodeValue);
			}
		}
	}

	encodeCustom(sjisTable);
}

void CDirectiveData::encodeFloat()
{
	normalData.clear();
	for (size_t i = 0; i < entries.size(); i++)
	{
		ExpressionValue value = entries[i].evaluate();
		if (!value.isValid())
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			continue;
		}

		if (value.isInt() && mode == EncodingMode::Float)
		{
			int32_t num = getFloatBits((float)value.intValue);
			normalData.push_back(num);
		} else if (value.isInt() && mode == EncodingMode::Double)
		{
			int64_t num = getDoubleBits((double)value.intValue);
			normalData.push_back(num);
		} else if (value.isFloat() && mode == EncodingMode::Float)
		{
			int32_t num = getFloatBits((float)value.floatValue);
			normalData.push_back(num);
		} else if (value.isFloat() && mode == EncodingMode::Double)
		{
			int64_t num = getDoubleBits((double)value.floatValue);
			normalData.push_back(num);
		} else {
			Logger::queueError(Logger::Error,L"Invalid expression type");
		}
	}
}

void CDirectiveData::encodeNormal()
{
	normalData.clear();
	for (size_t i = 0; i < entries.size(); i++)
	{
		ExpressionValue value = entries[i].evaluate();
		if (!value.isValid())
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			continue;
		}

		if (value.isString())
		{
			bool hadNonAscii = false;
			for (size_t l = 0; l < value.strValue.size(); l++)
			{
				int64_t num = value.strValue[l];
				normalData.push_back(num);

				if (num >= 0x80 && hadNonAscii == false)
				{
					Logger::printError(Logger::Warning,L"Non-ASCII character in data directive. Use .string instead");
					hadNonAscii = true;
				}
			}
		} else if (value.isInt())
		{
			int64_t num = value.intValue;
			normalData.push_back(num);
		} else if (value.isFloat() && mode == EncodingMode::U32)
		{
			int32_t num = getFloatBits((float)value.floatValue);
			normalData.push_back(num);
		} else if(value.isFloat() && mode == EncodingMode::U64) {
			int64_t num = getDoubleBits((double)value.floatValue);
			normalData.push_back(num);
		} else {
			Logger::queueError(Logger::Error,L"Invalid expression type");
		}
	}

	if (writeTermination)
	{
		normalData.push_back(0);
	}
}

bool CDirectiveData::Validate()
{
	position = g_fileManager->getVirtualAddress();

	size_t oldSize = getDataSize();
	switch (mode)
	{
	case EncodingMode::U8:
	case EncodingMode::U16:
	case EncodingMode::U32:
	case EncodingMode::U64:
	case EncodingMode::Ascii:
		encodeNormal();
		break;
	case EncodingMode::Float:
	case EncodingMode::Double:
		encodeFloat();
		break;
	case EncodingMode::Sjis:
		encodeSjis();
		break;
	case EncodingMode::Custom:
		encodeCustom(Global.Table);
		break;
	default:
		Logger::queueError(Logger::Error,L"Invalid encoding type");
		break;
	}

	g_fileManager->advanceMemory(getDataSize());
	return oldSize != getDataSize();
}

void CDirectiveData::Encode() const
{
	switch (mode)
	{
	case EncodingMode::Sjis:
	case EncodingMode::Custom:
		g_fileManager->write(customData.data(),customData.size());
		break;
	case EncodingMode::U8:
	case EncodingMode::Ascii:
		for (auto value: normalData)
		{
			g_fileManager->writeU8((uint8_t)value);
		}
		break;
	case EncodingMode::U16:
		for (auto value: normalData)
		{
			g_fileManager->writeU16((uint16_t)value);
		}
		break;
	case EncodingMode::U32:
	case EncodingMode::Float:
		for (auto value: normalData)
		{
			g_fileManager->writeU32((uint32_t)value);
		}
		break;
	case EncodingMode::U64:
	case EncodingMode::Double:
		for (auto value: normalData)
		{
			g_fileManager->writeU64((uint64_t)value);
		}
		break;
	}
}

void CDirectiveData::writeTempData(TempData& tempData) const
{
	size_t size = (getUnitSize()*2+3)*getDataSize()+20;
	wchar_t* str = new wchar_t[size];
	wchar_t* start = str;

	switch (mode)
	{
	case EncodingMode::Sjis:
	case EncodingMode::Custom:
		str += swprintf(str,20,L".byte ");

		for (size_t i = 0; i < customData.size(); i++)
		{
			str += swprintf(str,20,L"0x%02X,",(int8_t)customData[i]);
		}
		break;
	case EncodingMode::U8:
	case EncodingMode::Ascii:
		str += swprintf(str,20,L".byte ");
		
		for (size_t i = 0; i < normalData.size(); i++)
		{
			str += swprintf(str,20,L"0x%02X,",(int8_t)normalData[i]);
		}
		break;
	case EncodingMode::U16:
		str += swprintf(str,20,L".halfword ");

		for (size_t i = 0; i < normalData.size(); i++)
		{
			str += swprintf(str,20,L"0x%04X,",(int16_t)normalData[i]);
		}
		break;
	case EncodingMode::U32:
	case EncodingMode::Float:
		str += swprintf(str,20,L".word ");

		for (size_t i = 0; i < normalData.size(); i++)
		{
			str += swprintf(str,20,L"0x%08X,",(int32_t)normalData[i]);
		}
		break;
	case EncodingMode::U64:
	case EncodingMode::Double:
		str += swprintf(str,20,L".doubleword ");

		for (size_t i = 0; i < normalData.size(); i++)
		{
			str += swprintf(str,20,L"0x%16X,",(int64_t)normalData[i]);
		}
		break;
	}

	*(str-1) = 0;
	tempData.writeLine(position,start);
	delete[] start;
}

void CDirectiveData::writeSymData(SymbolData& symData) const
{
	switch (mode)
	{
	case EncodingMode::Ascii:
		symData.addData(position,getDataSize(),SymbolData::DataAscii);
		break;
	case EncodingMode::U8:
	case EncodingMode::Sjis:
	case EncodingMode::Custom:
		symData.addData(position,getDataSize(),SymbolData::Data8);
		break;
	case EncodingMode::U16:
		symData.addData(position,getDataSize(),SymbolData::Data16);
		break;
	case EncodingMode::U32:
	case EncodingMode::Float:
		symData.addData(position,getDataSize(),SymbolData::Data32);
		break;
	case EncodingMode::U64:
	case EncodingMode::Double:
		symData.addData(position,getDataSize(),SymbolData::Data64);
		break;
	}
}
