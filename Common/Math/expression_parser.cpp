#include "Common/StringUtils.h"
#include "expression_parser.h"
#include <ctype.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

typedef enum {
	EXOP_BRACKETL, EXOP_BRACKETR, EXOP_MEML, EXOP_MEMR, EXOP_MEMSIZE, EXOP_SIGNPLUS, EXOP_SIGNMINUS,
	EXOP_BITNOT, EXOP_LOGNOT, EXOP_MUL, EXOP_DIV, EXOP_MOD, EXOP_ADD, EXOP_SUB,
	EXOP_SHL, EXOP_SHR, EXOP_GREATEREQUAL, EXOP_GREATER, EXOP_LOWEREQUAL, EXOP_LOWER,
	EXOP_EQUAL, EXOP_NOTEQUAL, EXOP_BITAND, EXOP_XOR, EXOP_BITOR, EXOP_LOGAND,
	EXOP_LOGOR, EXOP_TERTIF, EXOP_TERTELSE, EXOP_NUMBER, EXOP_MEM, EXOP_NONE, EXOP_COUNT
} ExpressionOpcodeType;

typedef enum { EXCOMM_CONST, EXCOMM_CONST_FLOAT, EXCOMM_REF, EXCOMM_OP } ExpressionCommand;

static std::string expressionError;

typedef struct {
	char Name[4];
	unsigned char Priority;
	unsigned char len;
	unsigned char args;
	bool sign;
} ExpressionOpcode;

const ExpressionOpcode ExpressionOpcodes[] = {
	{ "(",	25,	1,	0,	false },	// EXOP_BRACKETL
	{ ")",	25,	1,	0,	false },	// EXOP_BRACKETR
	{ "[",	4,	1,	0,	false },	// EXOP_MEML
	{ "]",	4,	1,	0,	false },	// EXOP_MEMR
	{ ",",	5,	1,	2,	false },	// EXOP_MEMSIZE
	{ "+",	22,	1,	1,	true  },	// EXOP_SIGNPLUS
	{ "-",	22,	1,	1,	true  },	// EXOP_SIGNMINUS
	{ "~",	22,	1,	1,	false },	// EXOP_BITNOT
	{ "!",	22,	1,	1,	false },	// EXOP_LOGNOT
	{ "*",	21,	1,	2,	false },	// EXOP_MUL
	{ "/",	21,	1,	2,	false },	// EXOP_DIV
	{ "%",	21,	1,	2,	false },	// EXOP_MOD
	{ "+",	20,	1,	2,	false },	// EXOP_ADD
	{ "-",	20,	1,	2,	false },	// EXOP_SUB
	{ "<<",	19,	2,	2,	false },	// EXOP_SHL
	{ ">>",	19,	2,	2,	false },	// EXOP_SHR
	{ ">=",	18,	2,	2,	false },	// EXOP_GREATEREQUAL
	{ ">",	18,	1,	2,	false },	// EXOP_GREATER
	{ "<=",	18,	2,	2,	false },	// EXOP_LOWEREQUAL
	{ "<",	18,	1,	2,	false },	// EXOP_LOWER
	{ "==",	17,	2,	2,	false },	// EXOP_EQUAL
	{ "!=",	17,	2,	2,	false },	// EXOP_NOTEQUAL
	{ "&",	16,	1,	2,	false },	// EXOP_BITAND
	{ "^",	15,	1,	2,	false },	// EXOP_XOR
	{ "|",	14,	1,	2,	false },	// EXOP_BITOR
	{ "&&",	13,	2,	2,	false },	// EXOP_LOGAND
	{ "||",	12,	2,	2,	false },	// EXOP_LOGOR
	{ "?",	10,	1,	0,	false },	// EXOP_TERTIF
	{ ":",	11,	1,	3,	false },	// EXOP_TERTELSE
	{ "",	0,	0,	0,	false },	// EXOP_NUMBER
	{ "[]",	0,	0,	1,	false },	// EXOP_MEM
	{ "",	0,	0,	0,	false }		// EXOP_NONE
};

static int radixFromZeroPrefix(char c) {
	switch (tolower(c)) {
	case 'b': return 2;
	case 'o': return 8;
	case 'x': return 16;
	// Inventing a prefix since we default to hex.
	case 'd': return 10;
	}

	return -1;
}

static int radixFromSuffix(char c, int defaultrad) {
	switch (tolower(c)) {
	case 'o': return 8;
	case 'h': return 16;
	case 'i': return 10;
	case 'u': return 10;
	case 'b': return defaultrad == 16 ? -1 : 2;
	}

	return -1;
}

