#include "stdafx.h"
#include "ExpressionParser.h"

static ExpressionInternal* expression(Tokenizer& tokenizer);

static bool allowFunctionCall = true;

void allowFunctionCallExpression(bool allow)
{
	allowFunctionCall = allow;
}

static ExpressionInternal* primaryExpression(Tokenizer& tokenizer)
{
	const Token &tok = tokenizer.peekToken();

	if (tok.type == TokenType::Invalid)
		return NULL;

	switch (tok.type)
	{
	case TokenType::Float:
		tokenizer.eatToken();
		return new ExpressionInternal(tok.floatValue);
	case TokenType::Identifier:
		{
			const std::wstring stringValue = tok.getStringValue();
			tokenizer.eatToken();
			if (stringValue == L".")
				return new ExpressionInternal(OperatorType::MemoryPos);
			else
				return new ExpressionInternal(stringValue,OperatorType::Identifier);
		}
	case TokenType::String:
		tokenizer.eatToken();
		return new ExpressionInternal(tok.getStringValue(),OperatorType::String);
	case TokenType::Integer:
		tokenizer.eatToken();
		return new ExpressionInternal(tok.intValue);
	case TokenType::LParen:
		tokenizer.eatToken();
		ExpressionInternal* exp = expression(tokenizer);
			
		if (tokenizer.nextToken().type != TokenType::RParen)
		{
			delete exp;
			return nullptr;
		}

		return exp;
	}

	return nullptr;
}

static ExpressionInternal* postfixExpression(Tokenizer& tokenizer)
{
	if (allowFunctionCall &&
		tokenizer.peekToken(0).type == TokenType::Identifier &&
		tokenizer.peekToken(1).type == TokenType::LParen)
	{
		const std::wstring functionName = tokenizer.nextToken().getStringValue();
		tokenizer.eatToken();

		std::vector<ExpressionInternal*> parameters;
		while (tokenizer.peekToken().type != TokenType::RParen)
		{
			if (parameters.size() != 0 && tokenizer.nextToken().type != TokenType::Comma)
			{
				for (ExpressionInternal* exp: parameters)
					delete exp;
				return nullptr;
			}

			ExpressionInternal* exp = expression(tokenizer);
			if (exp == nullptr)
			{
				for (ExpressionInternal* exp: parameters)
					delete exp;
				return nullptr;
			}

			parameters.push_back(exp);
		}

		tokenizer.eatToken();

		return new ExpressionInternal(functionName,parameters);
	}

	return primaryExpression(tokenizer);
}

static ExpressionInternal* unaryExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = postfixExpression(tokenizer);
	if (exp != nullptr)
		return exp;

	const TokenType opType = tokenizer.nextToken().type;
	exp = postfixExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	switch (opType)
	{
	case TokenType::Plus:
		return exp;
	case TokenType::Minus:
		return new ExpressionInternal(OperatorType::Neg,exp);
	case TokenType::Tilde:
		return new ExpressionInternal(OperatorType::BitNot,exp);
	case TokenType::Exclamation:
		return new ExpressionInternal(OperatorType::LogNot,exp);
	case TokenType::Degree:
		return new ExpressionInternal(OperatorType::ToString,exp);
	default:
		delete exp;
		return nullptr;
	}
}

