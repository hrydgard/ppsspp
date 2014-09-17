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

// TODO: Bench both ways. Result may be different on old vs new hardware though..
// #define DX9_USE_HW_ALPHA_TEST 1

namespace DX9 {

struct FragmentShaderIDDX9 {
	FragmentShaderIDDX9() {clear();}
	void clear() {d[0] = 0xFFFFFFFF; d[1] = 0xFFFFFFFF;}
	u32 d[2];
	bool operator < (const FragmentShaderIDDX9 &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] < other.d[i])
				return true;
			if (d[i] > other.d[i])
				return false;
		}
		return false;
	}
	bool operator == (const FragmentShaderIDDX9 &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] != other.d[i])
				return false;
		}
		return true;
	}
};

void ComputeFragmentShaderIDDX9(FragmentShaderIDDX9 *id);
void GenerateFragmentShaderDX9(char *buffer);

bool IsAlphaTestAgainstZero();
bool IsAlphaTestTriviallyTrue();
bool IsColorTestTriviallyTrue();

#define CONST_PS_TEXENV 0
#define CONST_PS_ALPHACOLORREF 1
#define CONST_PS_ALPHACOLORMASK 2
#define CONST_PS_FOGCOLOR 3

// For stencil upload
#define CONST_PS_STENCILVALUE 4

};
