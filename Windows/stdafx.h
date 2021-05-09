// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#undef _WIN32_WINNT

#if defined(_MSC_VER) && _MSC_VER < 1700
#error You need a newer version of Visual Studio
#else
#define _WIN32_WINNT 0x601 // Compile for Win7 on Visual Studio 2012 and above
#endif

#undef WINVER
#define WINVER _WIN32_WINNT
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600       // Default value is 0x0400
#endif

#undef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1

#ifndef __clang__
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// WinSock2 MUST be included before <windows.h> !!!
#include <WinSock2.h>

#include "Common/CommonWindows.h"

#include <windowsx.h>
#include <process.h>
#include <tchar.h>
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <time.h>
#include <vector>
#include <map>
#include <string>

#include "Common/Log.h"
