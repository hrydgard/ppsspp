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

#include <string>

enum IdentifiedFileType {
	FILETYPE_ERROR,

	FILETYPE_PSP_PBP_DIRECTORY,

	FILETYPE_PSP_PBP,
	FILETYPE_PSP_ELF,
	FILETYPE_PSP_ISO,
	FILETYPE_PSP_ISO_NP,

	FILETYPE_PSP_DISC_DIRECTORY,

	FILETYPE_UNKNOWN_BIN,
	FILETYPE_UNKNOWN_ELF,

	// Try to reduce support emails...
	FILETYPE_ARCHIVE_RAR,
	FILETYPE_ARCHIVE_ZIP,
	FILETYPE_PSP_PS1_PBP,
	FILETYPE_ISO_MODE2,

	FILETYPE_NORMAL_DIRECTORY,

	FILETYPE_UNKNOWN
};

// This can modify the string, for example for stripping off the "/EBOOT.PBP"
// for a FILETYPE_PSP_PBP_DIRECTORY.
IdentifiedFileType Identify_File(std::string &str);

// Can modify the string filename, as it calls IdentifyFile above.
bool LoadFile(std::string &filename, std::string *error_string);