bool parseNumber(char *str, int defaultrad, int len, uint32_t &result) {
	int val = 0;
	int r = 0;
	if (len == 0)
		len = (int)strlen(str);

	if (str[0] == '0' && radixFromZeroPrefix(str[1]) != -1) {
		r = radixFromZeroPrefix(str[1]);
		str += 2;
		len -= 2;
	} else if (str[0] == '$') {
		r = 16;
		str++;
		len--;
	} else if (str[0] >= '0' && str[0] <= '9') {
		int suffix = radixFromSuffix(str[len - 1], defaultrad);
		if (suffix != -1) {
			r = suffix;
			len--;
		} else {
			r = defaultrad;
		}
	} else {
		return false;
	}

	switch (r)
	{
	case 2: // bin
		while (len--)
		{
			if (*str != '0' && *str != '1') return false;
			val = val << 1;
			if (*str++ == '1')
			{
				val++;
			}
		}
		break;
	case 8: // oct
		while (len--)
		{
			if (*str < '0' || *str > '7') return false;
			val = val << 3;
			val+=(*str++-'0');
		}
		break;
	case 10: // dec
		while (len--)
		{
			if (*str < '0' || *str > '9') return false;
			val = val * 10;
			val += (*str++ - '0');
		}
		break;
	case 16: // hex
		while (len--)
		{
			char c = tolower(*str++);
			if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return false;
			val = val << 4;

			if (c >= 'a') val += c-'a'+10;
			else val += c-'0';
		}
		break;
	default:
		return false;
	}

	result = val;
	return true;
}

// Parse only a float, and return as float bits.
static bool parseFloat(const char *str, int len, uint32_t &result)
{
	bool foundDecimal = false;
	for (int i = 0; i < len; ++i)
	{
		if (str[i] == '.')
		{
			if (foundDecimal)
				return false;
			foundDecimal = true;
			continue;
		}
		if (str[i] < '0' || str[i] > '9')
			return false;
	}

	float f = (float)atof(str);
	memcpy(&result, &f, sizeof(result));
	return foundDecimal;
}

ExpressionOpcodeType getExpressionOpcode(const char* str, int& ReturnLen, ExpressionOpcodeType LastOpcode)
{
	int longestlen = 0;
	ExpressionOpcodeType result = EXOP_NONE;

	for (int i = 0; i < EXOP_NUMBER; i++)
	{
		if (ExpressionOpcodes[i].sign == true &&
			(LastOpcode == EXOP_NUMBER || LastOpcode == EXOP_BRACKETR)) continue;

		int len = ExpressionOpcodes[i].len;
		if (len > longestlen)
		{
			if (strncmp(ExpressionOpcodes[i].Name,str,len) == 0)
			{
				result = (ExpressionOpcodeType) i;
				longestlen = len;
			}
		}
	}

	ReturnLen = longestlen;
	return result;
}