static ExpressionInternal* multiplicativeExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = unaryExpression(tokenizer);
	if (exp ==  nullptr)
		return nullptr;

	while (true)
	{
		OperatorType op = OperatorType::Invalid;
		switch (tokenizer.peekToken().type)
		{
		case TokenType::Mult:
			op = OperatorType::Mult;
			break;
		case TokenType::Div:
			op = OperatorType::Div;
			break;
		case TokenType::Mod:
			op = OperatorType::Mod;
			break;
		}

		if (op == OperatorType::Invalid)
			break;

		tokenizer.eatToken();

		ExpressionInternal* exp2 = unaryExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(op,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* additiveExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = multiplicativeExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (true)
	{
		OperatorType op = OperatorType::Invalid;
		switch (tokenizer.peekToken().type)
		{
		case TokenType::Plus:
			op = OperatorType::Add;
			break;
		case TokenType::Minus:
			op = OperatorType::Sub;
			break;
		}

		if (op == OperatorType::Invalid)
			break;

		tokenizer.eatToken();

		ExpressionInternal* exp2 = multiplicativeExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(op,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* shiftExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = additiveExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (true)
	{
		OperatorType op = OperatorType::Invalid;
		switch (tokenizer.peekToken().type)
		{
		case TokenType::LeftShift:
			op = OperatorType::LeftShift;
			break;
		case TokenType::RightShift:
			op = OperatorType::RightShift;
			break;
		}

		if (op == OperatorType::Invalid)
			break;

		tokenizer.eatToken();

		ExpressionInternal* exp2 = additiveExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(op,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* relationalExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = shiftExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (true)
	{
		OperatorType op = OperatorType::Invalid;
		switch (tokenizer.peekToken().type)
		{
		case TokenType::Less:
			op = OperatorType::Less;
			break;
		case TokenType::LessEqual:
			op = OperatorType::LessEqual;
			break;
		case TokenType::Greater:
			op = OperatorType::Greater;
			break;
		case TokenType::GreaterEqual:
			op = OperatorType::GreaterEqual;
			break;
		}

		if (op == OperatorType::Invalid)
			break;

		tokenizer.eatToken();

		ExpressionInternal* exp2 = shiftExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(op,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* equalityExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = relationalExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (true)
	{
		OperatorType op = OperatorType::Invalid;
		switch (tokenizer.peekToken().type)
		{
		case TokenType::Equal:
			op = OperatorType::Equal;
			break;
		case TokenType::NotEqual:
			op = OperatorType::NotEqual;
			break;
		}

		if (op == OperatorType::Invalid)
			break;

		tokenizer.eatToken();

		ExpressionInternal* exp2 = relationalExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(op,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* andExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = equalityExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (tokenizer.peekToken().type == TokenType::BitAnd)
	{
		tokenizer.eatToken();

		ExpressionInternal* exp2 = equalityExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(OperatorType::BitAnd,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* exclusiveOrExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = andExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (tokenizer.peekToken().type == TokenType::Caret)
	{
		tokenizer.eatToken();

		ExpressionInternal* exp2 = andExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(OperatorType::Xor,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* inclusiveOrExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = exclusiveOrExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (tokenizer.peekToken().type == TokenType::BitOr)
	{
		tokenizer.eatToken();

		ExpressionInternal* exp2 = exclusiveOrExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(OperatorType::BitOr,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* logicalAndExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = inclusiveOrExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (tokenizer.peekToken().type == TokenType::LogAnd)
	{
		tokenizer.eatToken();

		ExpressionInternal* exp2 = inclusiveOrExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(OperatorType::LogAnd,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* logicalOrExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = logicalAndExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	while (tokenizer.peekToken().type == TokenType::LogOr)
	{
		tokenizer.eatToken();

		ExpressionInternal* exp2 = logicalAndExpression(tokenizer);
		if (exp2 == nullptr)
		{
			delete exp;
			return nullptr;
		}

		exp = new ExpressionInternal(OperatorType::LogOr,exp,exp2);
	}

	return exp;
}

static ExpressionInternal* conditionalExpression(Tokenizer& tokenizer)
{
	ExpressionInternal* exp = logicalOrExpression(tokenizer);
	if (exp == nullptr)
		return nullptr;

	// check a ? b : c
	if (tokenizer.peekToken().type != TokenType::Question)
		return exp;

	tokenizer.eatToken();
	ExpressionInternal* second = expression(tokenizer);

	if (second != nullptr && tokenizer.nextToken().type == TokenType::Colon)
	{
		ExpressionInternal* third = expression(tokenizer);
		if (third != nullptr)
			return new ExpressionInternal(OperatorType::TertiaryIf,exp,second,third);

		delete third;
	}

	delete second;
	delete exp;
	return nullptr;
}

static ExpressionInternal* expression(Tokenizer& tokenizer)
{
	return conditionalExpression(tokenizer);
}

Expression parseExpression(Tokenizer& tokenizer, bool inUnknownOrFalseBlock)
{
	TokenizerPosition pos = tokenizer.getPosition();

	// parse expression, revert tokenizer to previous position
	// if it failed
	ExpressionInternal* exp = expression(tokenizer);
	if (exp == nullptr)
		tokenizer.setPosition(pos);

	Expression result;
	result.setExpression(exp, inUnknownOrFalseBlock);
	return result;
}
