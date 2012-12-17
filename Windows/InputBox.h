#pragma once

#include "../Globals.h"

#include <windows.h>

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, TCHAR *defaultvalue, TCHAR *outvalue);
bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, TCHAR *title, u32 defaultvalue, u32 &outvalue);
