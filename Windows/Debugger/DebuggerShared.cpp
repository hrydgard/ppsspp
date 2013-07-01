#include "DebuggerShared.h"
#include "../InputBox.h"

bool parseExpression(char* exp, DebugInterface* cpu, u32& dest)
{
	PostfixExpression postfix;
	if (cpu->initExpression(exp,postfix) == false) return false;
	return cpu->parseExpression(postfix,dest);
}

void displayExpressionError(HWND hwnd)
{
	MessageBox(hwnd,getExpressionError(),"Invalid expression",MB_OK);
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