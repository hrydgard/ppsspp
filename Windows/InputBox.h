#pragma once

#include <string>
#include "Common/CommonWindows.h"


// All I/O is in UTF-8
bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultvalue, std::string &outvalue, bool selected);
bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultvalue, std::string &outvalue);
bool InputBox_GetWString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue);
bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, const wchar_t *title, u32 defaultvalue, u32 &outvalue);
