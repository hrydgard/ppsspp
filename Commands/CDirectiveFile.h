#pragma once
#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"
#include "Core/ELF/ElfRelocator.h"

class GenericAssemblerFile;

class CDirectiveFile: public CAssemblerCommand
{
public:
	enum class Type { Invalid, Open, Create, Copy, Close };

	CDirectiveFile();
	void initOpen(const std::wstring& fileName, int64_t memory);
	void initCreate(const std::wstring& fileName, int64_t memory);
	void initCopy(const std::wstring& inputName, const std::wstring& outputName, int64_t memory);
	void initClose();

	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	Type type;
	int64_t virtualAddress;
	GenericAssemblerFile* file;
	AssemblerFile* closeFile;
};

class CDirectivePosition: public CAssemblerCommand
{
public:
	enum Type { Physical, Virtual };
	CDirectivePosition(Expression value, Type type);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const { };
private:
	void exec() const;
	Type type;
	Expression expression;
	int64_t position;
	int64_t virtualAddress;
};

class CDirectiveIncbin: public CAssemblerCommand
{
public:
	CDirectiveIncbin(const std::wstring& fileName);
	void setStart(Expression& exp) { startExpression = exp; };
	void setSize(Expression& exp) { sizeExpression = exp; };

	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	std::wstring fileName;
	int64_t fileSize;

	Expression startExpression;
	Expression sizeExpression;
	int64_t start;
	int64_t size;
	int64_t virtualAddress;
};

class CDirectiveAlignFill: public CAssemblerCommand
{
public:
	enum Mode { Align, Fill };

	CDirectiveAlignFill(int64_t value, Mode mode);
	CDirectiveAlignFill(Expression& value, Mode mode);
	CDirectiveAlignFill(Expression& value, Expression& fillValue, Mode mode);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	Mode mode;
	Expression valueExpression;
	Expression fillExpression;
	int64_t value;
	int64_t finalSize;
	int8_t fillByte;
	int64_t virtualAddress;
};

class CDirectiveSkip: public CAssemblerCommand
{
public:
	CDirectiveSkip(Expression& value);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const { };
private:
	Expression expression;
	int64_t value;
	int64_t virtualAddress;
};

class CDirectiveHeaderSize: public CAssemblerCommand
{
public:
	CDirectiveHeaderSize(Expression expression);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const { };
private:
	void exec() const;
	Expression expression;
	int64_t headerSize;
	int64_t virtualAddress;
};

class DirectiveObjImport: public CAssemblerCommand
{
public:
	DirectiveObjImport(const std::wstring& inputName);
	DirectiveObjImport(const std::wstring& inputName, const std::wstring& ctorName);
	~DirectiveObjImport() { };
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
private:
	ElfRelocator rel;
	CAssemblerCommand* ctor;
};
