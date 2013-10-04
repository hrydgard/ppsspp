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

#include "Common/FileUtil.h"
#include "Shaders.h"
#include "native/file/ini_file.h"
#include "Core/Config.h"

Shaders g_configshader;

Shaders::Shaders() { }
Shaders::~Shaders() { }

void Shaders::LoadfromIni(IniFile &file) {
	RestoretoDefault();
	if (!file.HasSection("FXAA")) {
		return;
	}
		IniFile::Section *FXAA = file.GetOrCreateSection("FXAA");

		FXAA->Get("Fragment", &fragment1, "fxaa.fsh");
		FXAA->Get("Vertex", &vertex1, "fxaa.vsh");

		IniFile::Section *NATURAL = file.GetOrCreateSection("Natural");

		NATURAL->Get("Fragment", &fragment2, "Natural.fsh");
		NATURAL->Get("Vertex", &vertex2, "Natural.vsh");

		IniFile::Section *CUSTOM = file.GetOrCreateSection("Custom");

		CUSTOM->Get("Fragment", &fragment3, "");
		CUSTOM->Get("Vertex", &vertex3, "");
}

void Shaders::SavetoIni(IniFile &file) {
		
		IniFile::Section *FXAA = file.GetOrCreateSection("FXAA");

		FXAA->Set("Fragment", fragment1.c_str());
		FXAA->Set("Vertex", vertex1.c_str());
		
		IniFile::Section *NATURAL = file.GetOrCreateSection("Natural");

		NATURAL->Set("Fragment", fragment2.c_str());
		NATURAL->Set("Vertex", vertex2.c_str());

		IniFile::Section *CUSTOM = file.GetOrCreateSection("Custom");

		CUSTOM->Set("Fragment", fragment3.c_str());
		CUSTOM->Set("Vertex", vertex3.c_str());
} 

void Shaders::RestoretoDefault() {
	
	fragment1 = "fxaa.fsh";
	fragment2 = "Natural.fsh";
	fragment3 = "";
	vertex1 = "fxaa.vsh";
	vertex2 = "Natural.vsh";
	vertex3 = "";
}
