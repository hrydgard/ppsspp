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

#include "Globals.h"

namespace DX9 {

// #define USE_BONE_ARRAY

struct VertexShaderIDDX9
{
	VertexShaderIDDX9() {d[0] = 0xFFFFFFFF;}
	void clear() {d[0] = 0xFFFFFFFF;}
	u32 d[2];
	bool operator < (const VertexShaderIDDX9 &other) const
	{
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++)
		{
			if (d[i] < other.d[i])
				return true;
			if (d[i] > other.d[i])
				return false;
		}
		return false;
	}
	bool operator == (const VertexShaderIDDX9 &other) const
	{
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++)
		{
			if (d[i] != other.d[i])
				return false;
		}
		return true;
	}
};

bool CanUseHardwareTransformDX9(int prim);

void ComputeVertexShaderIDDX9(VertexShaderIDDX9 *id, u32 vertType, int prim, bool useHWTransform);
void GenerateVertexShaderDX9(int prim, char *buffer, bool useHWTransform);

// Collapse to less skinning shaders to reduce shader switching, which is expensive.
int TranslateNumBonesDX9(int bones);

#define CONST_VS_PROJ 0
#define CONST_VS_PROJ_THROUGH 4
#define CONST_VS_VIEW 8
#define CONST_VS_WORLD 12
#define CONST_VS_TEXMTX 16
#define CONST_VS_BONE0 20
#define CONST_VS_BONE1 24
#define CONST_VS_BONE2 28
#define CONST_VS_BONE3 32
#define CONST_VS_BONE4 36
#define CONST_VS_BONE5 40
#define CONST_VS_BONE6 44
#define CONST_VS_BONE7 48
#define CONST_VS_BONE8 52
#define CONST_VS_FOGCOEF 56
#define CONST_VS_UVSCALEOFFSET 57
#define CONST_VS_AMBIENT 58
#define CONST_VS_MATAMBIENTALPHA 59
#define CONST_VS_MATDIFFUSE 60
#define CONST_VS_MATSPECULAR 61
#define CONST_VS_MATEMISSIVE 62
#define CONST_VS_LIGHTPOS 64
#define CONST_VS_LIGHTDIR 68
#define CONST_VS_LIGHTATT 72
#define CONST_VS_LIGHTANGLE 76
#define CONST_VS_LIGHTSPOTCOEF 80
#define CONST_VS_LIGHTDIFFUSE 84
#define CONST_VS_LIGHTSPECULAR 88
#define CONST_VS_LIGHTAMBIENT 92

};
