#pragma once

#include "Globals.h"

#include "Common/CommonWindows.h"
bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, TCHAR *defaultvalue, TCHAR *outvalue);
bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, TCHAR *defaultvalue, TCHAR *outvalue, size_t outlength);
bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, TCHAR *title, u32 defaultvalue, u32 &outvalue);
