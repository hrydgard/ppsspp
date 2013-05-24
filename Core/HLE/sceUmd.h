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

enum pspUmdState { 
	PSP_UMD_INIT		= 0x00,
	PSP_UMD_NOT_PRESENT = 0x01,
	PSP_UMD_PRESENT		= 0x02,
	PSP_UMD_CHANGED		= 0x04,
	PSP_UMD_NOT_READY	= 0x08,
	PSP_UMD_READY		= 0x10, 
	PSP_UMD_READABLE	= 0x20,
};

enum pspUmdError {
	PSP_ERROR_UMD_NOT_READY       = 0x80210001,
	PSP_ERROR_UMD_INVALID_PARAM   = 0x80010016,
};

enum pspUmdType {
	PSP_UMD_TYPE_GAME = 0x10,
	PSP_UMD_TYPE_VIDEO = 0x20,
	PSP_UMD_TYPE_AUDIO = 0x40,
};

void __UmdInit();
void __UmdDoState(PointerWrap &p);

void Register_sceUmdUser();
