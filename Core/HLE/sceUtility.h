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

#include "../../Common/ChunkFile.h"

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_LANGUAGE
*/
#define PSP_SYSTEMPARAM_LANGUAGE_JAPANESE		0
#define PSP_SYSTEMPARAM_LANGUAGE_ENGLISH		1
#define PSP_SYSTEMPARAM_LANGUAGE_FRENCH			2
#define PSP_SYSTEMPARAM_LANGUAGE_SPANISH		3
#define PSP_SYSTEMPARAM_LANGUAGE_GERMAN			4
#define PSP_SYSTEMPARAM_LANGUAGE_ITALIAN		5
#define PSP_SYSTEMPARAM_LANGUAGE_DUTCH			6
#define PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE		7
#define PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN		8
#define PSP_SYSTEMPARAM_LANGUAGE_KOREAN			9
#define PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL  	10
#define PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED	11


void __UtilityInit();
void __UtilityDoState(PointerWrap &p);
void __UtilityShutdown();

void Register_sceUtility();
