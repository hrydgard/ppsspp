// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <cstring>
#include <string>

#include "Common/Data/Encoding/Utf8.h"
#include "SysError.h"

#ifdef _WIN32
#include "CommonWindows.h"
#else
#include <errno.h>
#endif

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
std::string GetLastErrorMsg() {
#ifdef _WIN32
	return GetStringErrorMsg(GetLastError());
#else
	return GetStringErrorMsg(errno);
#endif
}

std::string GetStringErrorMsg(int errCode) {
	static const size_t buff_size = 1023;

#ifdef _WIN32
	wchar_t err_strw[buff_size] = {};

	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		err_strw, buff_size, NULL);

	char err_str[buff_size] = {};
	snprintf(err_str, buff_size, "%s", ConvertWStringToUTF8(err_strw).c_str());

	std::string err_string = err_str;
	if (!err_string.empty() && err_string.back() == '\n') {
		err_string.pop_back();
	}
	return err_string;
#else
	char err_str[buff_size] = {};

	// Thread safe (XSI-compliant)
	if (strerror_r(errCode, err_str, buff_size) == 0) {
		return "Unknown error";
	}
	return err_str;
#endif
}
