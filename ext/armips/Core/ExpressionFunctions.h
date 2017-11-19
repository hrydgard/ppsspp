#pragma once
#include "Expression.h"
#include <map>

typedef ExpressionValue (*ExpressionFunction)(const std::wstring& funcName, const std::vector<ExpressionValue>&);

enum class ExpFuncSafety
{
	// Result may depend entirely on the internal state
	Unsafe,
	// Result is unsafe in conditional blocks, safe otherwise
	ConditionalUnsafe,
	// Result is completely independent of the internal state
	Safe,
};

struct ExpressionFunctionEntry
{
	ExpressionFunction function;
	size_t minParams;
	size_t maxParams;
	ExpFuncSafety safety;
};

typedef std::map<std::wstring, const ExpressionFunctionEntry> ExpressionFunctionMap;

extern const ExpressionFunctionMap expressionFunctions;

ExpressionValue expFuncDefined(ExpressionInternal* exp);
