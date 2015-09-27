// Copyright (c) 2013- PPSSPP Project.

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

#include "file/ini_file.h"
#include "Core/Compatibility.h"
#include "Core/System.h"

void Compatibility::Load(const std::string &gameID) {
	IniFile compat;
	Clear();

	std::string path = GetSysDirectory(DIRECTORY_SYSTEM) + "compat.ini";
	if (compat.Load(path)) {
		LoadIniSection(compat, gameID);
	}

	// This loads from assets.
	if (compat.LoadFromVFS("compat.ini")) {
		LoadIniSection(compat, gameID);
	}
}

void Compatibility::Clear() {
	memset(&flags_, 0, sizeof(flags_));
}

void Compatibility::LoadIniSection(IniFile &iniFile, std::string section) {
	iniFile.Get(section.c_str(), "NoDepthRounding", &flags_.NoDepthRounding, flags_.NoDepthRounding);
}
