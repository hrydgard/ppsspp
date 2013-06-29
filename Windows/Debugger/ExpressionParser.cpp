#include "ExpressionParser.h"
#include "../../Core/MIPS/MIPSDebugInterface.h"
#include "../InputBox.h"

typedef enum {
	EXOP_BRACKETL, EXOP_BRACKETR, EXOP_SIGNPLUS, EXOP_SIGNMINUS,
	EXOP_BITNOT, EXOP_LOGNOT, EXOP_MUL, EXOP_DIV, EXOP_MOD, EXOP_ADD, EXOP_SUB,
	EXOP_SHL, EXOP_SHR, EXOP_GREATEREQUAL, EXOP_GREATER, EXOP_LOWEREQUAL, EXOP_LOWER,
	EXOP_EQUAL, EXOP_NOTEQUAL, EXOP_BITAND, EXOP_XOR, EXOP_BITOR, EXOP_LOGAND,
	EXOP_LOGOR, EXOP_TERTIF, EXOP_TERTELSE, EXOP_NUMBER, EXOP_NONE, EXOP_COUNT
} ExpressionOpcodeType;

typedef enum { EXCOMM_CONST, EXCOMM_OP } ExpressionCommand;

typedef std::pair<ExpressionCommand,u32> ExpressionPair;

static char expressionError[256];

typedef struct {
	char Name[4];
	unsigned char Priority;
	unsigned char len;
	unsigned char args;
	bool sign;
} ExpressionOpcode;

const ExpressionOpcode ExpressionOpcodes[] = {
	{ "(",	15,	1,	0,	false },	// EXOP_BRACKETL
	{ ")",	15,	1,	0,	false },	// EXOP_BRACKETR
	{ "+",	12,	1,	1,	true  },	// EXOP_SIGNPLUS
	{ "-",	12,	1,	1,	true  },	// EXOP_SIGNMINUS
	{ "~",	12,	1,	1,	false },	// EXOP_BITNOT
	{ "!",	12,	1,	1,	false },	// EXOP_LOGNOT
	{ "*",	11,	1,	2,	false },	// EXOP_MUL
	{ "/",	11,	1,	2,	false },	// EXOP_DIV
	{ "%",	11,	1,	2,	false },	// EXOP_MOD
	{ "+",	10,	1,	2,	false },	// EXOP_ADD
	{ "-",	10,	1,	2,	false },	// EXOP_SUB
	{ "<<",	9,	2,	2,	false },	// EXOP_SHL
	{ ">>",	9,	2,	2,	false },	// EXOP_SHR
	{ ">=",	8,	2,	2,	false },	// EXOP_GREATEREQUAL
	{ ">",	8,	1,	2,	false },	// EXOP_GREATER
	{ "<=",	8,	2,	2,	false },	// EXOP_LOWEREQUAL
	{ "<",	8,	1,	2,	false },	// EXOP_LOWER
	{ "==",	7,	2,	2,	false },	// EXOP_EQUAL
	{ "!=",	7,	2,	2,	false },	// EXOP_NOTEQUAL
	{ "&",	6,	1,	2,	false },	// EXOP_BITAND
	{ "^",	5,	1,	2,	false },	// EXOP_XOR
	{ "|",	4,	1,	2,	false },	// EXOP_BITOR
	{ "&&",	3,	2,	2,	false },	// EXOP_LOGAND
	{ "||",	2,	2,	2,	false },	// EXOP_LOGOR
	{ "?",	0,	1,	0,	false },	// EXOP_TERTIF
	{ ":",	1,	1,	3,	false },	// EXOP_TERTELSE
	{ "",	0,	0,	0,	false },	// EXOP_NUMBER
	{ "",	0,	0,	0,	false }		// EXOP_NONE
};

