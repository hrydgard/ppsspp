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

#pragma	once

enum EmuFileType
{
	FILETYPE_ERROR,

	FILETYPE_PSP_PBP,
	FILETYPE_PSP_ELF,
	FILETYPE_PSP_ISO,
	FILETYPE_PSP_ISO_NP,

	FILETYPE_UNKNOWN_BIN,
	FILETYPE_UNKNOWN_ELF,

	FILETYPE_UNKNOWN
};

EmuFileType Identify_File(const char *filename);

bool LoadFile(const char *filename, std::string *error_string);
