#include "stdafx.h"
#include "Expression.h"
#include "Common.h"
#include "ExpressionFunctions.h"

enum class ExpressionValueCombination
{
	II = (int(ExpressionValueType::Integer) << 2) | (int(ExpressionValueType::Integer) << 0),
	IF = (int(ExpressionValueType::Integer) << 2) | (int(ExpressionValueType::Float)   << 0),
	FI = (int(ExpressionValueType::Float)   << 2) | (int(ExpressionValueType::Integer) << 0),
	FF = (int(ExpressionValueType::Float)   << 2) | (int(ExpressionValueType::Float)   << 0),
	IS = (int(ExpressionValueType::Integer) << 2) | (int(ExpressionValueType::String)  << 0),
	FS = (int(ExpressionValueType::Float)   << 2) | (int(ExpressionValueType::String)  << 0),
	SI = (int(ExpressionValueType::String)  << 2) | (int(ExpressionValueType::Integer) << 0),
	SF = (int(ExpressionValueType::String)  << 2) | (int(ExpressionValueType::Float)   << 0),
	SS = (int(ExpressionValueType::String)  << 2) | (int(ExpressionValueType::String)  << 0),
};

ExpressionValueCombination getValueCombination(ExpressionValueType a, ExpressionValueType b)
{
	return (ExpressionValueCombination) ((int(a) << 2) | (int(b) << 0));
}

