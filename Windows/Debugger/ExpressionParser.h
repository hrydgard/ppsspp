#pragma once

#include "../../Common/CommonTypes.h"
class DebugInterface;

bool executeExpressionWindow(HWND hwnd, DebugInterface* cpu, u32& dest);
void displayExpressionError(HWND hwnd);
bool parseExpression(char* exp, DebugInterface* cpu, u32& dest);