bool parseNumber(char* str, int defaultrad, int len, u32& result)
{
	int val = 0;
	int r = 0;
	if (len == 0) len = (int) strlen(str);

	if (str[0] == '0' && tolower(str[1]) == 'x')
	{
		r = 16;
		str+=2;
		len-=2;
	} else if (str[0] == '$')
	{
		r = 16;
		str++;
		len--;
	} else if (str[0] == '0' && tolower(str[1]) == 'o')
	{
		r = 8;
		str+=2;
		len-=2;
	} else {
		if (!(str[0] >= '0' && str[0] <= '9')) return false;

		if (tolower(str[len-1]) == 'b')
		{
			r = 2;
			len--;
		} else if (tolower(str[len-1]) == 'o')
		{
			r = 8;
			len--;
		} else if (tolower(str[len-1]) == 'h')
		{
			r = 16;
			len--;
		} else {
			r = defaultrad;
		}
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

bool parseLabel(char* label, DebugInterface* debug, u32& dest)
{
	// check if it's a register first
	for (int i = 0; i < 32; i++)
	{
		char reg[8];
		sprintf(reg,"r%d",i);

		if (stricmp(label,reg) == 0
			|| stricmp(label,debug->GetRegName(0,i)) == 0)
		{
			dest = debug->GetRegValue(0,i);
			return true;
		}
	}

	if (stricmp(label,"pc") == 0)
	{
		dest = debug->GetPC();
		return true;
	}

	// now check labels
	return debug->getSymbolValue(label,dest);
}

ExpressionOpcodeType getExpressionOpcode(char* str, int& ReturnLen, ExpressionOpcodeType LastOpcode)
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
		c == '@' || c == '_' || c == '$')
	{
		return true;
	} else {
		return false;
	}
}

