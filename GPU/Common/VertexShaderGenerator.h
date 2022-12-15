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

#include <cstdint>

#include "Common/CommonTypes.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/thin3d.h"

struct VShaderID;

// Can technically be deduced from the vertex shader ID, but this is safer.
enum class VertexShaderFlags : u32 {
	MULTI_VIEW = 1,
};
ENUM_CLASS_BITOPS(VertexShaderFlags);

bool GenerateVertexShader(const VShaderID &id, char *buffer, const ShaderLanguageDesc &compat, const Draw::Bugs bugs, uint32_t *attrMask, uint64_t *uniformMask, VertexShaderFlags *vertexShaderFlags, std::string *errorString);

// D3D9 constants.
enum {
	CONST_VS_PROJ = 0,
	CONST_VS_PROJ_THROUGH = 4,
	CONST_VS_VIEW = 8,
	CONST_VS_WORLD = 11,
	CONST_VS_TEXMTX = 14,
	CONST_VS_UVSCALEOFFSET = 17,
	CONST_VS_FOGCOEF = 18,
	CONST_VS_AMBIENT = 19,
	CONST_VS_MATAMBIENTALPHA = 20,
	CONST_VS_MATDIFFUSE = 21,
	CONST_VS_MATSPECULAR = 22,
	CONST_VS_MATEMISSIVE = 23,
	CONST_VS_LIGHTPOS = 24,
	CONST_VS_LIGHTDIR = 28,
	CONST_VS_LIGHTATT = 32,
	CONST_VS_LIGHTANGLE_SPOTCOEF = 36,
	CONST_VS_LIGHTDIFFUSE = 40,
	CONST_VS_LIGHTSPECULAR = 44,
	CONST_VS_LIGHTAMBIENT = 48,
	CONST_VS_DEPTHRANGE = 52,
	CONST_VS_BONE0 = 53,
	CONST_VS_BONE1 = 56,
	CONST_VS_BONE2 = 59,
	CONST_VS_BONE3 = 62,
	CONST_VS_BONE4 = 65,
	CONST_VS_BONE5 = 68,
	CONST_VS_BONE6 = 71,
	CONST_VS_BONE7 = 74,
	CONST_VS_BONE8 = 77,
	CONST_VS_CULLRANGEMIN = 80,
	CONST_VS_CULLRANGEMAX = 81,
	CONST_VS_ROTATION = 82,
};
