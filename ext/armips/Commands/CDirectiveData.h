#pragma once
#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"
#include "Util/EncodingTable.h"
#include "../Archs/Architecture.h"

enum class EncodingMode { Invalid, U8, U16, U32, U64, Ascii, Float, Double, Sjis, Custom };

class TableCommand: public CAssemblerCommand
{
public:
	TableCommand(const std::wstring& fileName, TextFile::Encoding encoding);
	virtual bool Validate();
	virtual void Encode() const { };
	virtual void writeTempData(TempData& tempData) const { };
	virtual void writeSymData(SymbolData& symData) const { };
private:
	EncodingTable table;
};

class CDirectiveData: public CAssemblerCommand
{
public:
	CDirectiveData();
	~CDirectiveData();
	void setNormal(std::vector<Expression>& entries, size_t unitSize);
	void setFloat(std::vector<Expression>& entries);
	void setDouble(std::vector<Expression>& entries);
	void setAscii(std::vector<Expression>& entries, bool terminate);
	void setSjis(std::vector<Expression>& entries, bool terminate);
	void setCustom(std::vector<Expression>& entries, bool terminate);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	void encodeCustom(EncodingTable& table);
	void encodeSjis();
	void encodeFloat();
	void encodeDouble();
	void encodeNormal();
	size_t getUnitSize() const;
	size_t getDataSize() const;
	
	int64_t position;
	EncodingMode mode;
	bool writeTermination;
	std::vector<Expression> entries;
	ByteArray customData;
	std::vector<int64_t> normalData;
	Endianness endianness;
};
