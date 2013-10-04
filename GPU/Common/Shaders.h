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

#include <string>

class IniFile;

struct Shaders {
public:
	Shaders();
	~Shaders();

	std::string fragment1;
	std::string vertex1;

	std::string fragment2;
	std::string vertex2;

	std::string fragment3;
	std::string vertex3;

	void LoadfromIni(IniFile &file);
	void SavetoIni(IniFile &file);
	void RestoretoDefault();

};

extern Shaders g_configshader;


