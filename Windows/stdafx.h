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

#pragma warning(disable:4996)

// #define WIN32_LEAN_AND_MEAN 
// WinSock2 MUST be included before <windows.h> !!!
#include <WinSock2.h>

//#ifdef WINDOWS

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#undef WINVER
#define WINVER 0x0600
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600       // Default value is 0x0400
#endif

#undef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
//#define UNICODE

#if defined _M_IX86

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")

#elif defined _M_IA64

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")

#elif defined _M_X64

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

#else

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#endif

#include <windows.h>
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
#include <fstream>

#include "Log.h"

#define THEMES

//#else


//#endif

