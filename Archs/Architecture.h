#pragma once
#include "../Commands/CAssemblerCommand.h"
#include "../Core/FileManager.h"

class IElfRelocator;
class Tokenizer;
class Parser;

class CArchitecture
{
public:
	virtual CAssemblerCommand* parseDirective(Parser& parserr) { return nullptr; };
	virtual CAssemblerCommand* parseOpcode(Parser& parser) { return nullptr; };
	virtual void NextSection() = 0;
	virtual void Pass2() = 0;
	virtual void Revalidate() = 0;
	virtual IElfRelocator* getElfRelocator() = 0;
	virtual Endianness getEndianness() = 0;
};

class ArchitectureCommand: public CAssemblerCommand
{
public:
	ArchitectureCommand(const std::wstring& tempText, const std::wstring& symText);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	int64_t position;
	Endianness endianness;
	std::wstring tempText;
	std::wstring symText;
};

class CInvalidArchitecture: public CArchitecture
{
public:
	virtual void NextSection();
	virtual void Pass2();
	virtual void Revalidate();
	virtual IElfRelocator* getElfRelocator();
	virtual Endianness getEndianness() { return Endianness::Little; };
};

extern CInvalidArchitecture InvalidArchitecture;
