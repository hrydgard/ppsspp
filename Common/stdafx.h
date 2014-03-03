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

#ifndef _WIN32_WINNT

#if _MSC_VER < 1700
#define _WIN32_WINNT 0x501 // Compile for XP on Visual Studio 2010 and below
#else
#define _WIN32_WINNT 0x600 // Compile for Vista on Visual Studio 2012 and above
#endif // #if _MSC_VER < 1700

#endif // #ifndef _WIN32_WINNT

#ifndef _WIN32_IE
#define _WIN32_IE 0x0500       // Default value is 0x0400
#endif

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include "CommonWindows.h"
#include <tchar.h>
#include <vector>
