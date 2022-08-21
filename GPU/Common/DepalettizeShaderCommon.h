// Copyright (c) 2014- PPSSPP Project.

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

#include <cstdint>

#include "Common/GPU/Shader.h"
#include "GPU/ge_constants.h"

static const int DEPAL_TEXTURE_OLD_AGE = 120;

struct DepalConfig {
	int mask;
	int shift;
	u32 startPos;
	GEPaletteFormat clutFormat;
	GETextureFormat textureFormat;
	GEBufferFormat bufferFormat;
};

void GenerateDepalFs(char *buffer, const DepalConfig &config, const ShaderLanguageDesc &lang);
void GenerateDepalVs(char *buffer, const ShaderLanguageDesc &lang);
