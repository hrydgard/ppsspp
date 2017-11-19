#include "stdafx.h"
#include "SymbolData.h"
#include "FileManager.h"
#include "Misc.h"
#include "Common.h"
#include <algorithm>

SymbolData::SymbolData()
{
	clear();
}

void SymbolData::clear()
{
	enabled = true;
	nocashSymFileName.clear();
	modules.clear();
	files.clear();
	currentModule = 0;
	currentFunction = -1;
	
	SymDataModule defaultModule;
	defaultModule.file = NULL;
	modules.push_back(defaultModule);
}

struct NocashSymEntry
{
	int64_t address;
	std::wstring text;

	bool operator<(const NocashSymEntry& other) const
	{
		if (address != other.address)
			return address < other.address;
		return text < other.text;
	}
};

void SymbolData::writeNocashSym()
{
	if (nocashSymFileName.empty())
		return;

	std::vector<NocashSymEntry> entries;
	for (size_t k = 0; k < modules.size(); k++)
	{
		SymDataModule& module = modules[k];
		for (size_t i = 0; i < module.symbols.size(); i++)
		{
			SymDataSymbol& sym = module.symbols[i];
			
			size_t size = 0;
			for (size_t f = 0; f < module.functions.size(); f++)
			{
				if (module.functions[f].address == sym.address)
				{
					size = module.functions[f].size;
					break;
				}
			}

			NocashSymEntry entry;
			entry.address = sym.address;

			if (size != 0 && nocashSymVersion >= 2)
				entry.text = formatString(L"%s,%08X",sym.name,size);
			else
				entry.text = sym.name;

			if (nocashSymVersion == 1)
				std::transform(entry.text.begin(), entry.text.end(), entry.text.begin(), ::towlower);

			entries.push_back(entry);
		}

		for (const SymDataData& data: module.data)
		{
			NocashSymEntry entry;
			entry.address = data.address;

			switch (data.type)
			{
			case Data8:
				entry.text = formatString(L".byt:%04X",data.size);
				break;
			case Data16:
				entry.text = formatString(L".wrd:%04X",data.size);
				break;
			case Data32:
				entry.text = formatString(L".dbl:%04X",data.size);
				break;
			case Data64:
				entry.text = formatString(L".dbl:%04X",data.size);
				break;
			case DataAscii:
				entry.text = formatString(L".asc:%04X",data.size);
				break;
			}

			entries.push_back(entry);
		}
	}

	std::sort(entries.begin(),entries.end());
	
	TextFile file;
	if (file.open(nocashSymFileName,TextFile::Write,TextFile::ASCII) == false)
	{
		Logger::printError(Logger::Error,L"Could not open sym file %s.",file.getFileName());
		return;
	}
	file.writeLine(L"00000000 0");

	for (size_t i = 0; i < entries.size(); i++)
	{
		file.writeFormat(L"%08X %s\n",entries[i].address,entries[i].text);
	}

	file.write("\x1A");
	file.close();
}

void SymbolData::write()
{
	writeNocashSym();
}

void SymbolData::addLabel(int64_t memoryAddress, const std::wstring& name)
{
	if (!enabled)
		return;
	
	SymDataSymbol sym;
	sym.address = memoryAddress;
	sym.name = name;

	for (SymDataSymbol& symbol: modules[currentModule].symbols)
	{
		if (symbol.address == sym.address && symbol.name == sym.name)
			return;
	}

	modules[currentModule].symbols.push_back(sym);
}

void SymbolData::addData(int64_t address, size_t size, DataType type)
{
	if (!enabled)
		return;

	SymDataData data;
	data.address = address;
	data.size = size;
	data.type = type;
	modules[currentModule].data.insert(data);
}

size_t SymbolData::addFileName(const std::wstring& fileName)
{
	for (size_t i = 0; i < files.size(); i++)
	{
		if (files[i] == fileName)
			return i;
	}

	files.push_back(fileName);
	return files.size()-1;
}

void SymbolData::startModule(AssemblerFile* file)
{
	for (size_t i = 0; i < modules.size(); i++)
	{
		if (modules[i].file == file)
		{
			currentModule = i;
			return;
		}
	}

	SymDataModule module;
	module.file = file;
	modules.push_back(module);
	currentModule = modules.size()-1;
}

void SymbolData::endModule(AssemblerFile* file)
{
	if (modules[currentModule].file != file)
		return;

	if (currentModule == 0)
	{
		Logger::printError(Logger::Error,L"No module opened");
		return;
	}

	if (currentFunction != -1)
	{
		Logger::printError(Logger::Error,L"Module closed before function end");
		currentFunction = -1;
	}

	currentModule = 0;
}

void SymbolData::startFunction(int64_t address)
{
	if (currentFunction != -1)
	{
		endFunction(address);
	}

	currentFunction = modules[currentModule].functions.size();

	SymDataFunction func;
	func.address = address;
	func.size = 0;
	modules[currentModule].functions.push_back(func);
}

void SymbolData::endFunction(int64_t address)
{
	if (currentFunction == -1)
	{
		Logger::printError(Logger::Error,L"Not inside a function");
		return;
	}

	SymDataFunction& func = modules[currentModule].functions[currentFunction];
	func.size = (size_t) (address-func.address);
	currentFunction = -1;
}
