#include "Common/Data/Encoding/Utf8.h"

#include "DebuggerShared.h"
#include "../InputBox.h"

bool parseExpression(const char* exp, DebugInterface* cpu, u32& dest)
{
	PostfixExpression postfix;
	if (cpu->initExpression(exp,postfix) == false) return false;
	return cpu->parseExpression(postfix,dest);
}

void displayExpressionError(HWND hwnd)
{
	MessageBox(hwnd,ConvertUTF8ToWString(getExpressionError()).c_str(),L"Invalid expression",MB_OK);
}

bool executeExpressionWindow(HWND hwnd, DebugInterface* cpu, u32& dest)
{
	std::string expression;
	if (InputBox_GetString(GetModuleHandle(NULL), hwnd, L"Expression", "", expression) == false)
	{
		return false;
	}

	if (parseExpression(expression.c_str(), cpu, dest) == false)
	{
		displayExpressionError(hwnd);
		return false;
	}

	return true;
}
