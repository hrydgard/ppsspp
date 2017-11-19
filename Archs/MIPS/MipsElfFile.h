#pragma once
#include "Core/ELF/ElfFile.h"
#include "Core/FileManager.h"
#include "Commands/CAssemblerCommand.h"
#include "Mips.h"

class MipsElfFile: public AssemblerFile
{
public:
	MipsElfFile();
	virtual bool open(bool onlyCheck);
	virtual void close();
	virtual bool isOpen() { return opened; };
	virtual bool write(void* data, size_t length);
	virtual int64_t getVirtualAddress();
	virtual int64_t getPhysicalAddress();
	virtual int64_t getHeaderSize();
	virtual bool seekVirtual(int64_t virtualAddress);
	virtual bool seekPhysical(int64_t physicalAddress);
	virtual bool getModuleInfo(SymDataModuleInfo& info);
	virtual void beginSymData(SymbolData& symData);
	virtual void endSymData(SymbolData& symData);
	virtual const std::wstring& getFileName() { return fileName; };

	bool load(const std::wstring& fileName, const std::wstring& outputFileName);
	void save();
	bool setSection(const std::wstring& name);
private:
	ElfFile elf;
	std::wstring fileName;
	std::wstring outputFileName;
	bool opened;
	int platform;

	int segment;
	int section;
	size_t sectionOffset;
};


class DirectiveLoadMipsElf: public CAssemblerCommand
{
public:
	DirectiveLoadMipsElf(const std::wstring& fileName);
	DirectiveLoadMipsElf(const std::wstring& inputName, const std::wstring& outputName);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	MipsElfFile* file;
	std::wstring inputName;
	std::wstring outputName;
};
