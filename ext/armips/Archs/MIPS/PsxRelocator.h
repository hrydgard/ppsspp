#pragma once
#include "Util/ByteArray.h"
#include "Core/SymbolTable.h"
#include "Mips.h"
#include "Commands/CAssemblerCommand.h"

enum class PsxRelocationType { WordLiteral, UpperImmediate, LowerImmediate, FunctionCall };
enum class PsxRelocationRefType { SymblId, SegmentOffset };

struct PsxRelocation
{
	PsxRelocationType type;
	PsxRelocationRefType refType;
	int segmentOffset;
	int referenceId;
	int referencePos;
	int relativeOffset;
	int filePos;
};

struct PsxSegment
{
	std::wstring name;
	int id;
	ByteArray data;
	std::vector<PsxRelocation> relocations;
};


enum class PsxSymbolType { Internal, InternalID, External, BSS, Function };

struct PsxSymbol
{
	PsxSymbolType type;
	std::wstring name;
	int segment;
	int offset;
	int id;
	int size;
	Label* label;
};

struct PsxRelocatorFile
{
	std::wstring name;
	std::vector<PsxSegment> segments;
	std::vector<PsxSymbol> symbols;
};

class PsxRelocator
{
public:
	bool init(const std::wstring& inputName);
	bool relocate(int& memoryAddress);
	bool hasDataChanged() { return dataChanged; };
	const ByteArray& getData() const { return outputData; };
	void writeSymbols(SymbolData& symData) const;
private:
	size_t loadString(ByteArray& data, size_t pos, std::wstring& dest);
	bool parseObject(ByteArray data, PsxRelocatorFile& dest);
	bool relocateFile(PsxRelocatorFile& file, int& relocationAddress);
	
	ByteArray outputData;
	std::vector<PsxRelocatorFile> files;
	MipsElfRelocator* reloc;
	bool dataChanged;
};

class DirectivePsxObjImport: public CAssemblerCommand
{
public:
	DirectivePsxObjImport(const std::wstring& fileName);
	~DirectivePsxObjImport() { };
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const { };
	virtual void writeSymData(SymbolData& symData) const;
private:
	PsxRelocator rel;
};