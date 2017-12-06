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

#include "GPU/Common/ShaderID.h"

namespace DX9 {

// #define USE_BONE_ARRAY

	void GenerateVertexShaderHLSL(const VShaderID &id, char *buffer, ShaderLanguage lang = HLSL_DX9);

#define CONST_VS_PROJ 0
#define CONST_VS_PROJ_THROUGH 4
#define CONST_VS_VIEW 8
#define CONST_VS_WORLD 11
#define CONST_VS_TEXMTX 14
#define CONST_VS_UVSCALEOFFSET 17
#define CONST_VS_FOGCOEF 18
#define CONST_VS_AMBIENT 19
#define CONST_VS_BONE0 20
#define CONST_VS_BONE1 23
#define CONST_VS_BONE2 26
#define CONST_VS_BONE3 29
#define CONST_VS_BONE4 32
#define CONST_VS_BONE5 35
#define CONST_VS_BONE6 38
#define CONST_VS_BONE7 41
#define CONST_VS_BONE8 44
#define CONST_VS_MATAMBIENTALPHA 47
#define CONST_VS_MATDIFFUSE 48
#define CONST_VS_MATSPECULAR 49
#define CONST_VS_MATEMISSIVE 50
#define CONST_VS_LIGHTPOS 51
#define CONST_VS_LIGHTDIR 55
#define CONST_VS_LIGHTATT 59
#define CONST_VS_LIGHTANGLE 63
#define CONST_VS_LIGHTSPOTCOEF 67
#define CONST_VS_LIGHTDIFFUSE 71
#define CONST_VS_LIGHTSPECULAR 75
#define CONST_VS_LIGHTAMBIENT 79
#define CONST_VS_DEPTHRANGE 83
#define CONST_VS_GUARDBAND 84

};
