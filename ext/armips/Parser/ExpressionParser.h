#pragma once
#include "Tokenizer.h"
#include "Core/Expression.h"

Expression parseExpression(Tokenizer& tokenizer, bool inUnknownOrFalseBlock);
void allowFunctionCallExpression(bool allow);
