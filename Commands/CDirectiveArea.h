#pragma once
#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"

class CDirectiveArea: public CAssemblerCommand
{
public:
	CDirectiveArea(Expression& size);
	~CDirectiveArea();
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void writeSymData(SymbolData& symData) const;
	void setFillExpression(Expression& exp);
	void setContent(CAssemblerCommand* content) { this->content = content; }
private:
	int64_t position;
	Expression sizeExpression;
	size_t areaSize;
	size_t contentSize;
	Expression fillExpression;
	int8_t fillValue;
	CAssemblerCommand* content;
};
