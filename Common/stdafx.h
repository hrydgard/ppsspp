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

#pragma once

#ifdef _WIN32
#ifndef _WIN32_WINNT

#if defined(_MSC_VER) && _MSC_VER < 1700
#error You need a newer version of Visual Studio.
#else
#define _WIN32_WINNT 0x601
#endif

#endif // #ifndef _WIN32_WINNT

#undef WINVER
#define WINVER _WIN32_WINNT
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600       // Default value is 0x0400
#endif

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include "CommonWindows.h"
#include <tchar.h>
#include <vector>
#endif