bool isAlphaNum(char c)
{
	if ((c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') ||
		c == '@' || c == '_' || c == '$' || c == '.')
	{
		return true;
	} else {
		return false;
	}
}

bool initPostfixExpression(const char* infix, IExpressionFunctions* funcs, PostfixExpression& dest)
{
	expressionError.clear();

	int infixPos = 0;
	int infixLen = (int)strlen(infix);
	ExpressionOpcodeType lastOpcode = EXOP_NONE;
	std::vector<ExpressionOpcodeType> opcodeStack;
	dest.clear();

	while (infixPos < infixLen)
	{
		char first = tolower(infix[infixPos]);
		char subStr[256];
		int subPos = 0;

		if (first == ' ' || first == '\t')
		{
			infixPos++;
			continue;
		}

		if (first >= '0' && first <= '9')
		{
			while (isAlphaNum(infix[infixPos]))
			{
				subStr[subPos++] = infix[infixPos++];
			}
			subStr[subPos] = 0;

			uint32_t value;
			bool isFloat = false;
			if (parseFloat(subStr,subPos,value) == true)
				isFloat = true;
			else if (parseNumber(subStr,16,subPos,value) == false)
			{
				expressionError = StringFromFormat("Invalid number \"%s\"", subStr);
				return false;
			}

			dest.emplace_back(isFloat?EXCOMM_CONST_FLOAT:EXCOMM_CONST,value);
			lastOpcode = EXOP_NUMBER;
		} else if ((first >= 'a' && first <= 'z') || first == '@')
		{
			while (isAlphaNum(infix[infixPos]))
			{
				subStr[subPos++] = infix[infixPos++];
			}
			subStr[subPos] = 0;

			uint32_t value;
			if (funcs->parseReference(subStr,value) == true)
			{
				dest.emplace_back(EXCOMM_REF,value);
				lastOpcode = EXOP_NUMBER;
				continue;
			}

			if (funcs->parseSymbol(subStr,value) == true)
			{
				dest.emplace_back(EXCOMM_CONST,value);
				lastOpcode = EXOP_NUMBER;
				continue;
			}

			expressionError = StringFromFormat("Invalid symbol \"%s\"", subStr);
			return false;
		} else {
			int len;
			ExpressionOpcodeType type = getExpressionOpcode(&infix[infixPos],len,lastOpcode);
			if (type == EXOP_NONE)
			{
				expressionError = StringFromFormat("Invalid operator at \"%s\"", &infix[infixPos]);
				return false;
			}

			switch (type)
			{
			case EXOP_BRACKETL:
			case EXOP_MEML:
				opcodeStack.push_back(type);
				break;
			case EXOP_BRACKETR:
				while (true)
				{
					if (opcodeStack.empty())
					{		
						expressionError = "Closing parenthesis without opening one";
						return false;
					}
					ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
					opcodeStack.pop_back();
					if (t == EXOP_BRACKETL) break;
					dest.emplace_back(EXCOMM_OP,t);
				}
				break;
			case EXOP_MEMR:
				while (true)
				{
					if (opcodeStack.empty())
					{		
						expressionError = "Closing bracket without opening one";
						return false;
					}
					ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
					opcodeStack.pop_back();
					if (t == EXOP_MEML)
					{
						dest.emplace_back(EXCOMM_OP,EXOP_MEM);
						break;
					}
					dest.emplace_back(EXCOMM_OP,t);
				}
				type = EXOP_NUMBER;
				break;
			default:
				if (opcodeStack.empty() == false)
				{
					int CurrentPriority = ExpressionOpcodes[type].Priority;
					while (!opcodeStack.empty())
					{
						ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
						opcodeStack.pop_back();

						if (t == EXOP_BRACKETL || t == EXOP_MEML)
						{
							opcodeStack.push_back(t);
							break;
						}

						if (ExpressionOpcodes[t].Priority >= CurrentPriority)
						{
							dest.emplace_back(EXCOMM_OP,t);
						} else {
							opcodeStack.push_back(t);
							break;
						}
					}
				}
				opcodeStack.push_back(type);
				break;
			}
			infixPos += len;
			lastOpcode = type;
		}
	}

	while (!opcodeStack.empty())
	{
		ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
		opcodeStack.pop_back();

		if (t == EXOP_BRACKETL)	// opening bracket without closing one
		{
			expressionError = "Parenthesis not closed";
			return false;
		}
		dest.emplace_back(EXCOMM_OP,t);
	}

#if 0			// only for testing
	char test[1024];
	int testPos = 0;
	for (int i = 0; i < dest.size(); i++)
	{
		switch (dest[i].first)
		{
		case EXCOMM_CONST:
		case EXCOMM_CONST_FLOAT:
			testPos += snprintf(&test[testPos], sizeof(test) - testPos, "0x%04X ", dest[i].second);
			break;
		case EXCOMM_REF:
			testPos += snprintf(&test[testPos], sizeof(test) - testPos, "r%d ", dest[i].second);
			break;
		case EXCOMM_OP:
			testPos += snprintf(&test[testPos], sizeof(test) - testPos, "%s ", ExpressionOpcodes[dest[i].second].Name);
			break;
		};
	}
#endif

	return true;
}

bool parsePostfixExpression(PostfixExpression& exp, IExpressionFunctions* funcs, uint32_t& dest)
{
	size_t num = 0;
	uint32_t opcode;
	std::vector<uint32_t> valueStack;
	unsigned int arg[5]{};
	float fArg[5]{};
	bool useFloat = false;

	while (num < exp.size())
	{
		switch (exp[num].first)
		{
		case EXCOMM_CONST:	// konstante zahl
			valueStack.push_back(exp[num++].second);
			break;
		case EXCOMM_CONST_FLOAT:
			useFloat = true;
			valueStack.push_back(exp[num++].second);
			break;
		case EXCOMM_REF:
			useFloat = useFloat || funcs->getReferenceType(exp[num].second) == EXPR_TYPE_FLOAT;
			opcode = funcs->getReferenceValue(exp[num++].second);
			valueStack.push_back(opcode);
			break;
		case EXCOMM_OP:	// opcode
			opcode = exp[num++].second;
			if (valueStack.size() < ExpressionOpcodes[opcode].args)
			{
				expressionError = "Not enough arguments";
				return false;
			}
			for (int l = 0; l < ExpressionOpcodes[opcode].args; l++)
			{
				arg[l] = valueStack[valueStack.size()-1];
				valueStack.pop_back();
			}
			// In case of float representation.
			memcpy(fArg, arg, sizeof(fArg));

			switch (opcode)
			{
			case EXOP_MEMSIZE:	// must be followed by EXOP_MEM
				if (exp[num++].second != EXOP_MEM)
				{
					expressionError = "Invalid memsize operator";
					return false;
				}

				uint32_t val;
				if (funcs->getMemoryValue(arg[1], arg[0], val, &expressionError) == false)
				{
					return false;
				}
				valueStack.push_back(val);
				break;
			case EXOP_MEM:
				{
					uint32_t val;
					if (funcs->getMemoryValue(arg[0], 4, val, &expressionError) == false)
					{
						return false;
					}
					valueStack.push_back(val);
				}
				break;
			case EXOP_SIGNPLUS:		// keine aktion nötig
				break;
			case EXOP_SIGNMINUS:	// -0
				if (useFloat)
					valueStack.push_back((uint32_t)(0.0f - fArg[0]));
				else
					valueStack.push_back(0-arg[0]);
				break;
			case EXOP_BITNOT:			// ~b
				valueStack.push_back(~arg[0]);
				break;
			case EXOP_LOGNOT:			// !b
				valueStack.push_back(!(arg[0] != 0));
				break;
			case EXOP_MUL:			// a*b
				if (useFloat)
					valueStack.push_back((uint32_t)(fArg[1] * fArg[0]));
				else
					valueStack.push_back(arg[1]*arg[0]);
				break;
			case EXOP_DIV:			// a/b
				if (arg[0] == 0)
				{
					expressionError = "Division by zero";
					return false;
				}
				if (useFloat)
					valueStack.push_back((uint32_t)(fArg[1] / fArg[0]));
				else
					valueStack.push_back(arg[1]/arg[0]);
				break;
			case EXOP_MOD:			// a%b
				if (arg[0] == 0)
				{
					expressionError = "Modulo by zero";
					return false;
				}
				valueStack.push_back(arg[1]%arg[0]);
				break;
			case EXOP_ADD:			// a+b
				if (useFloat)
					valueStack.push_back((uint32_t)(fArg[1] + fArg[0]));
				else
					valueStack.push_back(arg[1]+arg[0]);
				break;
			case EXOP_SUB:			// a-b
				if (useFloat)
					valueStack.push_back((uint32_t)(fArg[1] - fArg[0]));
				else
					valueStack.push_back(arg[1]-arg[0]);
				break;
			case EXOP_SHL:			// a<<b
				valueStack.push_back(arg[1]<<arg[0]);
				break;
			case EXOP_SHR:			// a>>b
				valueStack.push_back(arg[1]>>arg[0]);
				break;
			case EXOP_GREATEREQUAL:		// a >= b
				if (useFloat)
					valueStack.push_back(fArg[1]>=fArg[0]);
				else
					valueStack.push_back(arg[1]>=arg[0]);
				break;
			case EXOP_GREATER:			// a > b
				if (useFloat)
					valueStack.push_back(fArg[1]>fArg[0]);
				else
					valueStack.push_back(arg[1]>arg[0]);
				break;
			case EXOP_LOWEREQUAL:		// a <= b
				if (useFloat)
					valueStack.push_back(fArg[1]<=fArg[0]);
				else
					valueStack.push_back(arg[1]<=arg[0]);
				break;
			case EXOP_LOWER:			// a < b
				if (useFloat)
					valueStack.push_back(fArg[1]<fArg[0]);
				else
					valueStack.push_back(arg[1]<arg[0]);
				break;
			case EXOP_EQUAL:		// a == b
				valueStack.push_back(arg[1]==arg[0]);
				break;
			case EXOP_NOTEQUAL:			// a != b
				valueStack.push_back(arg[1]!=arg[0]);
				break;
			case EXOP_BITAND:			// a&b
				valueStack.push_back(arg[1]&arg[0]);
				break;
			case EXOP_XOR:			// a^b
				valueStack.push_back(arg[1]^arg[0]);
				break;
			case EXOP_BITOR:			// a|b
				valueStack.push_back(arg[1]|arg[0]);
				break;
			case EXOP_LOGAND:			// a && b
				valueStack.push_back(arg[1]&&arg[0]);
				break;
			case EXOP_LOGOR:			// a || b
				valueStack.push_back(arg[1]||arg[0]);
				break;
			case EXOP_TERTIF:			// darf so nicht vorkommen
				return false;
			case EXOP_TERTELSE:			// exp ? exp : exp, else muss zuerst kommen!
				if (exp[num++].second != EXOP_TERTIF)
				{
					expressionError = "Invalid tertiary operator";
					return false;
				}
				valueStack.push_back(arg[2]?arg[1]:arg[0]);
				break;
			}
			break;
		}
	}

	if (valueStack.size() != 1) return false;
	dest = valueStack[0];
	return true;
}

bool parseExpression(const char *exp, IExpressionFunctions *funcs, uint32_t &dest) {
	PostfixExpression postfix;
	if (initPostfixExpression(exp,funcs,postfix) == false) return false;
	return parsePostfixExpression(postfix,funcs,dest);
}

const char *getExpressionError()
{
	if (expressionError.empty())
		expressionError = "Invalid expression";
	return expressionError.c_str();
}
