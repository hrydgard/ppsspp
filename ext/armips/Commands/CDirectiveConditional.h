#pragma once
#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"

enum class ConditionType
{
	IF,
	ELSE,
	ELSEIF,
	ENDIF,
	IFDEF,
	IFNDEF,
};

class CDirectiveConditional: public CAssemblerCommand
{
public:
	CDirectiveConditional(ConditionType type);
	CDirectiveConditional(ConditionType type, const std::wstring& name);
	CDirectiveConditional(ConditionType type, const Expression& exp);
	~CDirectiveConditional();
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
	void setContent(CAssemblerCommand* ifBlock, CAssemblerCommand* elseBlock);
private:
	bool evaluate();

	Expression expression;
	Label* label;
	bool previousResult;

	ConditionType type;
	CAssemblerCommand* ifBlock;
	CAssemblerCommand* elseBlock;
};