ExpressionValue ExpressionValue::operator+(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue + other.intValue;
		break;
	case ExpressionValueCombination::FI:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue + other.intValue;
		break;
	case ExpressionValueCombination::IF:
		result.type = ExpressionValueType::Float;
		result.floatValue = intValue + other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue + other.floatValue;
		break;
	case ExpressionValueCombination::IS:
		result.type = ExpressionValueType::String;
		result.strValue = to_wstring(intValue) + other.strValue;
		break;
	case ExpressionValueCombination::FS:
		result.type = ExpressionValueType::String;
		result.strValue = to_wstring(floatValue) + other.strValue;
		break;
	case ExpressionValueCombination::SI:
		result.type = ExpressionValueType::String;
		result.strValue = strValue + to_wstring(other.intValue);
		break;
	case ExpressionValueCombination::SF:
		result.type = ExpressionValueType::String;
		result.strValue = strValue + to_wstring(other.floatValue);
		break;
	case ExpressionValueCombination::SS:
		result.type = ExpressionValueType::String;
		result.strValue = strValue + other.strValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator-(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue - other.intValue;
		break;
	case ExpressionValueCombination::FI:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue - other.intValue;
		break;
	case ExpressionValueCombination::IF:
		result.type = ExpressionValueType::Float;
		result.floatValue = intValue - other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue - other.floatValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator*(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue * other.intValue;
		break;
	case ExpressionValueCombination::FI:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue * other.intValue;
		break;
	case ExpressionValueCombination::IF:
		result.type = ExpressionValueType::Float;
		result.floatValue = intValue * other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue * other.floatValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator/(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		if (other.intValue == 0)
		{
			result.type = ExpressionValueType::String;
			result.strValue = L"undef";
			return result;
		}
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue / other.intValue;
		break;
	case ExpressionValueCombination::FI:
		if (other.intValue == 0)
		{
			result.type = ExpressionValueType::String;
			result.strValue = L"undef";
			return result;
		}
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue / other.intValue;
		break;
	case ExpressionValueCombination::IF:
		if (other.floatValue == 0)
		{
			result.type = ExpressionValueType::String;
			result.strValue = L"undef";
			return result;
		}
		result.type = ExpressionValueType::Float;
		result.floatValue = intValue / other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		if (other.floatValue == 0)
		{
			result.type = ExpressionValueType::String;
			result.strValue = L"undef";
			return result;
		}
		result.type = ExpressionValueType::Float;
		result.floatValue = floatValue / other.floatValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator%(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		if (other.intValue == 0)
		{
			result.type = ExpressionValueType::String;
			result.strValue = L"undef";
			return result;
		}
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue % other.intValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator!() const
{
	ExpressionValue result;
	result.type = ExpressionValueType::Integer;
	
	if (isFloat())
		result.intValue = !floatValue;
	else
		result.intValue = !intValue;

	return result;
}

ExpressionValue ExpressionValue::operator~() const
{
	ExpressionValue result;

	if (isInt())
	{
		result.type = ExpressionValueType::Integer;
		result.intValue = ~intValue;
	}

	return result;
}

ExpressionValue ExpressionValue::operator<<(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = ((uint64_t) intValue) << other.intValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator>>(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = ((uint64_t) intValue) >> other.intValue;
		break;
	}

	return result;
}

bool ExpressionValue::operator<(const ExpressionValue& other) const
{
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		return intValue < other.intValue;
	case ExpressionValueCombination::FI:
		return floatValue < other.intValue;
	case ExpressionValueCombination::IF:
		return intValue < other.floatValue;
	case ExpressionValueCombination::FF:
		return floatValue < other.floatValue;
	}

	return false;
}

bool ExpressionValue::operator<=(const ExpressionValue& other) const
{
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		return intValue <= other.intValue;
	case ExpressionValueCombination::FI:
		return floatValue <= other.intValue;
	case ExpressionValueCombination::IF:
		return intValue <= other.floatValue;
	case ExpressionValueCombination::FF:
		return floatValue <= other.floatValue;
	}

	return false;
}

bool ExpressionValue::operator>(const ExpressionValue& other) const
{
	return !(*this <= other);
}

bool ExpressionValue::operator>=(const ExpressionValue& other) const
{
	return !(*this < other);
}

bool ExpressionValue::operator==(const ExpressionValue& other) const
{
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		return intValue == other.intValue;
	case ExpressionValueCombination::FI:
		return floatValue == other.intValue;
	case ExpressionValueCombination::IF:
		return intValue == other.floatValue;
	case ExpressionValueCombination::FF:
		return floatValue == other.floatValue;
	case ExpressionValueCombination::IS:
		return to_wstring(intValue) == other.strValue;
	case ExpressionValueCombination::FS:
		return to_wstring(floatValue) == other.strValue;
	case ExpressionValueCombination::SI:
		return strValue == to_wstring(other.intValue);
	case ExpressionValueCombination::SF:
		return strValue == to_wstring(other.floatValue);
	case ExpressionValueCombination::SS:
		return strValue == other.strValue;
	}

	return false;
}

bool ExpressionValue::operator!=(const ExpressionValue& other) const
{
	return !(*this == other);
}

ExpressionValue ExpressionValue::operator&(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue & other.intValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator|(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue | other.intValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator&&(const ExpressionValue& other) const
{
	ExpressionValue result;
	result.type = ExpressionValueType::Integer;

	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.intValue = intValue && other.intValue;
		break;
	case ExpressionValueCombination::FI:
		result.floatValue = floatValue && other.intValue;
		break;
	case ExpressionValueCombination::IF:
		result.floatValue = intValue && other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		result.floatValue = floatValue && other.floatValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator||(const ExpressionValue& other) const
{
	ExpressionValue result;
	result.type = ExpressionValueType::Integer;

	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.intValue = intValue || other.intValue;
		break;
	case ExpressionValueCombination::FI:
		result.floatValue = floatValue || other.intValue;
		break;
	case ExpressionValueCombination::IF:
		result.floatValue = intValue || other.floatValue;
		break;
	case ExpressionValueCombination::FF:
		result.floatValue = floatValue || other.floatValue;
		break;
	}

	return result;
}

ExpressionValue ExpressionValue::operator^(const ExpressionValue& other) const
{
	ExpressionValue result;
	switch (getValueCombination(type,other.type))
	{
	case ExpressionValueCombination::II:
		result.type = ExpressionValueType::Integer;
		result.intValue = intValue ^ other.intValue;
		break;
	}

	return result;
}

ExpressionInternal::ExpressionInternal()
{
	children = nullptr;
	childrenCount = 0;
}

ExpressionInternal::~ExpressionInternal()
{
	deallocate();
}

ExpressionInternal::ExpressionInternal(int64_t value)
	: ExpressionInternal()
{
	type = OperatorType::Integer;
	intValue = value;
}

ExpressionInternal::ExpressionInternal(double value)
	: ExpressionInternal()
{
	type = OperatorType::Float;
	floatValue = value;
}

ExpressionInternal::ExpressionInternal(const std::wstring& value, OperatorType type)
	: ExpressionInternal()
{
	this->type = type;
	strValue = value;

	switch (type)
	{
	case OperatorType::Identifier:
		fileNum = Global.FileInfo.FileNum;
		section = Global.Section;
		break;
	case OperatorType::String:
		break;
	}
}

ExpressionInternal::ExpressionInternal(OperatorType op, ExpressionInternal* a,
	ExpressionInternal* b, ExpressionInternal* c)
	: ExpressionInternal()
{
	type = op;
	allocate(3);

	children[0] = a;
	children[1] = b;
	children[2] = c;
}

ExpressionInternal::ExpressionInternal(const std::wstring& name, const std::vector<ExpressionInternal*>& parameters)
	: ExpressionInternal()
{
	type = OperatorType::FunctionCall;
	allocate(parameters.size());

	strValue = name;
	for (size_t i = 0; i < parameters.size(); i++)
	{
		children[i] = parameters[i];
	}
}

void ExpressionInternal::allocate(size_t count)
{
	deallocate();

	children = new ExpressionInternal*[count];
	childrenCount = count;
}

void ExpressionInternal::deallocate()
{
	for (size_t i = 0; i < childrenCount; i++)
	{
		delete children[i];
	}

	delete[] children;
	children = nullptr;
	childrenCount = 0;
}

void ExpressionInternal::replaceMemoryPos(const std::wstring& identifierName)
{
	for (size_t i = 0; i < childrenCount; i++)
	{
		if (children[i] != NULL)
		{
			children[i]->replaceMemoryPos(identifierName);
		}
	}

	if (type == OperatorType::MemoryPos)
	{
		type = OperatorType::Identifier;
		strValue = identifierName;
		fileNum = Global.FileInfo.FileNum;
		section = Global.Section;
	}
}

bool ExpressionInternal::checkParameterCount(size_t minParams, size_t maxParams)
{
	if (minParams > childrenCount)
	{
		Logger::queueError(Logger::Error,L"Not enough parameters for \"%s\" (min %d)",strValue,minParams);
		return false;
	}

	if (maxParams < childrenCount)
	{
		Logger::queueError(Logger::Error,L"Too many parameters for \"%s\" (min %d)",strValue,maxParams);
		return false;
	}

	return true;
}

ExpressionValue ExpressionInternal::executeFunctionCall()
{
	ExpressionValue invalid;

	// handle defined(x) seperately, it's kind of a special case
	if (strValue == L"defined")
	{
		if (checkParameterCount(1,1) == false)
			return invalid;

		return expFuncDefined(children[0]);
	}

	// find function, check parameter counts
	auto it = expressionFunctions.find(strValue);
	if (it == expressionFunctions.end())
	{
		Logger::queueError(Logger::Error,L"Unknown function \"%s\"",strValue);
		return invalid;
	}

	if (checkParameterCount(it->second.minParams,it->second.maxParams) == false)
		return invalid;

	// evaluate parameters
	std::vector<ExpressionValue> params;
	params.reserve(childrenCount);

	for (size_t i = 0; i < childrenCount; i++)
	{
		ExpressionValue result = children[i]->evaluate();
		if (result.isValid() == false)
		{
			Logger::queueError(Logger::Error,L"Invalid expression");
			return result;
		}

		params.push_back(result);
	}

	// execute
	return it->second.function(strValue,params);
}

bool isExpressionFunctionSafe(const std::wstring& name, bool inUnknownOrFalseBlock)
{
	auto it = expressionFunctions.find(name);
	if (it == expressionFunctions.end())
		return name != L"defined";

	if (inUnknownOrFalseBlock && it->second.safety == ExpFuncSafety::ConditionalUnsafe)
		return false;

	return it->second.safety != ExpFuncSafety::Unsafe;
}

bool ExpressionInternal::simplify(bool inUnknownOrFalseBlock)
{
	// check if this expression can actually be simplified
	// without causing side effects
	switch (type)
	{
	case OperatorType::Identifier:
	case OperatorType::MemoryPos:
	case OperatorType::ToString:
		return false;
	case OperatorType::FunctionCall:
		if (isExpressionFunctionSafe(strValue, inUnknownOrFalseBlock) == false)
			return false;
		break;
	}

	// check if the same applies to all children
	bool canSimplify = true;
	for (size_t i = 0; i < childrenCount; i++)
	{
		if (children[i] != nullptr && children[i]->simplify(inUnknownOrFalseBlock) == false)
			canSimplify = false;
	}

	// if so, this expression can be evaluated into a constant
	if (canSimplify)
	{
		ExpressionValue value = evaluate();

		switch (value.type)
		{
		case ExpressionValueType::Integer:
			type = OperatorType::Integer;
			intValue = value.intValue;
			break;
		case ExpressionValueType::Float:
			type = OperatorType::Float;
			floatValue = value.floatValue;
			break;
		case ExpressionValueType::String:
			type = OperatorType::String;
			strValue = value.strValue;
			break;
		default:
			type = OperatorType::Invalid;
			break;
		}

		deallocate();
	}

	return canSimplify;
}

ExpressionValue ExpressionInternal::evaluate()
{
	ExpressionValue val;

	Label* label;
	switch (type)
	{
	case OperatorType::Integer:
		val.type = ExpressionValueType::Integer;
		val.intValue = intValue;
		return val;
	case OperatorType::Float:
		val.type = ExpressionValueType::Float;
		val.floatValue = floatValue;
		return val;
	case OperatorType::Identifier:
		label = Global.symbolTable.getLabel(strValue,fileNum,section);
		if (label == nullptr)
		{
			Logger::queueError(Logger::Error,L"Invalid label name \"%s\"",strValue);
			return val;
		}

		if (!label->isDefined())
		{
			Logger::queueError(Logger::Error,L"Undefined label \"%s\"",label->getName());
			return val;
		}

		val.type = ExpressionValueType::Integer;
		val.intValue = label->getValue();
		return val;
	case OperatorType::String:
		val.type = ExpressionValueType::String;
		val.strValue = strValue;
		return val;
	case OperatorType::MemoryPos:
		val.type = ExpressionValueType::Integer;
		val.intValue = g_fileManager->getVirtualAddress();
		return val;
	case OperatorType::ToString:
		val.type = ExpressionValueType::String;
		val.strValue = children[0]->toString();
		return val;
	case OperatorType::Add:
		return children[0]->evaluate() + children[1]->evaluate();
	case OperatorType::Sub:
		return children[0]->evaluate() - children[1]->evaluate();
	case OperatorType::Mult:
		return children[0]->evaluate() * children[1]->evaluate();
	case OperatorType::Div:
		return children[0]->evaluate() / children[1]->evaluate();
	case OperatorType::Mod:
		return children[0]->evaluate() % children[1]->evaluate();
	case OperatorType::Neg:
		val.type = ExpressionValueType::Integer;
		val.intValue = 0;
		return val - children[0]->evaluate();
	case OperatorType::LogNot:
		return !children[0]->evaluate();
	case OperatorType::BitNot:
		return ~children[0]->evaluate();
	case OperatorType::LeftShift:
		return children[0]->evaluate() << children[1]->evaluate();
	case OperatorType::RightShift:
		return children[0]->evaluate() >> children[1]->evaluate();
	case OperatorType::Less:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() < children[1]->evaluate();
		return val;
	case OperatorType::Greater:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() > children[1]->evaluate();
		return val;
	case OperatorType::LessEqual:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() <= children[1]->evaluate();
		return val;
	case OperatorType::GreaterEqual:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() >= children[1]->evaluate();
		return val;
	case OperatorType::Equal:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() == children[1]->evaluate();
		return val;
	case OperatorType::NotEqual:
		val.type = ExpressionValueType::Integer;
		val.intValue = children[0]->evaluate() != children[1]->evaluate();
		return val;
	case OperatorType::BitAnd:
		return children[0]->evaluate() & children[1]->evaluate();
	case OperatorType::BitOr:
		return children[0]->evaluate() | children[1]->evaluate();
	case OperatorType::LogAnd:
		return children[0]->evaluate() && children[1]->evaluate();
	case OperatorType::LogOr:
		return children[0]->evaluate() || children[1]->evaluate();
	case OperatorType::Xor:
		return children[0]->evaluate() ^ children[1]->evaluate();
	case OperatorType::TertiaryIf:
		val.type = ExpressionValueType::Integer;
		val.intValue = 0;
		if (children[0]->evaluate() == val)
			return children[2]->evaluate();
		else
			return children[1]->evaluate();
	case OperatorType::FunctionCall:
		return executeFunctionCall();
	default:
		return val;
	}
}

static std::wstring escapeString(const std::wstring& text)
{
	std::wstring result = text;
	replaceAll(result,LR"(\)",LR"(\\)");
	replaceAll(result,LR"(")",LR"(\")");

	return formatString(LR"("%s")",text);
}

std::wstring ExpressionInternal::formatFunctionCall()
{
	std::wstring text = strValue + L"(";

	for (size_t i = 0; i < childrenCount; i++)
	{
		if (i != 0)
			text += L",";
		text += children[i]->toString();
	}

	return text + L")";
}

std::wstring ExpressionInternal::toString()
{
	switch (type)
	{
	case OperatorType::Integer:
		return formatString(L"%d",intValue);
	case OperatorType::Float:
		return formatString(L"%g",floatValue);
	case OperatorType::Identifier:
		return strValue;
	case OperatorType::String:
		return escapeString(strValue);
	case OperatorType::MemoryPos:
		return L".";
	case OperatorType::Add:
		return formatString(L"(%s + %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Sub:
		return formatString(L"(%s - %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Mult:
		return formatString(L"(%s * %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Div:
		return formatString(L"(%s / %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Mod:
		return formatString(L"(%s %% %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Neg:
		return formatString(L"(-%s)",children[0]->toString());
	case OperatorType::LogNot:
		return formatString(L"(!%s)",children[0]->toString());
	case OperatorType::BitNot:
		return formatString(L"(~%s)",children[0]->toString());
	case OperatorType::LeftShift:
		return formatString(L"(%s << %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::RightShift:
		return formatString(L"(%s >> %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Less:
		return formatString(L"(%s < %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Greater:
		return formatString(L"(%s > %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::LessEqual:
		return formatString(L"(%s <= %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::GreaterEqual:
		return formatString(L"(%s >= %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Equal:
		return formatString(L"(%s == %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::NotEqual:
		return formatString(L"(%s != %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::BitAnd:
		return formatString(L"(%s & %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::BitOr:
		return formatString(L"(%s | %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::LogAnd:
		return formatString(L"(%s && %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::LogOr:
		return formatString(L"(%s || %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::Xor:
		return formatString(L"(%s ^ %s)",children[0]->toString(),children[1]->toString());
	case OperatorType::TertiaryIf:
		return formatString(L"(%s ? %s : %s)",children[0]->toString(),children[1]->toString(),children[2]->toString());
	case OperatorType::ToString:
		return formatString(L"(%c%s)",L'\U000000B0',children[0]->toString());
	case OperatorType::FunctionCall:
		return formatFunctionCall();
	default:
		return L"";
	}
}

Expression::Expression()
{
	expression = NULL;
	constExpression = true;
}

void Expression::setExpression(ExpressionInternal* exp, bool inUnknownOrFalseBlock)
{
	expression = std::shared_ptr<ExpressionInternal>(exp);
	if (exp != nullptr)
		constExpression = expression->simplify(inUnknownOrFalseBlock);
	else
		constExpression = true;
}

ExpressionValue Expression::evaluate()
{
	if (expression == NULL)
	{
		ExpressionValue invalid;
		return invalid;
	}

	return expression->evaluate();
}

void Expression::replaceMemoryPos(const std::wstring& identifierName)
{
	if (expression != NULL)
		expression->replaceMemoryPos(identifierName);
}

Expression createConstExpression(int64_t value)
{
	Expression exp;
	ExpressionInternal* num = new ExpressionInternal(value);
	exp.setExpression(num,false);
	return exp;
}
