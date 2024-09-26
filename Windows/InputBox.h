#pragma once

#include <string>
#include "Common/CommonWindows.h"
#include "Common/Common.h"

enum class InputBoxFlags {
	Default = 0,
	Selected = 1,
	PasswordMasking = 2,
};
ENUM_CLASS_BITOPS(InputBoxFlags);

// All I/O is in UTF-8
bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultvalue, std::string &outvalue, InputBoxFlags flags = InputBoxFlags::Default);
bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, const wchar_t *title, u32 defaultvalue, u32 &outvalue);

bool UserPasswordBox_GetStrings(HINSTANCE hInst, HWND hParent, const wchar_t *title, std::string *username, std::string *password);
