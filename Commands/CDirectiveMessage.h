#pragma once
#include "Commands/CAssemblerCommand.h"
#include "Core/Common.h"
#include "Core/Expression.h"

class CDirectiveMessage: public CAssemblerCommand
{
public:
	enum class Type { Invalid, Warning, Error, Notice };	
	CDirectiveMessage(Type type, Expression exp);
	virtual bool Validate();
	virtual void Encode() const {};
	virtual void writeTempData(TempData& tempData) const { };
private:
	Type errorType;
	Expression exp;
};

class CDirectiveSym: public CAssemblerCommand
{
public:
	CDirectiveSym(bool enable) {enabled = enable; };
	virtual bool Validate() { return false; };
	virtual void Encode() const { };
	virtual void writeTempData(TempData& tempData) const { };
	virtual void writeSymData(SymbolData& symData) const { symData.setEnabled(enabled); }
private:
	bool enabled;
};