bool parseExpression(char* infix, DebugInterface* cpu, u32& dest)
{
	expressionError[0] = 0;

	int infixPos = 0;
	int infixLen = strlen(infix);
	ExpressionOpcodeType lastOpcode = EXOP_NONE;

	std::vector<ExpressionPair> postfixStack;
	std::vector<ExpressionOpcodeType> opcodeStack;

	while (infixPos < infixLen)
	{
		char first = tolower(infix[infixPos]);
		char subStr[12];
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

			u32 value;
			if (parseNumber(subStr,16,subPos,value) == false)
			{
				sprintf(expressionError,"Invalid number \"%s\"",subStr);
				return false;
			}

			postfixStack.push_back(ExpressionPair(EXCOMM_CONST,value));
			lastOpcode = EXOP_NUMBER;
		} else if ((first >= 'a' && first <= 'z') || first == '@')
		{
			while (isAlphaNum(infix[infixPos]))
			{
				subStr[subPos++] = infix[infixPos++];
			}
			subStr[subPos] = 0;

			u32 value;
			if (parseLabel(subStr,cpu,value) == false)
			{
				sprintf(expressionError,"Invalid label \"%s\"",subStr);
				return false;
			}

			postfixStack.push_back(ExpressionPair(EXCOMM_CONST,value));
			lastOpcode = EXOP_NUMBER;
		} else {
			int len;
			ExpressionOpcodeType type = getExpressionOpcode(&infix[infixPos],len,lastOpcode);
			if (type == EXOP_NONE)
			{
				sprintf(expressionError,"Invalid operator at \"%s\"",&infix[infixPos]);
				return false;
			}

			switch (type)
			{
			case EXOP_BRACKETL:
				opcodeStack.push_back(type);
				break;
			case EXOP_BRACKETR:
				while (true)
				{
					if (opcodeStack.empty())
					{		
						sprintf(expressionError,"Closing parenthesis without opening one");
						return false;
					}
					ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
					opcodeStack.pop_back();
					if (t == EXOP_BRACKETL) break;
					postfixStack.push_back(ExpressionPair(EXCOMM_OP,t));
				}
				break;
			default:
				if (opcodeStack.empty() == false)
				{
					int CurrentPriority = ExpressionOpcodes[type].Priority;
					while (!opcodeStack.empty())
					{
						ExpressionOpcodeType t = opcodeStack[opcodeStack.size()-1];
						opcodeStack.pop_back();

						if (t == EXOP_BRACKETL)
						{
							opcodeStack.push_back(t);
							break;
						}

						if (ExpressionOpcodes[t].Priority >= CurrentPriority)
						{
							postfixStack.push_back(ExpressionPair(EXCOMM_OP,t));
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
			sprintf(expressionError,"Parenthesis not closed");
			return false;
		}
		postfixStack.push_back(ExpressionPair(EXCOMM_OP,t));
	}


	// parse postfix now
	int num = 0;
	u32 opcode;
	std::vector<u32> valueStack;
	unsigned int arg[5];

	while (num < postfixStack.size())
	{
		switch (postfixStack[num].first)
		{
		case EXCOMM_CONST:	// konstante zahl
			valueStack.push_back(postfixStack[num++].second);
			break;
		case EXCOMM_OP:	// opcode
			opcode = postfixStack[num++].second;
			if (valueStack.size() < ExpressionOpcodes[opcode].args)
			{
				sprintf(expressionError,"Not enough arguments");
				return false;
			}
			for (int l = 0; l < ExpressionOpcodes[opcode].args; l++)
			{
				arg[l] = valueStack[valueStack.size()-1];
				valueStack.pop_back();
			}

			switch (opcode)
			{
			case EXOP_SIGNPLUS:		// keine aktion nötig
				break;
			case EXOP_SIGNMINUS:	// -0
				valueStack.push_back(0-arg[0]);
				break;
			case EXOP_BITNOT:			// ~b
				valueStack.push_back(~arg[0]);
				break;
			case EXOP_LOGNOT:			// !b
				valueStack.push_back(!arg[0]);
				break;
			case EXOP_MUL:			// a*b
				valueStack.push_back(arg[1]*arg[0]);
				break;
			case EXOP_DIV:			// a/b
				if (arg[0] == 0)
				{
					sprintf(expressionError,"Division by zero");
					return false;
				}
				valueStack.push_back(arg[1]/arg[0]);
				break;
			case EXOP_MOD:			// a%b
				if (arg[0] == 0)
				{
					sprintf(expressionError,"Modulo by zero");
					return false;
				}
				valueStack.push_back(arg[1]%arg[0]);
				break;
			case EXOP_ADD:			// a+b
				valueStack.push_back(arg[1]+arg[0]);
				break;
			case EXOP_SUB:			// a-b
				valueStack.push_back(arg[1]-arg[0]);
				break;
			case EXOP_SHL:			// a<<b
				valueStack.push_back(arg[1]<<arg[0]);
				break;
			case EXOP_SHR:			// a>>b
				valueStack.push_back(arg[1]>>arg[0]);
				break;
			case EXOP_GREATEREQUAL:		// a >= b
				valueStack.push_back(arg[1]>=arg[0]);
				break;
			case EXOP_GREATER:			// a > b
				valueStack.push_back(arg[1]>arg[0]);
				break;
			case EXOP_LOWEREQUAL:		// a <= b
				valueStack.push_back(arg[1]<=arg[0]);
				break;
			case EXOP_LOWER:			// a < b
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
			case EXOP_LOGOR:			// a && b
				valueStack.push_back(arg[1]||arg[0]);
				break;
			case EXOP_TERTIF:			// darf so nicht vorkommen
				return false;
			case EXOP_TERTELSE:			// exp ? exp : exp, else muss zuerst kommen!
				if (postfixStack[num++].second != EXOP_TERTIF)
				{
					sprintf(expressionError,"Invalid tertiary operator");
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

void displayExpressionError(HWND hwnd)
{
	if (expressionError[0] == 0)
	{	
		MessageBox(hwnd,"Invalid expression","Invalid expression",MB_OK);
	} else {
		MessageBox(hwnd,expressionError,"Invalid expression",MB_OK);
	}
}

bool executeExpressionWindow(HWND hwnd, DebugInterface* cpu, u32& dest)
{
	char expression[1024];
	if (InputBox_GetString(GetModuleHandle(NULL), hwnd, "Expression", "",expression) == false)
	{
		return false;
	}

	if (parseExpression(expression,cpu,dest) == false)
	{
		displayExpressionError(hwnd);
		return false;
	}

	return true;
}